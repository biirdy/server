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

#include <sys/mman.h>   //not needed

#include <pthread.h>

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/server.h>
#include <xmlrpc-c/server_abyss.h>

#include "srrp.h"
#include "ini/ini.h"
#include "ini/ini.c"


MYSQL *conn;
FILE* logs;
int deamon = 0;
char recv_buff[1024];
char send_buff[1024];

/*
* Config struct to be loaded in 
*/
typedef struct{
    //rcp
    int rcp_port;

    //database
    const char* mysql_addr;
    const char* mysql_usr;
    const char* mysql_pass;

    //server
    const char* server_addr;
    int server_port;
    int server_timeout;

    //iperf
    int tcp_iperf_port;
    int udp_iperf_port;
} configuration;
configuration config;


static int handler(void* user, const char* section, const char* name, const char* value){
    configuration* pconfig = (configuration*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("rcp", "rcp_port")) {
        pconfig->rcp_port = atoi(value);
    } else if (MATCH("database", "mysql_addr")) {
        pconfig->mysql_addr = strdup(value);
    } else if (MATCH("database", "mysql_usr")) {
        pconfig->mysql_usr = strdup(value);
    } else if (MATCH("database", "mysql_pass")) {
        pconfig->mysql_pass = strdup(value);
    } else if (MATCH("server", "server_addr")) {
        pconfig->server_addr = strdup(value);
    } else if (MATCH("server", "server_port")) {
        pconfig->server_port = atoi(value);
    } else if (MATCH("server", "server_timeout")) {
        pconfig->server_timeout = atoi(value);
    } else if (MATCH("iperf", "tcp_iperf_port")) {
        pconfig->tcp_iperf_port = atoi(value);
    } else if (MATCH("iperf", "udp_iperf_port")) {
        pconfig->udp_iperf_port = atoi(value);
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

/*
* Dictionary to store id to socket 
* http://stackoverflow.com/questions/4384359/quick-way-to-implement-dictionary-in-c
*/
struct nlist { /* table entry: */
    struct nlist *next; /* next entry in chain */
    int id;     /* defined name */
    int socket; /* replacement text */
};

#define HASHSIZE 101

static struct nlist * hashtab[HASHSIZE];// = mmap(NULL, sizeof(struct nlist *), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);; /* pointer table */

/* hash: form hash value for string s */
unsigned hash(int id){
    return id % HASHSIZE;
}

/* lookup: look for s in hashtab */
struct nlist *lookup(int id){
    struct nlist *np;

    for (np = hashtab[hash(id)]; np != NULL; np = np->next){
        if (id == np->id){
            return np; /* found */
        }
    }

    return NULL; /* not found */
}

/* install: put (name, defn) in hashtab */
struct nlist *install(int id, int socket){
    struct nlist *np;
    unsigned hashval;
    if ((np = lookup(id)) == NULL) { /* not found */
        np = (struct nlist *) malloc(sizeof(*np));
        //np = mmap(NULL, sizeof(*np), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        np->id = id;
        hashval = hash(id);
        np->next = hashtab[hashval];
        hashtab[hashval] = np;

        printf("Installing socket with hashval %d\n", hash(id));
    }
    //hashtab[hash(id)]->id = id;
    hashtab[hash(id)]->socket = socket;
    //return np;
}

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

    fprintf(logs, "%s - Server - [%s] - %s\n", stime, type, msg);       //write to log
    fflush(logs);

    // if not deamon - print logs
    if(!deamon)
        printf("%s - %s\n", type, msg);
}

static xmlrpc_value * iperf_request(xmlrpc_env *   const envP,
           xmlrpc_value * const paramArrayP,
           void *         const serverInfo,
           void *         const channelInfo) {

    xmlrpc_int32 id, length;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(ii)", &id, &length);
    if (envP->fault_occurred){
        server_log("Error", "Could not parse iperf request argument array");
        return NULL;
    }

    //create request
    struct srrp_request * tp_request;
    tp_request = (struct srrp_request *) send_buff;
    tp_request->id = 99;
    tp_request->type = SRRP_BW;
    tp_request->length = 1;

    struct srrp_param dur;
    dur.param = SRRP_DUR;
    dur.value = (int) length;
    tp_request->params[0] = dur;

    //get socket
    struct nlist * sck = lookup((int) id);
    if(sck != NULL){
        send(sck->socket, send_buff, sizeof(send_buff), 0);
        server_log("Info", "RPC sending iperf request to sensor %d", id);
        return xmlrpc_build_value(envP, "i", (xmlrpc_int32) 0);
    }else{
        server_log("Error", "RCP no reference to socket for id %d", id);
        return xmlrpc_build_value(envP, "i", (xmlrpc_int32) 1);
    }
}

static xmlrpc_value * ping_request( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 id, iterations;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(ii)", &id, &iterations);
    if (envP->fault_occurred){
        server_log("Error", "Could not parse ping request argument array");
        return NULL;
    }

    //create request
    struct srrp_request * rtt_request;
    rtt_request = (struct srrp_request *) send_buff;
    rtt_request-> id = 55;
    rtt_request-> type = SRRP_RTT;
    rtt_request-> length = 1;

    struct srrp_param ittr;
    ittr.param = SRRP_ITTR;
    ittr.value = (int) iterations;
    rtt_request->params[0] = ittr;

    //get socket
    struct nlist * sck = lookup((int) id);
    if(sck != NULL){
        send(sck->socket, send_buff, sizeof(send_buff), 0);
        server_log("Info", "RPC sending ping request to sensor %d", id);
        return xmlrpc_build_value(envP, "i", (xmlrpc_int32) 0);
    }else{
        server_log("Error", "RCP no reference to socket for id %d", id);
        return xmlrpc_build_value(envP, "i", (xmlrpc_int32) 1);
    }
}

static xmlrpc_value * udp_request(  xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 id, speed, size, dur, dscp;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiiii)", &id, &speed, &size, &dur, &dscp);
    if (envP->fault_occurred){
        server_log("Error", "Could not parse udp request argument array");
        return NULL;
    }

    //create request
    struct srrp_request * udp_request;
    udp_request = (struct srrp_request *) send_buff;
    udp_request-> id = 22;
    udp_request-> type = SRRP_UDP;
    udp_request-> length = 4;

    struct srrp_param send_speed;
    send_speed.param = SRRP_SPEED;
    send_speed.value = (int) speed;
    udp_request->params[0] = send_speed;

    struct srrp_param dgram_size;
    dgram_size.param = SRRP_SIZE;
    dgram_size.value = (int) size;
    udp_request->params[1] = dgram_size; 

    struct srrp_param duration;
    duration.param = SRRP_DUR;
    duration.value = (int) dur;
    udp_request->params[2] = duration;

    struct srrp_param qos;
    qos.param = SRRP_DSCP;
    qos.value = (int) dscp;
    udp_request->params[3] = qos;

    //get socket
    struct nlist * sck = lookup((int) id);
    if(sck != NULL){
        send(sck->socket, send_buff, sizeof(send_buff), 0);
        server_log("Info", "RPC sending udp request to sensor %d", id);
        return xmlrpc_build_value(envP, "i", (xmlrpc_int32) 0);
    }else{
        server_log("Error", "RCP no reference to socket for id %d", id);
        return xmlrpc_build_value(envP, "i", (xmlrpc_int32) 1);
    }
}

