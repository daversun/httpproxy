#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>
#include <iostream>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sys/wait.h>
#include <poll.h>
#include <set>
#include <map>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#define MAX_SIZE 1024

struct HostInfo{
    int status;		//get host information success or errno
    char type[16];	//CONNECT GET OR POST
    char host[32];	//HOST, eg: baidu.com
    char port[8];	//80 or other
};


/*
 * data: the data to parse
 * host_info: store the result we parse
 */
void parse(char* data, struct HostInfo* host_info){
    std::string s = data;
    std::string::size_type pos = s.find("Host");
    bool flag = false;
    int type_index = 0;

    if(pos == std::string::npos){
        host_info->status = 0;
		return;
    }else{
        while(data[pos++] != ' ');

        int index = 0, port_index = 0;
        while(data[pos] != '\r'){
			
			if(data[pos] == ':')flag = true;
			else if(flag && '0' <= data[pos] && data[pos] <= '9')host_info->port[port_index++] = data[pos];
			else if(!flag)host_info->host[index++] = data[pos];
			pos++; 
		}

		host_info->host[index] = 0;
		host_info->port[port_index] = 0;

		if(!flag){
			strcpy(host_info->port, "80");
		}
        
    }

	pos = 0;
	while(data[pos] != ' '){
		host_info->type[type_index++] = data[pos++];
	}

	host_info->type[type_index] = 0;

	host_info->status = 1;
}

/*
 * data: according the given data to create a connection
 * return: connection
 */
int createConnectionForServer(char* data){

	struct HostInfo host_info;
	int socket_fd;
	struct sockaddr_in srv;
	parse(data, &host_info);
	

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&srv, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_port   = htons(atoi(host_info.port));
	struct hostent* host_name = gethostbyname(host_info.host);
	srv.sin_addr = *(struct in_addr*)(host_name->h_addr_list[0]);

	
	if(connect(socket_fd, (struct sockaddr*)&srv, sizeof(srv))  < 0){
		perror("connect");
		exit(0);
	}


	return socket_fd;

}

/*
 * solving zombie processes to avoid waste the resource of os
 */
void sigchild(int signo){
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

void proxy(){
	struct HostInfo target_host;
	int socket_fd, conn_fd;
	int opt = 1;

	socklen_t addrlen;
	sockaddr_in addr, clientaddr;
	char return_data[] = "HTTP/1.1 200 Connection Established\r\n\r\n";

	signal(SIGCHLD, sigchild);

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	bzero(&addr, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_port   = htons(9999);
	addr.sin_addr.s_addr = INADDR_ANY;
	
	//set port can be reused
	if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
		perror("setsockopt");
		exit(0);
	}

	//make socket_fd and addr binding
	if(bind(socket_fd, (sockaddr*)&addr, sizeof(addr)) < 0){
		perror("bind");
		exit(0);
	}

	//listen the connection request
	listen(socket_fd, 64);
	
	
	while(true){
		//get a connection from queue
		conn_fd = accept(socket_fd, NULL, NULL);
		//create a processror to solve this request
		if(fork() == 0){
			close(socket_fd);
			
			fd_set pre, rs;
			int srv_fd = -1, len, target_fd = -1, max_fd;
			struct timeval time_out= {10, 0};
			char data[MAX_SIZE];
			HostInfo host;

			len = read(conn_fd, data, MAX_SIZE);
			
	
			target_fd = createConnectionForServer(data);
		
			parse(data, &host);
			
			/*
 				note here very important
				if you find is CONNECT header, write data to client or write the data to server
 			*/
			if(!strcmp("CONNECT", host.type))write(conn_fd, return_data, strlen(return_data));
			else write(target_fd, data, len);

			FD_ZERO(&pre);
			FD_SET(conn_fd, &pre);
			FD_SET(target_fd, &pre);
			max_fd = conn_fd;


			rs = pre;
			while(select(max_fd + 1, &rs, NULL, NULL, &time_out) > 0){
				
		
				if(FD_ISSET(target_fd, &rs)){
					len = read(target_fd, data, MAX_SIZE);
					
					if(len <= 0)break;
					write(conn_fd, data, len);
					
				}

				if(FD_ISSET(conn_fd, &rs)){
					len = read(conn_fd, data, MAX_SIZE);
				
					if(len <= 0)break;
					write(target_fd, data, len);
					
					
				}
				rs = pre;
			}

			close(target_fd);
			close(conn_fd);
			exit(0);
		}

		close(conn_fd);
	}
}

int main(int argc, char** argv){
	
	proxy();
	return 0;
}
