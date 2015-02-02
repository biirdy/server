#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>

#include <syslog.h>
#include <unistd.h>
#include <signal.h>

#include <mysql.h>

#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "srrp.h"

MYSQL *conn;
FILE* logs;
int deamon = 0;

void server_log(const char * type, const char * fmt, ...){
	//format message
	va_list args; 
	va_start(args, fmt);

	char msg[100];
	vsprintf(msg, fmt, args);

	va_end(args);

	//get timestamp
	time_t ltime;
	struct tm result;
	char stime[32];
	ltime = time(NULL);
	localtime_r(&ltime, &result);
	asctime_r(&result, stime);
	strtok(stime, "\n");			

	fprintf(logs, "%s - Server - [%s] - %s\n", stime, type, msg);		//write to log
	fflush(logs);

	if(!deamon)
		printf("%s - %s\n", type, msg);
}

static void deamonise(){
    pid_t pid;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0){
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);
    

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    chdir("/");

    deamon = 1;

    server_log("Info", "Deamonised");
}

void closeLog(int sig){
	server_log("Info", "Server stopped");
    fclose(logs);
    exit(1);
}

char * iptos (uint32_t ipaddr) {
    static char ips[16];
   
    sprintf(ips, "%d.%d.%d.%d",
        (ipaddr >> 24),
        (ipaddr >> 16) & 0xff,
        (ipaddr >>  8) & 0xff,
        (ipaddr      ) & 0xff );
    return ips;
}

int mysql_connect(){
	
	MYSQL_RES *res;
	MYSQL_ROW row;
	char *server = "localhost";
	char *user = "root";
	char *password = "root";
	char *database = "tnp";
	conn = mysql_init(NULL);
	/* Connect to database */
	if (!mysql_real_connect(conn, server,
		user, password, database, 0, NULL, 0)) {
		server_log("Error", "Database Connection - %s", mysql_error(conn));
		return 0;
	}
}

/*
* Returns the id
*/
int mysql_add_sensor(char * ip){
	char buff[200];
	char * query = "insert into sensors(ip, active, start) values('%s', true, FROM_UNIXTIME(%d))\n";

	sprintf(buff, query, ip, time(NULL));

	if (mysql_query(conn, buff)) {
      server_log("Error", "Database adding sensor - %s", mysql_error(conn));
      return -1;
   	}
	
	return mysql_insert_id(conn);
}

int mysql_remove_sensor(int id){
	char buff[200];
	char * query = "update sensors set active=false, end=FROM_UNIXTIME(%d) where sensor_id=%d";

	sprintf(buff, query, time(NULL), id);

	if (mysql_query(conn, buff)) {
      fprintf(stderr, "%s\n", mysql_error(conn));
      server_log("Error", "Database removing sensor - %s", mysql_error(conn));
      return -1;
   }
   return 1;
}

int mysql_add_bw(int sensor_id, int bandwidth, int duration, int bytes){
	char buff[200];
	char * query = "insert into bw(sensor_id, bytes, duration, speed, time) values(%d, %d, %d, %d, FROM_UNIXTIME(%d))\n";

	sprintf(buff, query, sensor_id, bytes, duration, bandwidth, time(NULL));

	if(mysql_query(conn, buff)){
		server_log("Error", "Database adding bandwidth - %s", mysql_error(conn));
		return -1;
	}
	return 1;
}

