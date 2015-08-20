#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

#include <mysql.h>

#define NAME "Xmlrpc-c Test Client"
#define VERSION "1.0"

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>
#include <xmlrpc-c/base.h>
#include <xmlrpc-c/server.h>
#include <xmlrpc-c/server_abyss.h>

#include "ini/ini.h"
#include "ini/ini.c"

#define RTT 1
#define TCP 2
#define UDP 3
#define DNS 4

const char * const serverUrl = "http://localhost:8080/RPC2";

FILE* logs;
int deamon = 0;

int global_mid;

/*
* Config struct to be loaded in 
*/
typedef struct{
    //rcp
    int scheduler_rpc_port;

    //database
    const char* mysql_addr;
    const char* mysql_usr;
    const char* mysql_pass;
} configuration;
configuration config;

void schedule_log(const char * type, const char * fmt, ...){
    //format message
    va_list args; 
    va_start(args, fmt);

    char msg[200];
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

    fprintf(logs, "%s - Schduler - [%s] - %s\n", stime, type, msg);       //write to log
    fflush(logs);

    // if not deamon - print logs
    if(!deamon)
        printf("%s - %s\n", type, msg);
}

static int handler(void* user, const char* section, const char* name, const char* value){
    configuration* pconfig = (configuration*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("rpc", "scheduler_rpc_port")) {
        pconfig->scheduler_rpc_port = atoi(value);
    }else if (MATCH("database", "mysql_addr")){
        pconfig->mysql_addr = strdup(value);
    }else if (MATCH("database", "mysql_usr")){
        pconfig->mysql_usr = strdup(value);
    }else if (MATCH("database", "mysql_pass")){
        pconfig->mysql_pass = strdup(value);
    }else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

static void dieIfFaultOccurred (xmlrpc_env * const envP) {
    if (envP->fault_occurred) {
        schedule_log("ERROR: %s (%d)", envP->fault_string, envP->fault_code);
        exit(1);
    }
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

    schedule_log("Info", "Deamonised");
}

int mysql_add_schedule(char * method_name, int mid, int sid, int src, int src_type, int dst, int dst_type, int delay, int param[], char * str_param[]){
	MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, CLIENT_MULTI_STATEMENTS)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return -1;
    }

    //build query
    char buff[1000];
    char * query;

    //exception for dns schedule with no recipient - should fix 
    if(strcmp(method_name, "dns") == 0){
    	query = "insert into schedule_measurements(measurement_id, schedule_id, source_id, source_type, method, delay, active) VALUES('%d', '%d', '%d', '%d', '%s', '%d', '%d') ON DUPLICATE KEY UPDATE schedule_id=VALUES(schedule_id), source_id=VALUES(source_id), source_type=VALUES(source_type), delay=VALUES(delay), method=VALUES(method), active=VALUES(active), measurement_id=LAST_INSERT_ID(measurement_id); \n";
    	sprintf(buff, query, mid, sid, src, src_type, method_name, delay, 1);
    }else{
    	query = "insert into schedule_measurements(measurement_id, schedule_id, source_id, source_type, destination_id, destination_type, method, delay, active) VALUES('%d', '%d', '%d', '%d', '%d', '%d', '%s', '%d', '%d') ON DUPLICATE KEY UPDATE schedule_id=VALUES(schedule_id), source_id=VALUES(source_id), source_type=VALUES(source_type), destination_id=VALUES(destination_id), destination_type=(destination_type), delay=VALUES(delay), method=VALUES(method), active=VALUES(active), measurement_id=LAST_INSERT_ID(measurement_id); \n";
    	sprintf(buff, query, mid, sid, src, src_type, dst, dst_type, method_name, delay, 1);
    }

    if (mysql_query(contd, buff)) {
      schedule_log("Error", "Database adding schedule measurement - %s", mysql_error(contd));
      return -1;
    }

    //what if adding params fail?
    int id = mysql_insert_id(contd);

    //only add params if new 
    if(strcmp(method_name, "rtt") == 0){
        query = "insert into schedule_params(measurement_id, param, value) VALUES('%d', 'iterations', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\n";
        sprintf(buff, query, id, param[0]);
    }else if(strcmp(method_name, "tcp") == 0){
        query = "insert into schedule_params(measurement_id, param, value) VALUES('%d', 'duration', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\n";
        sprintf(buff, query, id, param[0]);
    }else if(strcmp(method_name, "udp") == 0){
        query = "insert into schedule_params(measurement_id, param, value) VALUES('%d', 'send_speed', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\
                insert into schedule_params(measurement_id, param, value) VALUES('%d', 'packet_size', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\
                insert into schedule_params(measurement_id, param, value) VALUES('%d', 'duration', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\
                insert into schedule_params(measurement_id, param, value) VALUES('%d', 'dscp_flag', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\n";
        sprintf(buff, query, id, param[0], id, param[1], id, param[2], id, param[3]);
    }else if(strcmp(method_name, "dns") == 0){
        query = "insert into schedule_params(measurement_id, param, value) VALUES('%d', 'server', '%s') ON DUPLICATE KEY UPDATE value=VALUES(value);\
                insert into schedule_params(measurement_id, param, value) VALUES('%d', 'domain_name', '%s') ON DUPLICATE KEY UPDATE value=VALUES(value);\n";
        sprintf(buff, query, id, str_param[0], id, str_param[1]);
    }

    if (mysql_query(contd, buff)) {
      schedule_log("Error", "Database adding schedule params - %s", mysql_error(contd));
      return -1;
    }

    mysql_close(contd);

    return id;
}

