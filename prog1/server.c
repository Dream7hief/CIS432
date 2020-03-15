/*
Logan B Poole
lpoole3@cs.uoregon.edu
CIS432/532, Introduction to Computer Networks
Program 1 - DuckChat

~References:
https://www.geeksforgeeks.org/map-associative-containers-the-c-standard-template-library-stl/
https://www.geeksforgeeks.org/vector-in-cpp-stl/
https://www.geeksforgeeks.org/socket-programming-cc/
https://classes.cs.uoregon.edu/03F/cis432/cis432/cis432.samples/cis432.samples.raw.html
http://man7.org/linux/man-pages/index.html
*/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h> /*Socket library*/
#include <sys/socket.h> /*Socket library*/
#include <netinet/in.h>
#include <arpa/inet.h>
#include "duckchat.h"
#include "raw.h"

#include <map>
#include <vector>
#include <iostream>

#define PACKET_MAX 65507
#define MAX_CHANNELS 40

using namespace std;

void client_login_request(void *data, struct sockaddr_in addr);
void client_logout_request(struct sockaddr_in addr);
void client_join_request(void *data, struct sockaddr_in addr);
void client_leave_request(string username, string channel);

void send_say(void *data, sockaddr_in addr);
void send_list(sockaddr_in addr);
void send_who(void *data, sockaddr_in addr);
void send_error(sockaddr_in addr, const char *msg);

void recv_from_client(void);

int find_user_index(string username, vector<string> channel);
int logged_in(struct sockaddr_in addr, map<string, struct sockaddr_in> users);
string address_to_username(map<string, struct sockaddr_in> users, struct sockaddr_in addr);
sockaddr_in username_to_address(map<string,struct sockaddr_in> users, string username);
bool user_in_channel(string username, vector<string> channel);
void cpy_string(const string& input, char *dst, size_t dst_size);

typedef map<string, struct sockaddr_in> user;

static int sockfd = -1;
static struct sockaddr_in serv_addr;
static map <string, struct sockaddr_in> users;
static map <string, vector<string>> channels;

int main(int argc, char *argv[]){

  int port_number;
  fd_set readfds;
  char hostname[UNIX_PATH_MAX];
  struct hostent *hostent;
  /////////////// struct request *req_packet;

  if(argc != 3){
     cerr << "Usage: ./server [hostname] [port number]\n";
    exit(0);
  }
  /*Parse [hostname] [port number]*/

  //Check hostname length
  if(strlen(argv[1]) > UNIX_PATH_MAX){
    cerr << "Domain name exceeds max size.\n";
    exit(1);
  }
  else {
    strcpy(hostname, argv[1]);
  }
  if ((hostent = gethostbyname(hostname)) == NULL) {
    // fprintf(stderr, "ERROR - server: gethostbyname() failed\n");
    exit(1);
	}
  //Check port range, recommended range 1024 - 49151//this link always point to first Link
  port_number = atoi(argv[2]);
  if(port_number < 1024 || port_number > 49151){
    cerr << "Port number must be in the range [1024-49151]\n";
    exit(1);
  }

  //Clear trash from serv_addr
  memset((char*)&serv_addr, sizeof(serv_addr), sizeof(serv_addr));
  //Copy formatted host address
  memcpy(&serv_addr.sin_addr, hostent->h_addr_list[0], hostent->h_length);
  //Convert port number byte order
  serv_addr.sin_port = htons(port_number);
  //AF_INET = IPv4 Internet Protocols
  serv_addr.sin_family = AF_INET;

  /*Creat Socket*/
  if((sockfd = socket(AF_INET,SOCK_DGRAM, 0)) < 0){
    cerr << "ERROR - Server: socket() failed\n";
    exit(1);
  }
  if(bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
    cerr << "ERROR - Server: bind() failed\n";
    exit(1);
  }

  //Main server process
  while(1){
    FD_ZERO(&readfds); //Clear trash from read file descriptor set
    FD_SET(sockfd,&readfds); //Add socket file descriptor to read file descriptor set

    if(select(sockfd+1, &readfds, NULL, NULL, NULL) < 0){
      cerr << "ERROR - client: select() failed\n" ;
    }
    else{
      if(FD_ISSET(sockfd, &readfds)){
        recv_from_client();
      }
    }
  }
  return 0;
}

