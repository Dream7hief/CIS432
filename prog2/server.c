#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <iostream>
#include <time.h>


using namespace std;



//#include "hash.h"
#include "duckchat.h"


#define MAX_CONNECTIONS 10
#define HOSTNAME_MAX 100
#define MAX_MESSAGE_LEN 65536
#define MAX_UIDS 1000

//typedef map<string,string> channel_type; //<username, ip+port in string>
typedef map<string,struct sockaddr_in> channel_type; //<username, sockaddr_in of user>

int s; //socket for listening
struct sockaddr_in server;
long uid_q[MAX_UIDS];
int size_q = -1;
int flag;

map<string,struct sockaddr_in> usernames; //<username, sockaddr_in of user>
map<string,int> active_usernames; //0-inactive , 1-active
map<string,string> rev_usernames; //<ip+port in string, username>
map<string,channel_type> channels;

//Static after setup - each channel has own version
map<string, struct sockaddr_in> adj_servers; //<ip+port in string, sockaddr_in of server>
map<string, map<string, struct sockaddr_in>> server_channels;
map<string, pair<string, time_t> > server_timers;

void handle_socket_input();
void handle_login_message(void *data, struct sockaddr_in sock);
void handle_logout_message(struct sockaddr_in sock);
void handle_join_message(void *data, struct sockaddr_in sock);
void handle_leave_message(void *data, struct sockaddr_in sock);
void handle_say_message(void *data, struct sockaddr_in sock);
void handle_list_message(struct sockaddr_in sock);
void handle_who_message(void *data, struct sockaddr_in sock);
void handle_keep_alive_message(struct sockaddr_in sock);
void send_error_message(struct sockaddr_in sock, string error_msg);

void server_join_message(void *data, struct sockaddr_in sock);
void server_leave_message(void *data, struct sockaddr_in sock);
void server_say_message(void *data, struct sockaddr_in sock);
void broadcast_server_join(string sender_id, char *channel);