int mysql_stop_schedule(int mid){
	MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, 0)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return -1;
    }

    char buff[100];
    char * query = "update schedule_measurements set active=0 where measurement_id='%d'\n";
    sprintf(buff, query, mid);

    if (mysql_query(contd, buff)) {
      fprintf(stderr, "%s\n", mysql_error(contd));
      schedule_log("Error", "Database removing schedule - %s", mysql_error(contd));
      mysql_close(contd);
      return -1;
   }
   mysql_close(contd);
   return 1;
}

int mysql_stop_all(){
    MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, 0)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return -1;
    }

    char * query = "update schedule_measurements set active=0\n";

    if (mysql_query(contd, query)) {
      fprintf(stderr, "%s\n", mysql_error(contd));
      schedule_log("Error", "Database removing schedule - %s", mysql_error(contd));
      mysql_close(contd);
      return -1;
   }
   mysql_close(contd);
   return 1;
}

int mysql_update_pid(int mid, int pid){
	MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, 0)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return -1;
    }

    char buff[100];
    char * query = "update schedule_measurements set pid='%d' where measurement_id='%d'\n";

    sprintf(buff, query, pid, mid);

    if (mysql_query(contd, buff)) {
      fprintf(stderr, "%s\n", mysql_error(contd));
      schedule_log("Error", "Database updating pid of schedule - %s", mysql_error(contd));
      mysql_close(contd);
      return -1;
   }
   mysql_close(contd);
   return 1;
}

int mysql_update_status(int mid, int status){
    MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, 0)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return -1;
    }

    char buff[100];
    char * query = "update schedule_measurements set status='%d' where measurement_id='%d'\n";

    sprintf(buff, query, status, mid);

    if (mysql_query(contd, buff)) {
      fprintf(stderr, "%s\n", mysql_error(contd));
      schedule_log("Error", "Database updating status of schedule - %s", mysql_error(contd));
      mysql_close(contd);
      return -1;
   }
   mysql_close(contd);
   return 1;
}

void closeLog(int sig){
    schedule_log("Info", "Scheduler stopped");
    mysql_stop_all();
    fclose(logs);
    exit(1);
}

void endSchedule(int sig){
	mysql_stop_schedule(global_mid);
	schedule_log("Info", "Received signal to susspend schedule %d", global_mid);
	exit(1);
}