void client_login_request(void *data, struct sockaddr_in addr){
  if(logged_in(addr, users)){
    send_error(addr, "Username already in use");
    return;
  }
  struct request_login *packet;
  packet = (struct request_login*)data;

  users.insert(make_pair(packet->req_username, addr));
  cerr << "server: " << packet->req_username << " logs in\n";
}
void client_logout_request(struct sockaddr_in addr){
  if(logged_in(addr, users) == 0){
    send_error(addr, "User not logged in");
    return;
  }
  string username = address_to_username(users, addr);
  for(map<string, vector<string>> :: iterator it = channels.begin(); it != channels.end(); it++ ){
    client_leave_request(username,it->first);
  }
  users.erase(username);
  cerr << "server: "<< username << " logged out\n";
}
void client_join_request(void *data, struct sockaddr_in addr){
  if(logged_in(addr, users) == 0){
    send_error(addr, "User not logged in");
    return;
  }
  string username = address_to_username(users, addr);
  struct request_join *packet;
  packet = (struct request_join*)data;
  if(channels.size() == MAX_CHANNELS){
    send_error(addr, "Server has reached maximum channels");
    return;
  }
  cerr << "server: " << username <<" joins " << packet->req_channel << endl;;
  bool ex = false;
  for (map<string,vector<string>> :: iterator it = channels.begin(); it != channels.end(); it++){
    if (it->first==packet->req_channel){
      //channel already exists, have to check if user already in channel
      if (!user_in_channel(username,it->second)){
        //can add user to channel
        it->second.push_back(username);
      }
      ex = true;
    }
  }
  if (!ex){
    vector<string> joined_users;
    joined_users.push_back(username);
    //channel doesn't exist, create channel and add user to channel
    channels.insert(make_pair(packet->req_channel,joined_users));
  }
}
void client_leave_request(string username, string channel){
  for(map<string, vector<string>> :: iterator it = channels.begin(); it != channels.end(); it++){
    if(channel == it->first){
      int n;
      n = find_user_index(username, it->second);
      if(n>-1){
        cerr << "server: " << username << " leaves channel " << channel << endl;
        it->second.erase(it->second.begin()+n);
      }
      if(it->second.size() == 0 && channel != "Common" ){
        cerr << "server: removing empty channel " << channel << endl;
        channels.erase(it->first);
      }
    }
    return;
  }
}

void send_say(void *data, sockaddr_in addr){
  if(logged_in(addr, users) == 0){
    send_error(addr, "User not logged in");
    return;
  }
  struct request_say *packet;
  packet = (request_say*)data;
  string username = address_to_username(users, addr);
  cerr << "server: " << username << " sends say message in " << packet->req_channel << endl;
  struct text_say send_packet;
  send_packet.txt_type = TXT_SAY;
  strcpy(send_packet.txt_channel,packet->req_channel);

  cpy_string(username, send_packet.txt_username, sizeof(send_packet.txt_username));
  strcpy(send_packet.txt_text,packet->req_text);
  for (map<string,vector<string>> :: iterator it = channels.begin(); it != channels.end(); it++){
    if (it->first==packet->req_channel){
      //this is the right channel, iterate through the users to send the message multiple times
      for (int i = 0; i < (int)it->second.size(); i++){
        string username = it->second[i];
        sockaddr_in client_addr = username_to_address(users,username);
        sendto(sockfd, &send_packet, sizeof(send_packet), 0, (const sockaddr *)&client_addr,sizeof(client_addr));
      }
    }
  }
}
void send_list(sockaddr_in addr){
  if(logged_in(addr, users) == 0){
    send_error(addr, "User not logged in");
    return;
  }
  struct text_list packet[channels.size()+8];
  packet->txt_type = TXT_LIST;
  packet->txt_nchannels = channels.size();
  int index = 0;
  string username = address_to_username(users, addr);
  cerr << "server: " << username << " lists channels" << endl;
  for (map<string, vector<string>> :: iterator it = channels.begin(); it != channels.end(); it++){
    cpy_string(it->first,packet->txt_channels[index].ch_channel,sizeof(packet->txt_channels[index].ch_channel));
    index++;
  }
  sendto(sockfd,&packet,sizeof(packet),0,(const sockaddr *)&addr,sizeof(addr));
}
void send_who(void *data, sockaddr_in addr){
  if(logged_in(addr, users) == 0){
    send_error(addr, "User not logged in");
    return;
  }
  struct request_who *req_packet;
  req_packet = (struct request_who*)data;
  string username = address_to_username(users, addr);
  cerr << "server: " << username << " lists users on channel " << req_packet->req_channel << endl;
  for (map<string, vector<string>> :: iterator it = channels.begin(); it!=channels.end(); it++){
    if (it->first==req_packet->req_channel){
      struct text_who packet[it->second.size()+40];
      packet->txt_type = TXT_WHO;

      cpy_string(it->first,packet->txt_channel,sizeof(packet->txt_channel));
      packet->txt_nusernames = it->second.size();
      for (int i = 0; i < (int)it->second.size(); i++){
        cpy_string(it->second[i],packet->txt_users[i].us_username,sizeof(packet->txt_users[i].us_username));
      }
      sendto(sockfd,&packet,sizeof(packet),0,(const sockaddr *)&addr, sizeof(addr));
      return;
    }
  }
  send_error(addr, "Channel does not exist");
}
void send_error(sockaddr_in addr, const char *msg){
  struct text_error packet;
  packet.txt_type = TXT_ERROR;
  strcpy(packet.txt_error,msg);
  sendto(sockfd ,&packet,sizeof(packet), 0, (const sockaddr*)&addr, sizeof(addr));
}