int main(int argc, char *argv[])
{

	if (argc < 3 || (argc % 2 == 0))
	{
		printf("Usage: ./server domain_name port_num\n");
		printf("Usage: ./server domain_name port_num [Optional]\n");
		printf("[Optional]: neighbor_domain_name neighbor_port_num\n");
		exit(1);
	}
	else
	{
		struct sockaddr_in a_server;
		struct hostent *hp;
		char domain[HOSTNAME_MAX];
		char port_str[6];
		int port;
		string ip;
		string key;

		for(int i = 3; i < argc; i+=2)
		{

			if((hp = gethostbyname(argv[i])) == NULL)
			{
				puts("error resolving hostname..");
				exit(1);
			}
			memcpy(&a_server.sin_addr, hp->h_addr_list[0], hp->h_length);
			ip = inet_ntoa(a_server.sin_addr);
			port = atoi(argv[i+1]);
			sprintf(port_str, "%d", port);

			a_server.sin_family = AF_INET;
			a_server.sin_port = htons(port);

			key = ip + "." + port_str;
			// cout << "Key: " << key << endl;
			adj_servers[key] = a_server;
		}
	}

	char hostname[HOSTNAME_MAX];
	int port;

	strcpy(hostname, argv[1]);
	port = atoi(argv[2]);

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		perror ("socket() failed\n");
		exit(1);
	}

	struct hostent *he;

	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if ((he = gethostbyname(hostname)) == NULL) {
		puts("error resolving hostname..");
		exit(1);
	}
	memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
	/* Verify data - DELETE ///////////////////////////////////////////////////*/
	// cout << "This Server: " << inet_ntoa(server.sin_addr)
	// 	<< "."<< ntohs(server.sin_port) << endl;
	//
	// map<string, struct sockaddr_in> :: iterator iter;
	// for(iter = adj_servers.begin(); iter != adj_servers.end(); iter++)
	// {
	// 	cout << "Adjacent Server: " << iter->first << endl;
	// }
	//////////////////////////////////////////////////////////////////////
	int err;

	err = bind(s, (struct sockaddr*)&server, sizeof server);

	if (err < 0)
	{
		perror("bind failed\n");
	}

	//testing maps end

	//create default channel Common
	string default_channel = "Common";
	map<string,struct sockaddr_in> default_channel_users;
	channels[default_channel] = default_channel_users;

	time_t start_time;
	time(&start_time);

	flag = 0;

	while(1) //server runs for ever
	{
		time_t current_time;
		time(&current_time);
		double elapsed_time = (double)difftime(current_time, start_time);
		//use a file descriptor with a timer to handle timeouts

		if(elapsed_time >= 5)
		{
			cout << "seconds elapsed: " << elapsed_time << endl;
			cout << "flag counter " << flag << endl;
			if(flag == 12)
			{
				flag = 0;
				map<string, struct sockaddr_in>::iterator server_iter;
				for (server_iter = adj_servers.begin(); server_iter != adj_servers.end(); server_iter++)
				{
						map<string, channel_type>::iterator channel_iter;
						for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
						{
							struct server_request_join s2s_join;
							ssize_t bytes;
							void *send_data;
							size_t len;
							s2s_join.req_type = SERV_JOIN;
							strcpy(s2s_join.req_channel, channel_iter->first.c_str());
							send_data = &s2s_join;
							len = sizeof s2s_join;
							bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&(server_iter->second), sizeof (server_iter->second));
							if (bytes < 0)
							{
									perror("Message failed");
							}
							else
							{
								cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) << " "
										<< inet_ntoa((server_iter->second).sin_addr) << ":"
										<< (int)ntohs((server_iter->second).sin_port)
										<< " send S2S Join " << channel_iter->first << endl;
							}
						}

						map<string, map<string, struct sockaddr_in> >::iterator server_channel_iter;
	          for (server_channel_iter = server_channels.begin(); server_channel_iter != server_channels.end(); server_channel_iter++)
	          {
	            string server_id;
	            char port_str[6];
	            int port;
	            port = (int)ntohs(server_iter->second.sin_port);
	            sprintf(port_str, "%d", port);
	            strcpy(port_str, port_str);
	            string ip = inet_ntoa((server_iter->second).sin_addr);
	            server_id = ip + "." + port_str;

	            string channel = server_channel_iter->first;
	            map<string, pair<string, time_t> >::iterator timer_iter;
	            timer_iter = server_timers.find(server_id);
	            if (timer_iter == server_timers.end() )
	            {
	                //no timer found for this channel/server combo
	                //nothing to do
	                //cout << "No timer found for this server/channel pair, nothing to do" << endl;
	            }
	            else
	            {
	              //timer found, check > 120
	              time_t curr_time;
	              time(&curr_time);
	              double elapsed = (double)difftime(curr_time, (timer_iter->second).second);
	              if (elapsed >= 120)
	              {
	                cout << "Removing server " << server_id << " from channel " << channel  << " subscribers!" << endl;
	                map<string, struct sockaddr_in>::iterator find_iter;
	                find_iter = server_channels[channel].find(server_id);
	                if (find_iter != server_channels[channel].end())
	                {
	                  server_channels[channel].erase(find_iter);
	                  break;
	                }
	                //cout << "Couldn't find server's channel's timer." << endl;
	                //server_channels[channel].erase(server_id);
	              }
	            }
	          }
				}
			}
			else
			{
				flag++;
				map<string, struct sockaddr_in>::iterator server_iter;
				for (server_iter = adj_servers.begin(); server_iter != adj_servers.end(); server_iter++)
				{
						//this should only remove servers from server_channels map (map<channel, map<server_id string, server sockaddr_in> >)
						map<string, map<string, struct sockaddr_in> >::iterator channel_iter;
						for (channel_iter = server_channels.begin(); channel_iter != server_channels.end(); channel_iter++)
						{
								string server_id;
								char port_str[6];
								int port = (int)ntohs((server_iter->second).sin_port);
								sprintf(port_str,"%d",port);

								string ip = inet_ntoa((server_iter->second).sin_addr);
								server_id = ip + "." + port_str;

								string channel = channel_iter->first;
								map<string, pair<string, time_t> >::iterator timer_iter;
								timer_iter = server_timers.find(server_id);
								if (timer_iter == server_timers.end() )
								{
										//no timer found for this channel/server combo
								}
								else
								{
										//timer found, check > 120
										time_t curr_time;
										time(&curr_time);
										double elapsed = (double)difftime(curr_time, (timer_iter->second).second);
										if (elapsed > 120)
										{
												//remove channel from subscriptions
												map<string, struct sockaddr_in>::iterator find_iter;
												find_iter = server_channels[channel].find(server_id);
												if (find_iter != server_channels[channel].end())
												{
														server_channels[channel].erase(find_iter);
														//cout << "Removing server from channel subscribers!" << endl;
														break;
												}
												//cout << "Couldn't find server's channel's timer." << endl;
												//server_channels[channel].erase(server_identifier);
										}
										//else {cout << "elapsed < 120 for this comparison:" << elapsed << endl;}
								}
						}
				}
			}
			time(&start_time);
		}
		int rc;
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(s, &fds);

		struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
		rc = select(s+1, &fds, NULL, NULL, &timeout);
		if (rc < 0)
		{
			printf("error in select\n");
      getchar();
		}
		else
		{
			int socket_data = 0;

			if (FD_ISSET(s,&fds))
			{
				//reading from socket
				handle_socket_input();
				socket_data = 1;
			}
		}
	}
	return 0;
}