static xmlrpc_value * dns_request(  xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 id;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(i)", &id);
    if (envP->fault_occurred){
        server_log("Error", "Could not parse udp request argument array");
        return NULL;
    }

    //create request
    struct srrp_request * dns_request;
    dns_request = (struct srrp_request *) send_buff;
    dns_request-> id = 11;
    dns_request-> type = SRRP_DNS;
    dns_request-> length = 0;

    //get socket
    struct nlist * sck = lookup((int) id);
    if(sck != NULL){
        send(sck->socket, send_buff, sizeof(send_buff), 0);
        server_log("Info", "RPC sending dns request to sensor %d", id);
        return xmlrpc_build_value(envP, "i", (xmlrpc_int32) 0);
    }else{
        server_log("Error", "RCP no reference to socket for id %d", id);
        return xmlrpc_build_value(envP, "i", (xmlrpc_int32) 1);
    }
}

/*
* Start RPC server
*/
void * rpc_server(void * arg){
    struct xmlrpc_method_info3 const ieprf_method = {
        /* .methodName     = */ "iperf.request",
        /* .methodFunction = */ &iperf_request,
    };
    struct xmlrpc_method_info3 const ping_method = {
        /* .methodName     = */ "ping.request",
        /* .methodFunction = */ &ping_request,
    };
    struct xmlrpc_method_info3 const udp_method = {
        /* .methodName     = */ "udp.request",
        /* .methodFunction = */ &udp_request,
    };
    struct xmlrpc_method_info3 const dns_method = {
        /* .methodName     = */ "dns.request",
        /* .methodFunction = */ &dns_request,
    };
    xmlrpc_server_abyss_parms serverparm;
    xmlrpc_registry * registryP;
    xmlrpc_env env;
    xmlrpc_server_abyss_t * serverP;
    xmlrpc_server_abyss_sig * oldHandlersP;

    xmlrpc_env_init(&env);

    xmlrpc_server_abyss_global_init(&env);

    registryP = xmlrpc_registry_new(&env);
    if (env.fault_occurred) {
        server_log("Error", "xmlrpc_registry_new() failed.  %s", env.fault_string);
        exit(1);
    }

    //add methods
    xmlrpc_registry_add_method3(&env, registryP, &ieprf_method);
    if (env.fault_occurred) {
        server_log("Error", "xmlrpc_registry_add_method3() iperf_method failed.  %s", env.fault_string);
        exit(1);
    }
    xmlrpc_registry_add_method3(&env, registryP, &ping_method);
    if (env.fault_occurred) {
        server_log("Error", "xmlrpc_registry_add_method3() ping_method failed.  %s", env.fault_string);
        exit(1);
    }
    xmlrpc_registry_add_method3(&env, registryP, &udp_method);
    if (env.fault_occurred) {
        server_log("Error", "xmlrpc_registry_add_method3() udp_method failed.  %s", env.fault_string);
        exit(1);
    }
    xmlrpc_registry_add_method3(&env, registryP, &dns_method);
    if (env.fault_occurred) {
        server_log("Error", "xmlrpc_registry_add_method3() dns_method failed.  %s", env.fault_string);
        exit(1);
    }

    serverparm.config_file_name = NULL;   /* Select the modern normal API */
    serverparm.registryP        = registryP;
    serverparm.port_number      = config.rcp_port;
    serverparm.log_file_name    = "/var/log/tnp/xmlrpc_log";

    xmlrpc_server_abyss_create(&env, &serverparm, XMLRPC_APSIZE(registryP), &serverP);
    //xmlrpc_server_abyss_setup_sig(&env, serverP, &oldHandlersP);

    //while(1){}

    server_log("Info", "Started XML-RPC server");

    xmlrpc_server_abyss_run_server(&env, serverP);

    if (env.fault_occurred) {
        printf("xmlrpc_server_abyss() failed.  %s\n", env.fault_string);
        exit(1);
    }

    //xmlrpc_server_abyss_restore_sig(oldHandlersP);
    //free(oldHandlersP);

    server_log("Info", "Stopping XML-RPC server");

    xmlrpc_server_abyss_destroy(serverP);

    xmlrpc_server_abyss_global_term();

    /*xmlrpc_server_abyss(&env, &serverparm, XMLRPC_APSIZE(log_file_name));
    if (env.fault_occurred) {
        printf("xmlrpc_server_abyss() failed.  %s\n", env.fault_string);
        exit(1);
    }*/
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
    
    conn = mysql_init(NULL);
    /* Connect to database */
    if (!mysql_real_connect(conn, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, 0)) {
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

int mysql_sensor_connected(int id){
    char query[200];

    sprintf(query, "select active from sensors where sensor_id=%d", id);

    if(mysql_query(conn, query)){
        server_log("Error", "Databased checking connected sensor - %s", mysql_error(conn));
        return -1;
    }

    MYSQL_RES *result = mysql_store_result(conn);
  
    if (result == NULL) {
        server_log("Error", "Database empty results checking connected sensor");
        return -1; 
    }

    MYSQL_ROW row = mysql_fetch_row(result);
        
    int active = atoi(row[0]);
    mysql_free_result(result);

    return active;
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

int mysql_add_rtt(int sensor_id, int min, int max, int avg, int dev){
    char buff[200];
    char * query = "insert into rtts(sensor_id, min, max, avg, dev, time) values(%d, %d, %d, %d, %d, FROM_UNIXTIME(%d))\n";

    sprintf(buff, query, sensor_id, min, max, avg, dev, time(NULL));

    if(mysql_query(conn, buff)){
        server_log("Error", "Database adding bandwidth - %s", mysql_error(conn));
        return -1;
    }
    return 1;
}

int mysql_add_udp(int sensor_id, int size, int dur, int bw, int jit, int pkls, int dscp, int speed){
    char buff[200];
    char * query = "insert into udps(sensor_id, size, duration, bw, jitter, packet_loss, dscp_flag , send_bw, time) values(%d, %d, %d, %d, %d, %d, %d, %d, FROM_UNIXTIME(%d))\n";

    sprintf(buff, query, sensor_id, size, dur, bw, jit, pkls, dscp, speed, time(NULL));

    if(mysql_query(conn, buff)){
        server_log("Error", "Database adding udp - %s", mysql_error(conn));
        return -1;
    }
    return 1;
}

int mysql_add_dns(int sensor_id, int duration){
    char buff[200];
    char * query = "insert into dns(sensor_id, duration,time) values(%d, %d, FROM_UNIXTIME(%d))\n";

    sprintf(buff, query, sensor_id, duration, time(NULL));

    if(mysql_query(conn, buff)){
        server_log("Error", "Database adding dns - %s", mysql_error(conn));
        return -1;
    }
    return 1;
}

int mysql_add_dns_failure(int sensor_id){
    char buff[200];
    char * query = "insert into dns_failure(sensor_id, time) values(%d, FROM_UNIXTIME(%d))\n";

    sprintf(buff, query, sensor_id, time(NULL));

    if(mysql_query(conn, buff)){
        server_log("Error", "Database adding dns failure - %s", mysql_error(conn));
        return -1;
    }
    return 1;
}

int main(int argc, char ** argv) {

    //signal handler to close log file
    signal(SIGINT, closeLog);
    signal(SIGTERM, closeLog);

    //casue zombies to be reaped automatically 
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        perror(0);
        exit(1);
    }

    //log files
    logs = fopen("/var/log/tnp/server.log", "w+");

    server_log("Info" , "Server Started");

    //parse configurations from file
    if (ini_parse("/etc/network-sensor-server/config.ini", handler, &config) < 0) {
        server_log("Error", "Can't load 'config.ini'\n");
        return 1;
    }

    //make deamon
    if(argc == 2)
        deamonise();

    //connect to database
    mysql_connect();

    int welcomeSocket;//, newSocket;
    char buffer[1024];
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
    serverAddr.sin_port = htons(config.server_port);
    /* Set IP address to localhost */
    serverAddr.sin_addr.s_addr = inet_addr(config.server_addr);
    /* Set all bits of the padding field to 0 */
    memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);  

    /*---- Bind the address struct to the socket ----*/
    bind(welcomeSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));

    /*---- Listen on the socket, with 10 max connection requests queued ----*/
    if(listen(welcomeSocket,10)==0){
        server_log("Info" , "Listening on %s", config.server_addr);
    }else{
        server_log("Error" , "Failed to listen on %s", config.server_addr);
        exit(1);
    }

    //start rpc server in thread
    pthread_t pth;
    pthread_create(&pth, NULL, rpc_server, (void * ) 1);

    //listen loop
    while(1){
        /*---- Accept call creates a new socket for the incoming connection ----*/
        int newSocket = accept(welcomeSocket, (struct sockaddr *) &clientAddr, &addr_size);

        char addr[15];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size);

        //add to db 
        int id = mysql_add_sensor(addr);
        server_log("Info" , "Sensor %s connected with id %d", inet_ntop(AF_INET, &clientAddr.sin_addr, addr, addr_size), id);

        //remember socket
        install(id, newSocket);

        //receive loop - should start loops outside
        if(fork() == 0){
            //heartbeat loop
            int hb_pid;
            if((hb_pid = fork()) == 0){
                //build request
                struct srrp_request * hb_request;
                hb_request = (struct srrp_request *) send_buff;
                hb_request->id = 1;
                hb_request->type = SRRP_HB;

                while(1){
                    if(!mysql_sensor_connected(id))
                        _exit(0);

                    printf("Sending hb request\n");
                    send(newSocket, send_buff, sizeof(send_buff), 0);
                    sleep(1);           //second
                }
            }

            //ping loop
            int ping_pid;
            if((ping_pid = fork()) == 0){
                //initial sleep of 10 seconds
                sleep(10);  

                //build the request
                struct srrp_request * rtt_request;
                rtt_request = (struct srrp_request *) send_buff;
                rtt_request->id = 55;
                rtt_request->type = SRRP_RTT;
                rtt_request->length = 1;

                struct srrp_param ittr;
                ittr.param = SRRP_ITTR;
                ittr.value = 5;
                rtt_request->params[0] = ittr;

                while(10){
                    if(!mysql_sensor_connected(id))
                        _exit(0);

                    server_log("Info", "Sending ping request to sensor %d", id);
                    send(newSocket, send_buff, sizeof(send_buff), 0);
                    sleep(60);      //1 minutes
                }

            }

            //iperf loop
            int iperf_pid;
            if((iperf_pid = fork()) == 0){
                //initial sleep of 30 seconds
                sleep(60);  

                //build the request
                struct srrp_request * tp_request;
                tp_request = (struct srrp_request *) send_buff;
                tp_request->id = 99;
                tp_request->type = SRRP_BW;
                tp_request->length = 1;

                struct srrp_param dur;
                dur.param = SRRP_DUR;
                dur.value = 10;
                tp_request->params[0] = dur;

                while(10){
                    if(!mysql_sensor_connected(id))
                        _exit(0);

                    server_log("Info", "Sending iperf request to sensor %d", id);
                    send(newSocket, send_buff, sizeof(send_buff), 0);
                    sleep(300);     //5 minutes
                }
            }

            //dns loop
            int dns_pid;
            if((dns_pid = fork()) == 0){
                //inital sleep
                sleep(5);

                //build the request
                struct srrp_request * dns_request;
                dns_request = (struct srrp_request *) send_buff;
                dns_request->id = 33;
                dns_request->type = SRRP_DNS;
                dns_request->length = 0;

                while(1){
                    if(!mysql_sensor_connected(id))
                        _exit(0);

                    server_log("Info", "Sending dns request to sensor %d", id);
                    send(newSocket, send_buff, sizeof(send_buff), 0);
                    sleep(60);      //1 minute
                }
            }

            struct timeval tv;

            int bytes = 1;
            
            while(bytes){
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET (newSocket, &rfds);

                tv.tv_sec = config.server_timeout;
                tv.tv_usec = 0;
                
                int ready = select(newSocket + 1, &rfds, NULL, NULL, &tv);
                
                if(ready == -1){
                    server_log("Error", "select()");
                    perror("select()\n");
                }else if(ready){
                    bytes = recv(newSocket,recv_buff, sizeof(recv_buff),0);

                    struct srrp_response * response;
                    response = (struct srrp_response *) recv_buff;
                    //response->id      = 10;
                    
                    if(response->id == 0){
                        printf("Received hb response\n");
                    }else if(response->id == 99){
                        server_log("Info", "Recevied iperf response from sensor %d", id);

                        int i;
                        int bandwidth = 0;
                        int duration = 0;
                        int size = 0;
                        for(i = 0; i < response->length ; i++){
                            if(response->results[i].result == SRRP_RES_DUR){
                                duration = response->results[i].value;
                                //printf("iperf duration = %d\n", response->results[i].value);
                            }else if(response->results[i].result == SRRP_RES_SIZE){
                                size = response->results[i].value;
                                //printf("iperf size = %d\n", response->results[i].value);
                            }else if(response->results[i].result == SRRP_RES_BW){
                                bandwidth = response->results[i].value;
                                //printf("iperf speed = %d\n", response->results[i].value);
                            }else{
                                server_log("Error", "Unrecoginsed result type for iperf test - %d", response->results[i].result);
                            }
                        }

                        //if all the results - add to db
                        if(bandwidth && duration && size){
                            mysql_add_bw(id, bandwidth, duration, size);
                        }else{
                            server_log("Error", "Response missing iperf results from sensor %d", id);
                        }

                    }else if(response->id == 55){
                        server_log("Info", "Received ping response");

                        int i;
                        int avg = 0;
                        int max = 0;
                        int min = 0;
                        int dev = 0;

                        for(i = 0; i < response->length ; i++){
                            if(response->results[i].result == SRRP_RES_RTTMAX){
                                max = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_RTTMIN){
                                min = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_RTTDEV){
                                dev = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_RTTAVG){
                                avg = response->results[i].value;
                            }else{
                                server_log("Error", "Unrecoginsed result type for ping test - %d", response->results[i].result);
                            }
                        }

                        if(avg && min && dev && max){
                            mysql_add_rtt(id, min, max, avg, dev);
                        }else{
                            server_log("Error", "Response missing ping results from sensor %d", id);
                        }

                    }else if(response->id == 22){
                        server_log("Info", "Received udp iperf response");

                        int i, dur, size, speed, dscp, bw, jit, pkls;

                        //parse results
                        for(i=0; i < response->length; i++){
                            if(response->results[i].result == SRRP_RES_DUR){
                                dur = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_SIZE){
                                size = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_BW){
                                bw = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_JTR){
                                jit = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_PKLS){
                                pkls = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_SPEED){
                                speed = response->results[i].value;
                            }else if(response->results[i].result == SRRP_RES_DSCP){
                                dscp = response->results[i].value;
                            }else{
                                server_log("Error", "Unrecognised result type for udp iperf");
                            }
                        }

                        //add to db
                        if(dur && size && bw && jit && pkls && speed){
                            mysql_add_udp(id, size, dur, bw, jit, pkls, dscp, speed);
                        }else{
                            server_log("Error", "Response missing udp iperf results from sensor %d", id);
                        }

                    }else if(response->id == 33){
                        printf("RECV!\n");
                        if(response->success == SRRP_SCES){
                            server_log("Info", "Received successfull dns response from sensor %d", id);

                            mysql_add_dns(id, response->results[0].value);
                        }else{
                            server_log("Info", "Received unsucessfull dns response from sensor %d", id);

                            mysql_add_dns_failure(id);
                        }

                    }else{
                        server_log("Error", "Uncognised data type - %d", response->id);
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

            //kill iperf loop
            kill(iperf_pid, SIGKILL);

            //kill dns loop
            kill(dns_pid, SIGKILL);

            //kill comm (process)
            _exit(0);
        }
    }

    close(welcomeSocket);

    fclose(logs);
    closelog();

    return 0;
}