void recv_from_client(void){
  struct request *req_packet;
  struct sockaddr_in recv_client;
  size_t rt_len;
  void *data;
  socklen_t rc_len;
  char recv_text[PACKET_MAX];

  rt_len = sizeof(recv_text);
  data = &recv_text;
  rc_len = sizeof(recv_client);

  if(recvfrom(sockfd, data, rt_len, 0, (struct sockaddr*)&recv_client, &rc_len) < 0){
    cerr << "ERROR - server: recvfrom() failed\n";
  }

  req_packet = (struct request*)data;

  switch(req_packet->req_type){
    case REQ_LOGIN:
      client_login_request(data, recv_client);
      break;
    case REQ_LOGOUT:
      client_logout_request(recv_client);
      break;
    case REQ_JOIN:
      client_join_request(data, recv_client);
      break;
    case REQ_LEAVE:
      client_leave_request(address_to_username(users, recv_client), ((request_leave*)req_packet)->req_channel);
      break;
    case REQ_SAY:
      send_say(data, recv_client);
      break;
    case REQ_LIST:
      send_list(recv_client);
      break;
    case REQ_WHO:
      send_who(data, recv_client);
      break;
    default:
      send_error(recv_client, "Unrecognized packet type");
      break;
  }
}

int find_user_index(string username, vector<string> channel){
  for(int i = 0; i < (int)channel.size(); i++){
    if(username == channel[i]){
      return i;
    }
  }
  return -1;
}
int logged_in(struct sockaddr_in addr, map<string, struct sockaddr_in> users){
  for (map<string,struct sockaddr_in>::iterator it = users.begin(); it != users.end(); it++){
    if (addr.sin_port == it->second.sin_port){
      return 1;
    }
  }
  return 0;
}
string address_to_username(map<string, struct sockaddr_in> users, struct sockaddr_in addr){
  for(map<string,struct sockaddr_in>::iterator it = users.begin(); it!=users.end(); it++){
    if (it->second.sin_port == addr.sin_port){
      return it->first;
    }
  }
  send_error(addr, "Invalid user");
  return "NULL";
}
sockaddr_in username_to_address(map<string,struct sockaddr_in> users, string username){
  return users[username];
}
bool user_in_channel(string username, vector<string> channel){
  for (int i = 0; i < (int)channel.size(); i++){
    if (username == channel[i]){
       return true;
     }
  }
  return false;
}
void cpy_string(const string& input, char *output, size_t output_size){
  strncpy(output, input.c_str(),output_size - 1);
  output[output_size -1] = '\0';
}