/*
*
*/
int call(char * method_name, int src, int dst, int param[], char * str_param[]){
	xmlrpc_env env;
    xmlrpc_value * resultP;
    xmlrpc_int32 success;
    
    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    /* Start up our XML-RPC client library. */
    xmlrpc_client_init2(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0);
    dieIfFaultOccurred(&env);

 	if(strcmp(method_name, "ping.request") == 0){
   		resultP = xmlrpc_client_call(&env, serverUrl, "ping.request", "(iii)", (xmlrpc_int32) src, (xmlrpc_int32) dst, (xmlrpc_int32) param[0]);
    }else if(strcmp(method_name, "iperf.request") == 0){
   		resultP = xmlrpc_client_call(&env, serverUrl, "iperf.request", "(iii)", (xmlrpc_int32) src, (xmlrpc_int32) dst, (xmlrpc_int32) param[0]);
    }else if(strcmp(method_name, "udp.request") == 0){
   		resultP = xmlrpc_client_call(&env, serverUrl, "udp.request", "(iiiiii)", (xmlrpc_int32) src, (xmlrpc_int32) dst, (xmlrpc_int32) param[0], (xmlrpc_int32) param[1], (xmlrpc_int32) param[2], (xmlrpc_int32) param[3]);
    }else if(strcmp(method_name, "dns.request") == 0){
   		resultP = xmlrpc_client_call(&env, serverUrl, "dns.request", "(iss)", (xmlrpc_int32) src, str_param[0], str_param[1]);
    }else{
    	schedule_log("Error", "Unrecognised method name %s", method_name);
   		return 1;
    }
    
    dieIfFaultOccurred(&env);
    
    //get resutl 
    xmlrpc_read_int(&env, resultP, &success);
    dieIfFaultOccurred(&env);
    
    //dispose of our result value
    xmlrpc_DECREF(resultP);

    // clean up our error-handling environment
    xmlrpc_env_clean(&env);
    
    //shutdown our XML-RPC client library 
    xmlrpc_client_cleanup();

    return (int) success;
}

int send_request(char * method_name, int src, int src_type, int dst, int dst_type, int param[], char * str_param[], int sid){

    int faults = 0;

    //neither source or destination is a group
    if(src_type == 0 && dst_type == 0){
        schedule_log("Info", "Schedule %d source and destination are both sensors - sending single request to server", sid);
        faults = call(method_name, src, dst, param, str_param);
        schedule_log("Info", "Schedule %d request sent with %d faults", sid, faults);
        return faults;
    }

    //mysql connecy
    MYSQL * contd;
    contd = mysql_init(NULL);
    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, CLIENT_MULTI_STATEMENTS)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return 0;
    }

    char buff[100];
    char * query;
    int * srcs;
    int * dsts;
    int src_cnt;
    int dst_cnt;
    int i, j;

    //source is a group
    if(src_type == 1){
        schedule_log("Info", "Schedule %d source is a group - getting group members", sid);

        query = "SELECT sensor_id FROM group_membership WHERE group_id ='%d'\n";
        sprintf(buff, query, src);

        if (mysql_query(contd, buff)) {
            fprintf(stderr, "%s\n", mysql_error(contd));
            schedule_log("Error", "Database getting sensors in group - %s", mysql_error(contd));
            mysql_close(contd);
            return -1;
        }

        MYSQL_RES *result = mysql_store_result(contd);
        if (result == NULL) {
            schedule_log("Error", "Database empty results getting sensors in group %d", dst);
            return -1; 
        }

        //get number of sensors in group and initalise soures array
        src_cnt = mysql_num_rows(result);
        srcs = malloc(sizeof(int) * src_cnt);

        schedule_log("Info", "Schedule %d source has %d members", sid, src_cnt);

        //laod results into sources array
        MYSQL_ROW row;
        for(i = 0; i < src_cnt; i++){
            row = mysql_fetch_row(result);
            srcs[i] = atoi(row[0]);
        }

        mysql_free_result(result);
    }else{
        srcs = malloc(sizeof(int));
        srcs[0] = src;
        src_cnt = 1;
    }

    //destination is a group
    if(dst_type == 1){
        schedule_log("Info", "Schedule %d destination is a group - getting group members", sid);

        query = "SELECT sensor_id FROM group_membership WHERE group_id ='%d'\n";
        sprintf(buff, query, dst);

        if (mysql_query(contd, buff)) {
            fprintf(stderr, "%s\n", mysql_error(contd));
            schedule_log("Error", "Database getting sensors in group - %s", mysql_error(contd));
            mysql_close(contd);
            return -1;
        }

        MYSQL_RES *result = mysql_store_result(contd);
        if (result == NULL) {
            schedule_log("Error", "Database empty results getting sensors in group %d", dst);
            return -1; 
        }

        //get number of sensors in group and initalise soures array
        dst_cnt = mysql_num_rows(result);
        dsts = malloc(sizeof(int) * dst_cnt);

        schedule_log("Info", "Schedule %d destination has %d members", sid, dst_cnt);

        //laod results into sources array
        MYSQL_ROW row;
        for(i = 0; i < dst_cnt; i++){
            row = mysql_fetch_row(result);
            dsts[i] = atoi(row[0]);
        }

        mysql_free_result(result);
    }else{
        dsts    = malloc(sizeof(int));
        dsts[0] = dst; 
        dst_cnt = 1;
    }

    mysql_close(contd);

    schedule_log("Info", "Sending all schedule %d requests to server.", sid);

    for(i = 0; i < src_cnt; i++){
        for(j = 0; j < dst_cnt; j++){
            if(srcs[i] != dsts[j])      //dont call if src and dst are the same sensor
                faults+=call(method_name, srcs[i], dsts[j], param, str_param);
        }
    }

    schedule_log("Info", "All schedule %d requests sent with %d faults", sid, faults);
    return faults;  
}