int main(int argc, char ** argv) {

	//signal handler to close log file
	signal(SIGINT, closeLog);
	signal(SIGTERM, closeLog);

	//log files
	logs = fopen("/var/log/tnp/server.log", "a+");

	server_log("Info" , "Server Started");

	if(argc == 3)
		deamonise();

	//connect to database
	mysql_connect();

	int welcomeSocket, newSocket;
	char buffer[1024];
	char recv_buff[1024];
	char send_buff[1024];
	struct sockaddr_in serverAddr, clientAddr;
	struct sockaddr_storage serverStorage;
	socklen_t addr_size;

	addr_size = sizeof serverStorage;

	/*---- Create the socket. The three arguments are: ----*/
	/* 1) Internet domain 2) Stream socket 3) Default protocol (TCP in this case) */
	welcomeSocket = socket(PF_INET, SOCK_STREAM, 0);

	/*---- Configure settings of the server address struct ----*/
	/* Address family = Internet */
	serverAddr.sin_family = AF_INET;
	/* Set port number, using htons function to use proper byte order */
	serverAddr.sin_port = htons(7891);
	/* Set IP address to localhost */
	serverAddr.sin_addr.s_addr = inet_addr(argv[1]);
	/* Set all bits of the padding field to 0 */
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);  

	/*---- Bind the address struct to the socket ----*/
	bind(welcomeSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));

	/*---- Listen on the socket, with 5 max connection requests queued ----*/
	if(listen(welcomeSocket,5)==0){
		server_log("Info" , "Listening on %s", argv[1]);
	}else{
		server_log("Error" , "Failed to listening on %s", argv[1]);
	}

	/*---- Accept call creates a new socket for the incoming connection ----*/

	while(1){
		newSocket = accept(welcomeSocket, (struct sockaddr *) &clientAddr, &addr_size);
		if(fork() == 0){
			char addr[15];
			//printf("Connected %s with pid %d\n", inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size), (int) getpid());
			inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size);

			//add to db 
			int id = mysql_add_sensor(addr);
			server_log("Info" , "Sensor %s connected with id %d", inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size), id);
			
			//heartbeat loop
			int hb_pid;
			if((hb_pid = fork()) == 0){
				//build request
				struct srrp_request * hb_request;
				hb_request = (struct srrp_request *) send_buff;
				hb_request->id = 1;
				hb_request->type = SRRP_HB;

				while(1){
					printf("Sending hb request\n");
					send(newSocket, send_buff, sizeof(send_buff), 0);
					sleep(1);			//second
				}
			}

			//ping loop
			int ping_pid;
			if((ping_pid = fork()) == 0){
				//call initial ping 
				char * command = "/home/ubuntu/tools/ping %d %d &";
				char cmd[50];
				sprintf(cmd, command, id, 5);
				while(1){
					server_log("Info", "Executing command %s", cmd);
					system(cmd);
					sleep(60);			//minute
				}
			}

			//iperf loop
			int iperf_pid;
			if((iperf_pid = fork()) == 0){
				//initial sleep of 30 seconds
				sleep(30);	

				//build the request
				struct srrp_request * tp_request;
				tp_request = (struct srrp_request *) send_buff;
				tp_request->id = 99;
				tp_request->type = SRRP_BW;
				tp_request->length = 1;

				struct srrp_param size;
				size.param = SRRP_SIZE;
				size.value = 10;
				tp_request->params[0] = size;

				while(10){
					printf("Sending iperf request\n");
					server_log("Info", "Sending iperf request to sensor %d", id);
					send(newSocket, send_buff, sizeof(send_buff), 0);
					sleep(300);		//5 minutes
				}
			}

			struct timeval tv;

			int bytes = 1;
			
			while(bytes){
				fd_set rfds;
				FD_ZERO(&rfds);
				FD_SET (newSocket, &rfds);

				tv.tv_sec = 5;
				tv.tv_usec = 0;
				
				int ready = select(newSocket + 1, &rfds, NULL, NULL, &tv);
				
				if(ready == -1){
					perror("select()\n");
				}else if(ready){
					bytes = recv(newSocket,recv_buff, sizeof(recv_buff),0);

					struct srrp_response * response;
					response = (struct srrp_response *) recv_buff;
					//response->id 		= 10;
					
					if(response->id == 0){
						printf("Received hb response\n");
					}else if(response->id == 99){
						printf("Recevied ieprf response\n");
						server_log("Info", "Recevied iperf response from sensor %d", id);

						int i;
						int bandwidth = 0;
						int duration = 0;
						int size = 0;
						for(i = 0; i < response->length ; i++){
							if(response->results[i].result == SRRP_RES_DUR){
								duration = response->results[i].value;
								printf("iperf duration = %d\n", response->results[i].value);
							}else if(response->results[i].result == SRRP_RES_SIZE){
								size = response->results[i].value;
								printf("iperf size = %d\n", response->results[i].value);
							}else if(response->results[i].result == SRRP_RES_BW){
								bandwidth = response->results[i].value;
								printf("iperf speed = %d\n", response->results[i].value);
							}else{
								printf("Unrecoginsed result type for iperf test");
							}
						}

						//if all the results - add to db
						if(bandwidth && duration && size){
							mysql_add_bw(id, bandwidth, duration, size);
						}else{
							server_log("Error", "Response missing iperf results from sensor %d", id);
						}

					}else{
						printf("Uncognised data type=%d\n", response->id);
					}

				}else{
					server_log("Info", "Sensor %s timedout", inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size));
					break;
				}
					
			}
			
			server_log("Info", "Sensor %s discconected", inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size));
			
			//mark as disconnected in db 
			mysql_remove_sensor(id);

			//close comm socket
			close(newSocket);

			//kill ping loop (process)
			kill(ping_pid, SIGKILL);

			//kill hb loop (process)
			kill(hb_pid, SIGKILL);

			kill(iperf_pid, SIGKILL);

			//kill comm (process)
			exit(0);
		}
	}

	close(welcomeSocket);

	fclose(logs);
	closelog();

	return 0;
}