void handle_socket_input()
{
	struct sockaddr_in recv_client;
	ssize_t bytes;
	void *data;
	size_t len;
	socklen_t fromlen;
	fromlen = sizeof(recv_client);
	char recv_text[MAX_MESSAGE_LEN];
	data = &recv_text;
	len = sizeof recv_text;


	bytes = recvfrom(s, data, len, 0, (struct sockaddr*)&recv_client, &fromlen);


	if (bytes < 0)
	{
		perror ("recvfrom failed\n");
	}
	else
	{
		//printf("received message\n");

		struct request* request_msg;
		request_msg = (struct request*)data;

		//printf("Message type:");
		request_t message_type = request_msg->req_type;

		//printf("%d\n", message_type);

		if (message_type == REQ_LOGIN)
		{
			handle_login_message(data, recv_client); //some methods would need recv_client
		}
		else if (message_type == REQ_LOGOUT)
		{
			handle_logout_message(recv_client);
		}
		else if (message_type == REQ_JOIN)
		{
			handle_join_message(data, recv_client);
		}
		else if (message_type == REQ_LEAVE)
		{
			handle_leave_message(data, recv_client);
		}
		else if (message_type == REQ_SAY)
		{
			handle_say_message(data, recv_client);
		}
		else if (message_type == REQ_LIST)
		{
			handle_list_message(recv_client);
		}
		else if (message_type == REQ_WHO)
		{
			handle_who_message(data, recv_client);
		}
		else if (message_type == SERV_JOIN)
		{
			server_join_message(data, recv_client);
		}
		else if (message_type == SERV_LEAVE)
		{
			server_leave_message(data, recv_client);
		}
		else if (message_type == SERV_SAY)
		{
			server_say_message(data, recv_client);
		}
		else
		{
			//send error message to client
			send_error_message(recv_client, "*Unknown command");
		}




	}


}
void handle_login_message(void *data, struct sockaddr_in sock)
{
	struct request_login* msg;
	msg = (struct request_login*)data;

	string username = msg->req_username;
	usernames[username]	= sock;
	active_usernames[username] = 1;

	//rev_usernames[sock] = username;

	//char *inet_ntoa(struct in_addr in);
	string ip = inet_ntoa(sock.sin_addr);
	//cout << "ip: " << ip <<endl;
	int port = sock.sin_port;
	//unsigned short short_port = sock.sin_port;
	//cout << "short port: " << short_port << endl;
	//cout << "port: " << port << endl;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;

	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;
	rev_usernames[key] = username;

	cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
  << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
  << " recv Request login " << username << endl;

}
void handle_logout_message(struct sockaddr_in sock)
{

	//construct the key using sockaddr_in
	string ip = inet_ntoa(sock.sin_addr);
	//cout << "ip: " << ip <<endl;
	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;

	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;

	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;

	/*
    for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
    {
        cout << "key: " << iter->first << " username: " << iter->second << endl;
    }
	*/




	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//send an error message saying not logged in
		send_error_message(sock, "Not logged in");
	}
	else
	{
		//cout << "key " << key << " found."<<endl;
		string username = rev_usernames[key];
		rev_usernames.erase(iter);

		//remove from usernames
		map<string,struct sockaddr_in>::iterator user_iter;
		user_iter = usernames.find(username);
		usernames.erase(user_iter);

		//remove from all the channels if found
		map<string,channel_type>::iterator channel_iter;
		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			//cout << "key: " << iter->first << " username: " << iter->second << endl;
			//channel_type current_channel = channel_iter->second;
			map<string,struct sockaddr_in>::iterator within_channel_iterator;
			within_channel_iterator = channel_iter->second.find(username);
			if (within_channel_iterator != channel_iter->second.end())
			{
				channel_iter->second.erase(within_channel_iterator);
			}
		}

		//remove entry from active usernames also
		//active_usernames[username] = 1;
		map<string,int>::iterator active_user_iter;
		active_user_iter = active_usernames.find(username);
		active_usernames.erase(active_user_iter);

    cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
    << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
    << " recv Request logout " << username << endl;

	}


	/*
    for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
    {
        cout << "key: " << iter->first << " username: " << iter->second << endl;
    }
	*/


	//if so delete it and delete username from usernames
	//if not send an error message - later

}
void handle_join_message(void *data, struct sockaddr_in sock)
{
	//get message fields
	struct request_join* msg;
	msg = (struct request_join*)data;
	string channel = msg->req_channel;
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	iter = rev_usernames.find(key);

	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];
		map<string,channel_type>::iterator channel_iter;
		channel_iter = server_channels.find(channel);
		active_usernames[username] = 1;

		if (channel_iter == server_channels.end())
		{
			//channel not found
			map<string,struct sockaddr_in> new_channel_users;
			new_channel_users[username] = sock;
			channels[channel] = new_channel_users;
			//cout << "creating new channel and joining" << endl;

			/*Add subscribe to new channel, add adj_servers as subscribed servers
			send broadcast to adj_servers*/

			map<string, struct sockaddr_in>::iterator adj_iter;
			map<string, struct sockaddr_in> server_ids;

			for(adj_iter = adj_servers.begin(); adj_iter != adj_servers.end(); adj_iter++)
			{
				server_ids[adj_iter->first] = adj_iter->second;
			}

			server_channels[channel] = server_ids;

			cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
			<< " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
			<< " recv Request join " << username << " " << channel << endl;

			char channel_p[CHANNEL_MAX];
			strcpy(channel_p, channel.c_str());
			broadcast_server_join(key, channel_p);
		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in>* existing_channel_users;
			//existing_channel_users = &channels[channel];
			//*existing_channel_users[username] = sock;

			channels[channel][username] = sock;
			//cout << "joining exisitng channel" << endl;
			/*No action required*/

			cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
			<< " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
			<< " recv Request join " << username << " " << channel << endl;
		}
	}
}
void handle_leave_message(void *data, struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes, remove user from channel
	//if not send an error message to the user


	//get message fields
	struct request_leave* msg;
	msg = (struct request_leave*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];

		map<string,channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to leave non-existent channel " << channel << endl;

		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);

			if (channel_user_iter == channels[channel].end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << "server: " << username << " trying to leave channel " << channel  << " where he/she is not a member" << endl;
			}
			else
			{
				channels[channel].erase(channel_user_iter);
				//existing_channel_users.erase(channel_user_iter);
				cout << "server: " << username << " leaves channel " << channel <<endl;

				//delete channel if no more users
				if (channels[channel].empty() && (channel != "Common"))
				{
					channels.erase(channel_iter);
					cout << "server: " << "removing empty channel " << channel <<endl;
				}

			}


		}




	}



}
void handle_say_message(void *data, struct sockaddr_in sock)
{
	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes send the message to all the members of the channel
	//if not send an error message to the user

	//get message fields
	struct request_say* msg;
	msg = (struct request_say*)data;
	string channel = msg->req_channel;
	string text = msg->req_text;
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;

	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	iter = rev_usernames.find(key);

	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		map<string,channel_type>::iterator channel_iter;
		channel_iter = channels.find(channel);
		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
			<< " " << inet_ntoa(sock.sin_addr) << (int)ntohs(sock.sin_port)
			<< " " << username << " trying to send a message to non-existent channel "
			<< channel << endl;

		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);

			if (channel_user_iter == channels[channel].end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
				<< " " << inet_ntoa(sock.sin_addr) << (int)ntohs(sock.sin_port)
				<< " " << username << " trying to send a message to channel "
				<< channel  << " where he/she is not a member" << endl;
			}
			else
			{
				cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
				<< " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
				<< " recv Request say " << username << " " << channel
				<< " \"" << text << "\"" << endl;

				map<string,struct sockaddr_in> existing_channel_users;
				existing_channel_users = channels[channel];
				for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
				{
					//cout << "key: " << iter->first << " username: " << iter->second << endl;

					ssize_t bytes;
					void *send_data;
					size_t len;

					struct text_say send_msg;
					send_msg.txt_type = TXT_SAY;

					const char* str = channel.c_str();
					strcpy(send_msg.txt_channel, str);
					str = username.c_str();
					strcpy(send_msg.txt_username, str);
					str = text.c_str();
					strcpy(send_msg.txt_text, str);
					//send_msg.txt_username, *username.c_str();
					//send_msg.txt_text,*text.c_str();
					send_data = &send_msg;

					len = sizeof send_msg;

					//cout << username <<endl;
					struct sockaddr_in dest_sock = channel_user_iter->second;


					//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, fromlen);
					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, sizeof dest_sock);

					if (bytes < 0)
					{
						perror("Message failed\n"); //error
					}
					else
					{
						//printf("Message sent\n");

					}

					//send server say
					map<string, struct sockaddr_in> server_ids;
					map<string, struct sockaddr_in>::iterator sub_iter;
					server_ids = server_channels[channel];

					FILE *ranfd;
					long uid;
					int err;
					ranfd = fopen("/dev/urandom", "r");
					if(ranfd == NULL)
					{
						return;
					}
					err = fread(&uid, sizeof(uid), 1, ranfd);
					if(err < 0)
					{
						return;
					}
					//Add unique id to message queue
					size_q++;

					uid_q[size_q%MAX_UIDS] = uid;
					fclose(ranfd);

					map<string, struct sockaddr_in> existing_channel_servers;
					existing_channel_servers = server_channels[channel];
					map<string, struct sockaddr_in>::iterator server_iter;
					for(server_iter = existing_channel_servers.begin(); server_iter != existing_channel_servers.end(); server_iter++)
					{
						ssize_t bytes;
						void *send_data;
						size_t len;

						struct server_request_say msg;
						msg.req_type = SERV_SAY;
						const char* str = channel.c_str();
						strcpy(msg.req_channel, str);
						str = username.c_str();
						strcpy(msg.req_username, str);
						str = text.c_str();
						strcpy(msg.req_text, str);
						msg.req_uid = uid;
						// cout << "say uid " << uid << endl;
						// cout << "uid size " << sizeof(uid) << endl;

						send_data = &msg;
						len = sizeof(msg);
						struct sockaddr_in dest_sock = server_iter->second;

						bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, sizeof(dest_sock));
						if(bytes < 0)
						{
							perror("Message failed\n"); //error
						}
						else
						{
							cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
							<< " " << inet_ntoa(dest_sock.sin_addr) << ":" << (int)ntohs(dest_sock.sin_port)
							<< " send S2S Request Say " << username << " " << channel
							<< " " << "\"" << text << "\"" << endl;
						}
					}
				}
			}
		}
	}
}
void handle_list_message(struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes, send a list of channels
	//if not send an error message to the user



	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		int size = channels.size();
		//cout << "size: " << size << endl;

		active_usernames[username] = 1;

		ssize_t bytes;
		void *send_data;
		size_t len;


		//struct text_list temp;
		struct text_list *send_msg = (struct text_list*)malloc(sizeof (struct text_list) + (size * sizeof(struct channel_info)));


		send_msg->txt_type = TXT_LIST;

		send_msg->txt_nchannels = size;


		map<string,channel_type>::iterator channel_iter;



		//struct channel_info current_channels[size];
		//send_msg.txt_channels = new struct channel_info[size];
		int pos = 0;

		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			string current_channel = channel_iter->first;
			const char* str = current_channel.c_str();
			//strcpy(current_channels[pos].ch_channel, str);
			//cout << "channel " << str <<endl;
			strcpy(((send_msg->txt_channels)+pos)->ch_channel, str);
			//strcpy(((send_msg->txt_channels)+pos)->ch_channel, "hello");
			//cout << ((send_msg->txt_channels)+pos)->ch_channel << endl;

			pos++;

		}



		//send_msg.txt_channels =
		//send_msg.txt_channels = current_channels;
		send_data = send_msg;
		len = sizeof (struct text_list) + (size * sizeof(struct channel_info));

					//cout << username <<endl;
		struct sockaddr_in dest_sock = sock;


		//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, fromlen);
		bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, sizeof dest_sock);

		if (bytes < 0)
		{
			perror("Message failed\n"); //error
		}
		else
		{
			//printf("Message sent\n");

		}

		cout << "server: " << username << " lists channels"<<endl;


	}



}
void handle_who_message(void *data, struct sockaddr_in sock)
{


	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//if yes, send user list in the channel
	//if not send an error message to the user


	//get message fields
	struct request_who* msg;
	msg = (struct request_who*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];

		active_usernames[username] = 1;

		map<string,channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to list users in non-existing channel " << channel << endl;

		}
		else
		{
			//channel exits
			map<string,struct sockaddr_in> existing_channel_users;
			existing_channel_users = channels[channel];
			int size = existing_channel_users.size();

			ssize_t bytes;
			void *send_data;
			size_t len;


			//struct text_list temp;
			struct text_who *send_msg = (struct text_who*)malloc(sizeof (struct text_who) + (size * sizeof(struct user_info)));


			send_msg->txt_type = TXT_WHO;

			send_msg->txt_nusernames = size;

			const char* str = channel.c_str();

			strcpy(send_msg->txt_channel, str);



			map<string,struct sockaddr_in>::iterator channel_user_iter;

			int pos = 0;

			for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
			{
				string username = channel_user_iter->first;

				str = username.c_str();

				strcpy(((send_msg->txt_users)+pos)->us_username, str);


				pos++;



			}

			send_data = send_msg;
			len = sizeof(struct text_who) + (size * sizeof(struct user_info));

						//cout << username <<endl;
			struct sockaddr_in dest_sock = sock;


			//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, fromlen);
			bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, sizeof dest_sock);

			if (bytes < 0)
			{
				perror("Message failed\n"); //error
			}
			else
			{
				//printf("Message sent\n");

			}

			cout << "server: " << username << " lists users in channnel "<< channel << endl;




			}




	}




}
void send_error_message(struct sockaddr_in sock, string error_msg)
{
	ssize_t bytes;
	void *send_data;
	size_t len;

	struct text_error send_msg;
	send_msg.txt_type = TXT_ERROR;

	const char* str = error_msg.c_str();
	strcpy(send_msg.txt_error, str);

	send_data = &send_msg;

	len = sizeof send_msg;


	struct sockaddr_in dest_sock = sock;



	bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, sizeof dest_sock);

	if (bytes < 0)
	{
		perror("Message failed\n"); //error
	}
	else
	{
		//printf("Message sent\n");

	}





}