/*
*
*/
static xmlrpc_value * add_rtt_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 mid, sid, src, src_type, dst, dst_type, ittr, interval, delay;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiiiiiiii)", &mid, &sid, &src, &src_type, &dst, &dst_type, &ittr, &interval, &delay);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse add_request request argument array");
        return NULL;
    }

    if((int) mid == 0)
        schedule_log("Info", "Created RTT measurement from %d to %d with an interval of %d seconds", (int) src, (int) dst, (int) interval);
    else
        schedule_log("Info", "Starting RTT measurement ", (int) mid);

    int param[1] = {(int) ittr};
   	mid = mysql_add_schedule("rtt", mid, sid, src, src_type, dst, dst_type, delay, param, NULL);

    if(!mid)
        return NULL;

   	//what if fork fails?
    if(fork() == 0){
    	signal(SIGINT, endSchedule);
    	signal(SIGTERM, endSchedule);

    	global_mid = (int) mid;

    	mysql_update_pid((int) mid, getpid());

        sleep((int) delay);

    	while(1){
            schedule_log("Info", "Measurement %d invoked", mid);
	    	if(!send_request("ping.request", (int)src, (int)src_type, (int)dst, (int)dst_type, param, NULL, mid)){
                mysql_update_status(mid, 1);  	
	    	}else{
	    		mysql_update_status(mid, 0);
	    	}
    		sleep((int) interval);
    	}
    }

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) mid);
}

/*
*
*/
static xmlrpc_value * add_tcp_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 mid, sid, src, src_type, dst, dst_type, dur, interval, delay;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiiiiiiii)", &mid, &sid, &src, &src_type, &dst, &dst_type, &dur, &interval, &delay);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse add_request request argument array");
        return NULL;
    }

    int param[1] = {(int) dur};
    mid = mysql_add_schedule("tcp", mid, sid, src, src_type, dst, dst_type, delay, param, NULL);

    if(!mid)
        return NULL;

    if((int) mid == 0)
        schedule_log("Info", "Created TCP measurement from %d to %d with an interval of %d seconds - Schedule id %d", (int) src, (int) dst, (int) interval, mid);    
    else
        schedule_log("Info", "Starting TCP measurement - Measurement id %d", mid);    

    if(fork() == 0){

    	signal(SIGINT, endSchedule);
    	signal(SIGTERM, endSchedule);

    	global_mid = (int) mid;

    	mysql_update_pid((int) mid, getpid());

        sleep((int) delay);

    	while(1){
	    	if(!send_request("iperf.request", (int)src, (int)src_type, (int)dst, (int)dst_type, param, NULL, mid)){
                mysql_update_status(mid, 1);
	    	}else{
	    		mysql_update_status(mid, 0);
	    	}
    		sleep((int) interval);
    	}
    }

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) mid);
}

