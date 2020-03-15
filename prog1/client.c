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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>
#include <iostream>

#include "duckchat.h"
#include "raw.h"

#define PACKET_MAX 65507
#define BUFF_SIZE 16000
#define STDIN 0

using namespace std;

/*Methods for sending messages to server*/
void send_login_req(char *username);
void send_logout_request(void);
void send_join_request(char *channel);
void send_leave_request(char *channel);
void send_list_request(void);
void send_who_request(char *channel);
void send_say_request(char *text);

/*Methods for receiving messages from server and parsing user input*/
void rcv_from_stdin(void);
void rcv_from_server(void);

/*Helper Methods*/
void logout_cleanup(void);
void prompt(void);

struct sockaddr_in serv_addr;
static int sockfd = -1;
static char buffer[BUFF_SIZE];
static map <string, string> sub_channels;
static char active_channel[CHANNEL_MAX];

int main(int argc, const char *argv[]){
  raw_mode();
  atexit(cooked_mode);

  int port_number;
  char username[USERNAME_MAX];
  char hostname[UNIX_PATH_MAX];
  struct hostent *hostent;

  if(argc != 4){
    cerr << "Usage: ./client [hostname] [port number] [username]\n";
    exit(1);
  }

  /*Parse [hostname] [port number] [username]*/

  //Check hostname length
  if(strlen(argv[1]) > UNIX_PATH_MAX){
    cerr << "Domain name exceeds max size.\n";
    exit(1);
  }
  else {
    strcpy(hostname, argv[1]);
  }

  //Check port range, recommended range 1024 - 49151
  port_number = atoi(argv[2]);
  if(port_number < 1024 || port_number > 49151){
    cerr << "Port number must be in the range [1024-49151]\n";
    exit(1);
  }

  //Check username length
  if(strlen(argv[3]) > USERNAME_MAX){
    cerr << "Username exceeds max size.\n";
    exit(1);
  }
  else {
    strcpy(username, argv[3]);
  }

  //Create Socket
  if((sockfd = socket(AF_INET,SOCK_DGRAM, 0)) < 0){
    cerr << "ERROR - client: socket() failed\n";
    exit(1);
  }

  //Convert hostname to usuable address
  if((hostent = gethostbyname(hostname)) == NULL){
    cerr << "ERROR - client: gethostbyname() failed\n";
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

  //Login to server
  send_login_req(username);
  //Set active channel to default channel: Common
  strcpy(active_channel, "Common");
  //Join Common channel
  send_join_request(active_channel);

  prompt();

  //Main client process
  while(1){
    fflush(stdout);
    fd_set readfds;

    FD_ZERO(&readfds); //Clear trash from read file descriptor set
    FD_SET(STDIN, &readfds); //Add stdin(fd 0) to read file descriptor set
    FD_SET(sockfd,&readfds); //Add socket file descriptor to read file descriptor set

    if(select(sockfd+1, &readfds, NULL, NULL, NULL) < 0){
      cerr << "ERROR - client: select() failed\n";
    }
    else{
      if(FD_ISSET(STDIN, &readfds)){
        rcv_from_stdin();
      }
      if(FD_ISSET(sockfd, &readfds)){
        rcv_from_server();
      }
    }
  }
  exit(0);
}

/*  LOGIN REQUEST
0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                32-bit message type identifier (0)             |
+---------------------------------------------------------------+
|                     32-byte user name                         |
                             ...
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
void send_login_req(char *username){
  size_t req_size;
  void *data;
  struct request_login login_req;

  login_req.req_type = REQ_LOGIN;
  strcpy(login_req.req_username, username);
  req_size = sizeof(login_req);
  data = &login_req;

  if(sendto(sockfd, data, req_size, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
    cerr << "ERROR - client: send_login_req() sendto() failed\n";
  }
}

/*  LOGOUT REQUEST(1)
00                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                32-bit message type identifier (1)             |
+---------------------------------------------------------------+
*/
void send_logout_request(){
  size_t req_size;
  void *data;
  struct request_logout logout_req;

  logout_req.req_type = REQ_LOGOUT;
  req_size = sizeof(logout_req);
  data = &logout_req;

  if(sendto(sockfd, data, req_size, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
    cerr << "ERROR - client: send_logout_request() sendto() failed\n";
  }
}

/*  JOIN REQUEST(2)
0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                32-bit message type identifier (2)             |
+---------------------------------------------------------------+
|                     32-byte channel name                      |
                             ...
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
void send_join_request(char *channel){
  size_t req_size;
  void *data;
  struct request_join join_req;

  join_req.req_type = REQ_JOIN;
  strcpy(join_req.req_channel, channel);
  req_size = sizeof(join_req);
  data = &join_req;

  if(sendto(sockfd, data, req_size, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
    cerr << "ERROR - client: send_join_request() sendto() failed\n";
  }
  else{
    map<string, string> :: iterator it;
    strcpy(active_channel, channel);
    it = sub_channels.find(channel);
    if(it == sub_channels.end()){
      sub_channels.insert(pair<string, string>(channel, channel));
    }
  }
  strcpy(active_channel, channel);
}

/*  LEAVE REQUEST(3)
0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                32-bit message type identifier (3)             |
+---------------------------------------------------------------+
|                     32-byte channel name                      |
                             ...
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
void send_leave_request(char *channel){
  size_t req_size;
  void *data;
  struct request_leave leave_req;

  leave_req.req_type = REQ_LEAVE;
  strcpy(leave_req.req_channel, channel);
  req_size = sizeof(leave_req);
  data = &leave_req;

  if(sendto(sockfd, data, req_size, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
    cerr << "ERROR - client: send_leave_request() sendto() failed\n";
  }
}

/* SAY REQUEST(4)
0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                32-bit message type identifier (4)             |
+---------------------------------------------------------------+
|                     32-byte channel name                      |
                             ...
|                                                               |
+---------------------------------------------------------------+
|                     64-byte text field                        |
                             ...
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
void send_say_request(char *text){
  size_t req_size;
  void *data;
  struct request_say say_req;

  say_req.req_type = REQ_SAY;
  strcpy(say_req.req_channel, active_channel);
  strcpy(say_req.req_text, text);
  req_size = sizeof(say_req);
  data = &say_req;

  if(sendto(sockfd, data, req_size, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
    cerr << "ERROR - client: send_say_request() sendto() failed\n";
  }
}

/* LIST REQUEST(5)
0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                32-bit message type identifier (5)             |
+---------------------------------------------------------------+
*/
void send_list_request(){
  size_t req_size;
  void *data;
  struct request_list list_req;

  list_req.req_type = REQ_LIST;
  req_size = sizeof(list_req);
  data = &list_req;

  if(sendto(sockfd, data, req_size, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
    cerr << "ERROR - client: send_list_request() sendto() failed\n";
  }

}

/* WHO REQUEST(6)
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                32-bit message type identifier (6)             |
+---------------------------------------------------------------+
|                     32-byte channel name                      |
                            ...
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
void send_who_request(char *channel){
  size_t req_size;
  void *data;
  struct request_who who_req;

  who_req.req_type = REQ_WHO;
  strcpy(who_req.req_channel, channel);
  req_size = sizeof(who_req);
  data = &who_req;

  if(sendto(sockfd, data, req_size, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
    cerr << "ERROR - client: send_who_request() sendto() failed\n";
  }
}

void rcv_from_stdin(void){
  int in_length = 0;
  char in_char = '\0';

  while(in_char != '\n'){
    in_char = getchar();
    /*If input character isn't the newline character and check input length to
    ensure space is left for null character*/
    if((in_char != '\n') && (in_length < SAY_MAX - 1)){
      buffer[in_length] = in_char;
      putchar(in_char);
      in_length++;
    }
  }
  buffer[in_length] = '\0';
  fprintf(stdout,"\n");

  if(buffer[0] == '/'){
    char *s_command[2];
    s_command[0] = strtok(buffer, " ");

    if(strcmp(s_command[0], "/exit") == 0){
      send_logout_request();
      logout_cleanup();
    }
    else if(strcmp(s_command[0], "/list") == 0){
      send_list_request();
    }
    else {
      if(s_command[0] == NULL){
        fprintf(stdout, "*Unknown command\n");
      }
      else{
        s_command[1] = strtok(NULL, "\0");
        if(s_command[1] == NULL){
          fprintf(stdout, "*Unknown command\n");
        }
        else if(strncmp(s_command[0], "/join", 5) == 0){

          send_join_request(s_command[1]);
          strcpy(active_channel, s_command[1]);
        }
        else if(strncmp(s_command[0], "/leave", 6) == 0){
          send_leave_request(s_command[1]);
        }
        else if(strncmp(s_command[0], "/who", 4) == 0){
          send_who_request(s_command[1]);
        }
        else if(strncmp(s_command[0], "/switch", 7) == 0){
          //find channel to switch to. IF not there error.
          map<string, string> :: iterator it;
          strcpy(active_channel, s_command[1]);
          it = sub_channels.find(s_command[1]);
          if(it != sub_channels.end()){
            sub_channels.insert(pair<string, string>(string(s_command[1]), string(s_command[1])));
          }
          else{
            fprintf(stdout, "You have not subscribed to channel %s\n",s_command[1]);
          }
        }
      }
    }
  }
  else{
    if(active_channel != NULL){
      send_say_request(buffer);
    }
  }
  prompt();
}

void rcv_from_server(void){
  struct sockaddr_in recv_serv;
  size_t rt_len;
  void *data;
  socklen_t rs_len;
  char recv_text[PACKET_MAX];

  rt_len = sizeof(recv_text);
  data = &recv_text;
  rs_len = sizeof(recv_serv);

  if(recvfrom(sockfd, data, rt_len, 0, (struct sockaddr*)&serv_addr, &rs_len) < 0){
    cerr << "ERROR - client: recvfrom() failed\n";
  }

  fprintf(stdout, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");

  struct text *recv_data;
  recv_data = (struct text*)data;
  text_t response_type = recv_data->txt_type;

  if(response_type == TXT_SAY){
    struct text_say *text;
    text = (struct text_say*)data;

    fprintf(stdout, "[%s][%s]: %s\n", text->txt_channel, text->txt_username, text->txt_text);
  }
  else if(response_type == TXT_LIST){
    struct text_list *text;
    text = (struct text_list*)data;

    fprintf(stdout, "Existing channels:\n");
    struct channel_info* channel;
    channel = text->txt_channels;
    for (int i = 0; i < text->txt_nchannels; i++){
      fprintf(stdout, " %s\n",(channel+i)->ch_channel);
    }
  }
  else if(response_type == TXT_WHO){
    struct text_who *text;
    text = (struct text_who*)data;

    fprintf(stdout, "Users on channel %s:\n",text->txt_channel);

    struct user_info* user;
    user = text->txt_users;
    for (int i = 0; i < text->txt_nusernames; i++){
      fprintf(stdout, " %s\n",(user+i)->us_username);
    }
  }
  else if(response_type == TXT_ERROR){
    struct text_error *text;
    text = (struct text_error*)data;

    fprintf(stdout, "Error: %s\n",text->txt_error);
  }

  prompt();
}

void logout_cleanup(void){
  close(sockfd);
  exit(0);
}

void prompt(void){
  fprintf(stdout, "> ");
  fflush(stdout);
}
