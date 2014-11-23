#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>

#include <sys/time.h>

#include <mysql.h>

MYSQL *conn;

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
	char * query = "insert into sensors(ip, active, start, end) values('%s', true, '%d-%d-%d %d:%d:%d', '2013-01-01 20:20:20')\n"
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	sprintf(buff, query, ip, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	if (mysql_query(conn, "select * from sensors")) {
      fprintf(stderr, "%s\n", mysql_error(conn));
      return -1;
   }
	
	return mysql_insert_id(conn);
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
			close(newSocket);
		}
	}

	close(welcomeSocket);

	return 0;
}