/*
*
*/
static xmlrpc_value * add_udp_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 mid, sid, src, src_type, dst, dst_type, dur, speed, size, dscp, interval, delay;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiiiiiiiiiii)", &mid, &sid, &src, &src_type, &dst, &dst_type, &speed, &size, &dur, &dscp, &interval, &delay);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse add_udp_schedule request argument array");
        return NULL;
    }

    int param[4] = {(int) speed, (int) size, (int) dur, (int) dscp};
    mid = mysql_add_schedule("udp", mid, sid, src, src_type, dst, dst_type, delay, param, NULL);

    if(!mid)
        return NULL;

    if((int) mid == 0)
        schedule_log("Info", "Created UDP measurement from %d to %d with an interval of %d seconds - Schedule ID", (int) src, (int) dst, (int) interval, mid);
    else
        schedule_log("Info", "Started UDP measurement - Measurement ID %d", mid);

    if(fork() == 0){

    	signal(SIGINT, endSchedule);
    	signal(SIGTERM, endSchedule);

    	global_mid = mid;

    	mysql_update_pid((int) mid, getpid());

        sleep((int) delay);

    	while(1){
	    	if(!send_request("udp.request", (int)src, (int)src_type, (int)dst, (int)dst_type, param, NULL, mid)){
                mysql_update_status(mid, 1);
	    	}else{
	    		mysql_update_status(mid, 0);
	    	}
    		sleep((int) interval);
    	}
    }

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) mid);
}

/*
*
*/
static xmlrpc_value * add_dns_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 mid, sid, src, src_type, interval, delay;
    const char * server;
    const char * domain_name;

    int params = xmlrpc_array_size(envP, paramArrayP);

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiiissii)", &mid, &sid, &src, &src_type, &domain_name, &server, &interval, &delay); 
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse add_request request argument array");
        return NULL;
    }

    char * str_param[] = {strdup(domain_name), strdup(server)};
    mid = mysql_add_schedule("dns", mid, sid, src, src_type, 0, 0, delay, NULL, str_param);

    if(!mid)
        return;

    if((int) mid == 0)
        schedule_log("Info", "Created DNS measuremnt to %d with an interval of %d seconds - Measurement ID %d", (int) src, (int) interval, mid);
    else
        schedule_log("Info", "Started DNS measurement - Measurement ID %d", mid);

    if(fork() == 0){

    	signal(SIGINT, endSchedule);
    	signal(SIGTERM, endSchedule);

    	global_mid = mid;
 		mysql_update_pid((int) mid, getpid());

        sleep((int) delay);

    	while(1){
	    	if(!send_request("dns.request", (int)src, (int)src_type, 0, 0, NULL, str_param, mid)){
                mysql_update_status(mid, 1);	
	    	}else{
	    		mysql_update_status(mid, 0);
	    	}

    		schedule_log("Info", "Sending request from schedule id %d", mid); 
    		sleep((int) interval);
    	}
    }

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) mid);
}

/*
*
*/
static xmlrpc_value * stop_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 mid, pid;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(ii)", &mid, &pid);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse stop_schedule request argument array");
        return NULL;
    }

    schedule_log("Info", "RPC Stopping schedule %d (pid=%d)", mid, pid);

    int ret = kill((int) pid, SIGINT);

    mysql_update_pid((int) mid, 0);		

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) ret);
}