void server_join_message(void *data, struct sockaddr_in sock)
{
	struct server_request_join *msg;
	msg= (struct server_request_join*)data;
	char channel[CHANNEL_MAX];
	strcpy(channel, msg->req_channel);

	map<string, map<string, struct sockaddr_in>>::iterator sub_iter;
	sub_iter = server_channels.find(channel);

	string sender_id;
	string ip = inet_ntoa(sock.sin_addr);
	int port = (int)ntohs(sock.sin_port);
	char port_str[6];
	sprintf(port_str, "%d", port);
	sender_id = ip + "." + port_str;

	if(sub_iter == server_channels.end())
	{
		cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
		<< " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
		<< " recv S2S Join " << channel << endl;

		map<string, struct sockaddr_in>::iterator adj_iter;
		map<string, struct sockaddr_in> server_ids;
		for(adj_iter = adj_servers.begin(); adj_iter != adj_servers.end(); adj_iter++)
		{
			server_ids[adj_iter->first] = adj_iter->second;

			map<string, pair<string, time_t> >::iterator timer_iter;
			timer_iter = server_timers.find(sender_id);
			if (timer_iter == server_timers.end())
			{
					//timer not found
					time_t new_time;
					time(&new_time);
					pair<string, time_t> channel_time_pair;
					channel_time_pair.first = channel;
					memcpy(&channel_time_pair.second, &new_time, sizeof new_time);
					server_timers[sender_id] = channel_time_pair;
			}
			else
			{
					//timer found, reset it.
					time(&((timer_iter->second).second));
			}
		}
		server_channels[channel] = server_ids;
		broadcast_server_join(sender_id, channel);

	}
	else
	{
		//update timer(s)
		map<string, struct sockaddr_in>::iterator server_iter;
		for (server_iter = adj_servers.begin(); server_iter != adj_servers.end(); server_iter++)
		{
				if (sender_id == server_iter->first)
				{
						map<string, pair<string, time_t> >::iterator timer_iter;
						timer_iter = server_timers.find(sender_id);
						if (timer_iter == server_timers.end())
						{
								//cout << "Timer for this server/channel pair was not found.  Creating it." << endl;
								//timer not found
								time_t new_time;
								time(&new_time);
								pair<string, time_t> channel_time_pair;
								channel_time_pair.first = channel;
								memcpy(&channel_time_pair.second,  &new_time, sizeof new_time);
								server_timers[sender_id] = channel_time_pair;
								(server_channels[channel])[server_iter->first] = server_iter->second;
						}
						else
						{
								//cout << "Refreshing timer for server:" << origin_server_key << " at channel:" << channel << endl;
								//timer found, reset it.
								time(&((timer_iter->second).second));
						}
				}
				else
				{
						//add this server as a subscriber to channel
						//(server_channels[channel])[server_iter->first] = server_iter->second;
				}
		}

		cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
		<< " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
		<< " recv S2S Join " << channel << endl;
	}
}
void server_leave_message(void *data, struct sockaddr_in sock)
{
	struct server_request_leave *msg;
	msg = (struct server_request_leave*)data;
	string channel = msg->req_channel;
	string ip = inet_ntoa(sock.sin_addr);
	int port = ntohs(sock.sin_port);
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;

	map<string,struct sockaddr_in>::iterator server_iter;

	server_iter = server_channels[channel].find(key);
	if (server_iter != server_channels[channel].end())
	{
		server_channels[channel].erase(server_iter);
  }

  cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
  << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
  << " recv S2S Leave " << channel << endl;
}
void server_say_message(void *data, struct sockaddr_in sock)
{
	struct server_request_say *msg;
	msg = (struct server_request_say*)data;
	char username[USERNAME_MAX];
	char channel[CHANNEL_MAX];
	char text[SAY_MAX];
	long uid;

	strcpy(username, msg->req_username);
	strcpy(channel, msg->req_channel);
	strcpy(text, msg->req_text);
	uid = msg->req_uid;

	// cout << "username channel uid: " << username << " "
	// << channel << " " << uid << endl;
	// cout << "text: \"" << text << "\"" << endl;
	string sender_id;
	char port_str[6];
	string ip = inet_ntoa(sock.sin_addr);
	int port = (int)ntohs(sock.sin_port);
	sprintf(port_str, "%d", port);
	sender_id = ip + "." + port_str;

	cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
	<< " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
	<< " recv S2S Say "<< username << " " << channel << " \"" << text << "\"" << endl;

	//check uid 1 = true & 0 = false
	int is_new = 1;
	for(int i = 0; i < MAX_UIDS; i++)
	{
		if(uid_q[i] == uid)
		{
			is_new = 0;
			break;
		}
	}

	if(is_new == 1)
	{
		size_q++;
		uid_q[size_q%MAX_UIDS];

		map<string, struct sockaddr_in>::iterator channel_user_iter;
		map<string, struct sockaddr_in> existing_channel_users;
		existing_channel_users = channels[channel];

		for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
		{
			ssize_t bytes;
			void *send_data;
			size_t len;
			struct text_say msg;
			msg.txt_type = TXT_SAY;
			strcpy(msg.txt_channel, channel);
			strcpy(msg.txt_username, username);
			strcpy(msg.txt_text, text);
			send_data = &msg;
			len = sizeof msg;
			struct sockaddr_in dest_sock = channel_user_iter->second;

			bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, sizeof dest_sock);

			if (bytes < 0)
			{
					perror("Message failed"); //error
			}
			//handle_say_message(data, sock);


		}
		map<string, struct sockaddr_in>::iterator server_iter;
		map<string, struct sockaddr_in> subscribed_channels;
		subscribed_channels = server_channels[channel];

		int server_subscribers = 0;
    for (server_iter = server_channels[channel].begin(); server_iter != server_channels[channel].end(); server_iter++)
    {
        if (sender_id != server_iter->first) {
            server_subscribers++;
        }
    }
		if(server_subscribers > 0)
		{
			for(server_iter = subscribed_channels.begin(); server_iter != subscribed_channels.end(); server_iter++)
			{
				if(server_iter->first != sender_id)
				{
					ssize_t bytes;
					void *send_data;
					size_t len;
					struct server_request_say msg;
					msg.req_type = SERV_SAY;
					strcpy(msg.req_channel, channel);
					strcpy(msg.req_username, username);
					strcpy(msg.req_text, text);
					msg.req_uid = uid;
					send_data = &msg;
					len = sizeof msg;
					struct sockaddr_in dest_sock = server_iter->second;

					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&dest_sock, sizeof dest_sock);

					if (bytes < 0)
					{
						perror("Message failed"); //error
					}
					cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
					<< " " << inet_ntoa(dest_sock.sin_addr) << ":" << (int)ntohs(dest_sock.sin_port)
					<< " send S2S Say "<< username << " " << channel << " \"" << text << "\"" << endl;
				}

			}
		}
		else
		{
			if (channels[channel].empty() && (channel != "Common"))
			{
					//cout << "Channel is empty on this server" << endl;
					ssize_t bytes;
					size_t len;
					void *send_data;
					struct server_request_leave s2s_leave;
					s2s_leave.req_type = SERV_LEAVE;
					strcpy(s2s_leave.req_channel, channel);
					send_data = &s2s_leave;
					len = sizeof s2s_leave;
					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&sock, sizeof sock);
					if (bytes < 0)
					{
							perror("Message failed");
					}
					else
					{
					cout << inet_ntoa(server.sin_addr) << ":" << (int)htons(server.sin_port)
						 << " " << inet_ntoa(sock.sin_addr) <<":"<< (int)htons(sock.sin_port)
						 << " send S2S Leave " << channel << endl;
					}
			}
		}
	}
	else
	{
		cout << sender_id << " Duplicate uid" << endl;
	  map<string,struct sockaddr_in>::iterator server_iter;
		for (server_iter = server_channels[channel].begin(); server_iter != server_channels[channel].end(); server_iter++)
		{
			if(sender_id == server_iter->first)
			{
				//send leave back to sender
				ssize_t bytes;
				size_t len;
				void *send_data;
				struct server_request_leave s2s_leave;

				s2s_leave.req_type = SERV_LEAVE;
				strcpy(s2s_leave.req_channel, channel);
				len = sizeof s2s_leave;
				bytes = sendto(s, send_data, len, 0, (struct sockaddr*)(&server_iter->second), sizeof(server_iter->second));
				if (bytes < 0)
				{
						perror("Message failed");
				}
				else
				{
						cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
								<< " " << inet_ntoa((server_iter->second).sin_addr) << ":"
								<< (int)ntohs((server_iter->second).sin_port)
								<< " send S2S Leave " << channel << endl;
				}
				break;
			}
		}
	}

}
void broadcast_server_join(string sender_id, char *channel)
{
	ssize_t bytes;
	void *send_data;
	size_t len;

	map<string, struct sockaddr_in> :: iterator adj_iter;
	struct server_request_join msg;

	msg.req_type = SERV_JOIN;
	strcpy(msg.req_channel, channel);
	send_data = &msg;
	len = sizeof msg;

	//Broadcast to all adjacent servers
	for(adj_iter = adj_servers.begin(); adj_iter != adj_servers.end(); adj_iter++)
	{
		if(adj_iter->first == sender_id)
		{
			cout << "Don't broadcast backwards to sender" << endl;
		}
		else
		{
			bytes = sendto(s, send_data, len, 0,
				 (struct sockaddr*)&adj_iter->second, sizeof(adj_iter->second));
			if(bytes < 0)
			{
				perror("Message failed\n"); //error
			}
			else
			{
				cout << inet_ntoa(server.sin_addr) << ":"
				 << (int)ntohs(server.sin_port)
				 << " " << inet_ntoa(adj_iter->second.sin_addr) << ":"
				 << (int)ntohs(adj_iter->second.sin_port)
				 << " send S2S Request Join " << channel << endl;
			}
		}
	}

}
