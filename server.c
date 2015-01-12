#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <signal.h>

#include <mysql.h>

MYSQL *conn;

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
		fprintf(stderr, "%s\n", mysql_error(conn));
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
      fprintf(stderr, "%s\n", mysql_error(conn));
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
      return -1;
   }
   return 1;
}

void say_hello(int fd, short event, void *arg)
{
  printf("Hello\n");
}

int main(int argc, char ** argv) {

	mysql_connect();

	int welcomeSocket, newSocket;
	char buffer[1024];
	struct sockaddr_in serverAddr, clientAddr;
	struct sockaddr_storage serverStorage;
	socklen_t addr_size;

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
	if(listen(welcomeSocket,5)==0)
		printf("Listening\n");
	else
		printf("Error\n");

	/*---- Accept call creates a new socket for the incoming connection ----*/
	addr_size = sizeof serverStorage;

	while(1){
		newSocket = accept(welcomeSocket, (struct sockaddr *) &clientAddr, &addr_size);
		if(fork() == 0){
			char addr[15];
			printf("Connected %s with pid %d\n", inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size), (int) getpid());

			//add to db 
			int id = mysql_add_sensor(addr);
			printf("Connection added to database with id %d\n", id);
			
			//ping loop
			int ping_pid;
			if((ping_pid = fork()) == 0){
				//call initial ping 
				char * command = "../tools/ping %d %d %s &";
				char cmd[50];
				sprintf(cmd, command, id, 5, addr);
				while(1){
					printf("Starting pingn\n");
					system(cmd);
					sleep(60);
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
					bytes = recv(newSocket,buffer,13,0);
					if(strcmp(buffer, "Heartbeat") == 0){
						printf("Heartbeat from %s %d - %s\n", inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size), (int) getpid(), buffer);
					}else{
						printf("XXX\n");
					}
					//printf("%d\n", strcmp("Heartbeat", buffer));	
				}else{
					printf("Timeout\n");
					break;
				}
					
			}
			printf("Connection %s %d closed\n", inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size), (int) getpid());
			printf("Removing sensor with id: %d from database \n", id);
			
			//mark as disconnected in db 
			mysql_remove_sensor(id);

			//close comm socket
			close(newSocket);

			//kill ping loop (process)
			kill(ping_pid, SIGKILL);

			//kill comm (process)
			exit(0);
		}
	}

	close(welcomeSocket);

	return 0;
}