int main(int argc, char ** argv){	
	signal(SIGINT, closeLog);
    signal(SIGTERM, closeLog);

    //open log file
	logs = fopen("/var/log/network-sensor-server/server.log", "a");
	schedule_log("Info", "Strated scheduler");

	//load in config
	if (ini_parse("/etc/network-sensor-server/config.ini", handler, &config) < 0) {
        schedule_log("Error", "Can't load 'config.ini'\n");
        return 1;
    }

	//make deamon
    if(argc == 2)
        deamonise();

    /*                  */
    /* -- RPC server -- */
    /*                  */

    struct xmlrpc_method_info3 const add_rtt_schedule_method = {
        /* .methodName     = */ "add_rtt_schedule.request",
        /* .methodFunction = */ &add_rtt_schedule,
    };
    struct xmlrpc_method_info3 const add_tcp_schedule_method = {
        /* .methodName     = */ "add_tcp_schedule.request",
        /* .methodFunction = */ &add_tcp_schedule,
    };
    struct xmlrpc_method_info3 const add_udp_schedule_method = {
        /* .methodName     = */ "add_udp_schedule.request",
        /* .methodFunction = */ &add_udp_schedule,
    };
    struct xmlrpc_method_info3 const add_dns_schedule_method = {
        /* .methodName     = */ "add_dns_schedule.request",
        /* .methodFunction = */ &add_dns_schedule,
    };
    struct xmlrpc_method_info3 const stop_schedule_method = {
        "stop_schedule.request",
        &stop_schedule,
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
        schedule_log("Error", "xmlrpc_registry_new() failed.  %s", env.fault_string);
        exit(1);
    }

    //add methods
    xmlrpc_registry_add_method3(&env, registryP, &add_rtt_schedule_method);
    if (env.fault_occurred) {
        schedule_log("Error", "xmlrpc_registry_add_rtt_method3() add_schdule failed.  %s", env.fault_string);
        exit(1);
    }
    xmlrpc_registry_add_method3(&env, registryP, &add_tcp_schedule_method);
    if (env.fault_occurred) {
        schedule_log("Error", "xmlrpc_registry_add_tcp_method3() add_schdule failed.  %s", env.fault_string);
        exit(1);
    }
    xmlrpc_registry_add_method3(&env, registryP, &add_udp_schedule_method);
    if (env.fault_occurred) {
        schedule_log("Error", "xmlrpc_registry_add_udp_method3() add_schdule failed.  %s", env.fault_string);
        exit(1);
    }
    xmlrpc_registry_add_method3(&env, registryP, &add_dns_schedule_method);
    if (env.fault_occurred) {
        schedule_log("Error", "xmlrpc_registry_add_dns_method3() add_schdule failed.  %s", env.fault_string);
        exit(1);
    }
    xmlrpc_registry_add_method3(&env, registryP, &stop_schedule_method);
    if (env.fault_occurred) {
        schedule_log("Error", "xmlrpc_registry_add_dns_method3() add_schdule failed.  %s", env.fault_string);
        exit(1);
    }

    serverparm.config_file_name = NULL;   /* Select the modern normal API */
    serverparm.registryP        = registryP;
    serverparm.port_number      = config.scheduler_rpc_port;
    serverparm.runfirst         = NULL;
    serverparm.runfirst_arg     = NULL;
    serverparm.log_file_name    = "/var/log/network-sensor-server/xmlrpc_log";

    xmlrpc_server_abyss_create(&env, &serverparm, XMLRPC_APSIZE(log_file_name), &serverP);

    schedule_log("Info", "Started XML-RPC server");

    xmlrpc_server_abyss_run_server(&env, serverP);

    if (env.fault_occurred) {
        printf("xmlrpc_server_abyss() failed.  %s\n", env.fault_string);
        exit(1);
    }

    schedule_log("Info", "Stopping XML-RPC server");

    xmlrpc_server_abyss_destroy(serverP);
    xmlrpc_server_abyss_global_term();
}