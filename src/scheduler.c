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

int global_sid;

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

int mysql_add_schedule(char * method_name, int id, int src, int src_type, int dst, int dst_type, int interval, int param[]){
	MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, CLIENT_MULTI_STATEMENTS)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return 0;
    }

    //create details string
    char * details_fmt;
    char details[100];

    if(strcmp(method_name, "rtt") == 0){
    	details_fmt = "RTT( iterations = %d)";
    	sprintf(details, details_fmt, param[0]);
    }else if(strcmp(method_name, "tcp") == 0){
    	details_fmt = "TCP( duration = %d)";
    	sprintf(details, details_fmt, param[0]);
	}else if(strcmp(method_name, "udp") == 0){
		details_fmt = "UDP( send speed = %d, datagram size = %d, dscp flag = %d, duration = %d)";
    	sprintf(details, details_fmt, param[0], param[1], param[2], param[3]);
    }else if(strcmp(method_name, "dns") == 0){
    	details_fmt = "DNS status";
    	strcpy(details, details_fmt);
    }

    //build query
    char buff[1000];
    char * query;

    //exception for dns schedule with no recipient - should fix 
    if(strcmp(method_name, "dns") == 0){
    	query = "insert into schedules(schedule_id, source_id, source_type, period, method, details, active) VALUES('%d', '%d', '%d', '%d', '%s', '%s', '%d') ON DUPLICATE KEY UPDATE source_id=VALUES(source_id), source_type=VALUES(source_type), period=VALUES(period), method=VALUES(method), details=VALUES(details), active=VALUES(active), schedule_id=LAST_INSERT_ID(schedule_id); \n";
    	sprintf(buff, query, id, src, src_type, interval, method_name, details, 1);
        //schedule_log("Query", "%s", buff);
    }else{
    	query = "insert into schedules(schedule_id, source_id, source_type, destination_id, destination_type, period, method, details, active) VALUES('%d', '%d', '%d', '%d', '%d', '%d', '%s', '%s', '%d') ON DUPLICATE KEY UPDATE source_id=VALUES(source_id), source_type=VALUES(source_type), destination_id=VALUES(destination_id), destination_type=(destination_type), period=VALUES(period), method=VALUES(method), details=VALUES(details), active=VALUES(active), schedule_id=LAST_INSERT_ID(schedule_id); \n";
    	sprintf(buff, query, id, src, src_type, dst, dst_type, interval, method_name, details, 1);
    }

    if (mysql_query(contd, buff)) {
      schedule_log("Error", "Database adding schedule - %s", mysql_error(contd));
      return -1;
    }

    //what if adding params fail?
    int sid = mysql_insert_id(contd);

    //only add params if new 
    if(strcmp(method_name, "rtt") == 0){
        query = "insert into schedule_params(schedule_id, param, value) VALUES('%d', 'iterations', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\n";
        sprintf(buff, query, sid, param[0]);
    }else if(strcmp(method_name, "tcp") == 0){
        query = "insert into schedule_params(schedule_id, param, value) VALUES('%d', 'duration', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\n";
        sprintf(buff, query, sid, param[0]);
    }else if(strcmp(method_name, "udp") == 0){
        query = "insert into schedule_params(schedule_id, param, value) VALUES('%d', 'send_speed', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\
                insert into schedule_params(schedule_id, param, value) VALUES('%d', 'packet_size', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\
                insert into schedule_params(schedule_id, param, value) VALUES('%d', 'dscp_flag', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\
                insert into schedule_params(schedule_id, param, value) VALUES('%d', 'duration', '%d') ON DUPLICATE KEY UPDATE value=VALUES(value);\n";

        sprintf(buff, query, sid, param[0], sid, param[1], sid, param[2], sid, param[3]);
        //schedule_log("Query", "%s", buff);
    }else if(strcmp(method_name, "dns") == 0){
        query = "insert into schedule_params(schedule_id, param, value) VALUES('%d', 'abcd', '1') ON DUPLICATE KEY UPDATE value=VALUES(value);\n";
        sprintf(buff, query, sid);
    }

    if (mysql_query(contd, buff)) {
      schedule_log("Error", "Database adding schedule params - %s", mysql_error(contd));
      return -1;
    }

    mysql_close(contd);

    return sid;
}

int mysql_stop_schedule(int sid){
	MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, 0)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return 0;
    }

    char buff[100];
    char * query = "update schedules set active=0 where schedule_id='%d'\n";
    sprintf(buff, query, sid);

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
        return 0;
    }

    char * query = "update schedules set active=0\n";

    if (mysql_query(contd, query)) {
      fprintf(stderr, "%s\n", mysql_error(contd));
      schedule_log("Error", "Database removing schedule - %s", mysql_error(contd));
      mysql_close(contd);
      return -1;
   }
   mysql_close(contd);
   return 1;
}

int mysql_update_pid(int sid, int pid){
	MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, 0)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return 0;
    }

    char buff[100];
    char * query = "update schedules set pid='%d' where schedule_id='%d'\n";

    sprintf(buff, query, pid, sid);

    if (mysql_query(contd, buff)) {
      fprintf(stderr, "%s\n", mysql_error(contd));
      schedule_log("Error", "Database updating pid of schedule - %s", mysql_error(contd));
      mysql_close(contd);
      return -1;
   }
   mysql_close(contd);
   return 1;
}

int mysql_update_status(int sid, int status){
    MYSQL * contd;
    contd = mysql_init(NULL);

    if (!mysql_real_connect(contd, config.mysql_addr,
        config.mysql_usr, config.mysql_pass, "tnp", 0, NULL, 0)) {
        schedule_log("Error", "Database Connection - %s", mysql_error(contd));
        return 0;
    }

    char buff[100];
    char * query = "update schedules set status='%d' where schedule_id='%d'\n";

    sprintf(buff, query, status, sid);

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
	mysql_stop_schedule(global_sid);
	schedule_log("Info", "Received signal to susspend schedule %d", global_sid);
	exit(1);
}

/*
*
*/
int call(char * method_name, int src, int dst, int param[]){
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
   		resultP = xmlrpc_client_call(&env, serverUrl, "dns.request", "(i)", (xmlrpc_int32) src);
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

int send_request(char * method_name, int src, int src_type, int dst, int dst_type, int param[], int sid){

    int faults = 0;

    //neither source or destination is a group
    if(src_type == 0 && dst_type == 0){
        schedule_log("Info", "Schedule %d source and destination are both sensors - sending single request to server", sid);
        faults = call(method_name, src, dst, param);
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
                faults+=call(method_name, srcs[i], dsts[j], param);
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

    xmlrpc_int32 id, src, src_type, dst, dst_type, ittr, interval;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiiiiii)", &id, &src, &src_type, &dst, &dst_type, &ittr, &interval);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse add_request request argument array");
        return NULL;
    }

    if((int) id == 0)
        schedule_log("Info", "Created RTT schedule from sensor %d to %d with an interval of %d seconds", (int) src, (int) dst, (int) interval);
    else
        schedule_log("Info", "Starting RTT schedule ", (int) id);

    int param[1] = {(int) ittr};
   	int sid = mysql_add_schedule("rtt", id, src, src_type, dst, dst_type, interval, param);

   	//what if fork fails?
    if(fork() == 0){
    	signal(SIGINT, endSchedule);
    	signal(SIGTERM, endSchedule);

    	global_sid = sid;

    	mysql_update_pid(sid, getpid());

    	while(1){
            schedule_log("Info", "Schedule %d invoked", sid);
	    	if(!send_request("ping.request", (int)src, (int)src_type, (int)dst, (int)dst_type, param, sid)){
                mysql_update_status(sid, 1);  	
	    	}else{
	    		mysql_update_status(sid, 0);
	    	}
    		sleep((int) interval);
    	}
    }

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) sid);
}

/*
*
*/
static xmlrpc_value * add_tcp_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 id, src, src_type, dst, dst_type, dur, interval;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiiiiii)", &id, &src, &src_type, &dst, &dst_type, &dur, &interval);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse add_request request argument array");
        return NULL;
    }

    int param[1] = {(int) dur};
    int sid = mysql_add_schedule("tcp", id, src, src_type, dst, dst_type, interval, param);

    if((int)id==0)
        schedule_log("Info", "Created TCP schedule from sensor %d to %d with an interval of %d seconds - Schedule id %d", (int) src, (int) dst, (int) interval, sid);    
    else
        schedule_log("Info", "Starting TCP schedule - Schedule id %d", sid);    

    if(fork() == 0){

    	signal(SIGINT, endSchedule);
    	signal(SIGTERM, endSchedule);

    	global_sid = sid;

    	mysql_update_pid(sid, getpid());

    	while(1){
	    	if(!send_request("iperf.request", (int)src, (int)src_type, (int)dst, (int)dst_type, param, sid)){
                mysql_update_status(sid, 1);
	    	}else{
	    		mysql_update_status(sid, 0);
	    	}
    		sleep((int) interval);
    	}
    }

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) sid);
}

/*
*
*/
static xmlrpc_value * add_udp_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 id, src, src_type, dst, dst_type, dur, speed, size, dscp, interval;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiiiiiiiii)", &id, &src, &src_type, &dst, &dst_type, &speed, &size, &dur, &dscp, &interval);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse add_udp_schedule request argument array");
        return NULL;
    }

    int param[4] = {(int) speed, (int) size, (int) dur, (int) dscp};
    int sid = mysql_add_schedule("udp", id, src, src_type, dst, dst_type, interval, param);

    if((int)id==0)
        schedule_log("Info", "Created UDP schedule from sensor %d to %d with an interval of %d seconds - Schedule ID", (int) src, (int) dst, (int) interval, sid);
    else
        schedule_log("Info", "Started UDP schedule - Schedule ID %d", sid);

    if(fork() == 0){

    	signal(SIGINT, endSchedule);
    	signal(SIGTERM, endSchedule);

    	global_sid = sid;

    	mysql_update_pid(sid, getpid());

    	while(1){
	    	if(!send_request("udp.request", (int)src, (int)src_type, (int)dst, (int)dst_type, param, sid)){
                mysql_update_status(sid, 1);
	    	}else{
	    		mysql_update_status(sid, 0);
	    	}
    		sleep((int) interval);
    	}
    }

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) sid);
}

/*
*
*/
static xmlrpc_value * add_dns_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 id, src, src_type, interval;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(iiii)", &id, &src, &src_type, &interval);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse add_request request argument array");
        return NULL;
    }

    int sid = mysql_add_schedule("dns", id, src, src_type, 0, 0, interval, NULL);

    if((int)id==0)
        schedule_log("Info", "Created DNS schedule to sensor %d with an interval of %d seconds - Schedule ID %d", (int) src, (int) interval, sid);
    else
        schedule_log("Info", "Started DNS schedule - Schedule ID %d", sid);

    if(fork() == 0){

    	signal(SIGINT, endSchedule);
    	signal(SIGTERM, endSchedule);

    	global_sid = sid;
 		mysql_update_pid(sid, getpid());

    	while(1){
	    	if(!send_request("dns.request", (int)src, (int)src_type, 0, 0, NULL, sid)){
                mysql_update_status(sid, 1);	
	    	}else{
	    		mysql_update_status(sid, 0);
	    	}

    		schedule_log("Info", "Sending request from schedule id %d", sid); 
    		sleep((int) interval);
    	}
    }

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) sid);
}

/*
*
*/
static xmlrpc_value * stop_schedule( xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){

    xmlrpc_int32 sid, pid;

    //parse argument array
    xmlrpc_decompose_value(envP, paramArrayP, "(ii)", &sid, &pid);
    if (envP->fault_occurred){
        schedule_log("Error", "Could not parse stop_schedule request argument array");
        return NULL;
    }

    schedule_log("Info", "RPC Stopping schedule %d (pid=%d)", sid, pid);

    int ret = kill((int) pid, SIGINT);

    mysql_update_pid((int) sid, 0);		

    return xmlrpc_build_value(envP, "i", (xmlrpc_int32) ret);
}

void * rpc_server(void * arg){
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
    serverparm.runfirst			= NULL;
    serverparm.runfirst_arg		= NULL;
    serverparm.log_file_name    = "/var/log/network-sensor-server/xmlrpc_log";

    xmlrpc_server_abyss_create(&env, &serverparm, XMLRPC_APSIZE(log_file_name), &serverP);

    schedule_log("Info", "Started XML-RPC server");

    xmlrpc_server_abyss_run_server(&env, serverP);

    if (env.fault_occurred) {
        printf("xmlrpc_server_abyss() failed.  %s\n", env.fault_string);
        exit(1);
    }

    while(1){}

    schedule_log("Info", "Stopping XML-RPC server");

    xmlrpc_server_abyss_destroy(serverP);
    xmlrpc_server_abyss_global_term();
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

    //pthread for rpc server
	pthread_t pth;
    pthread_create(&pth, NULL, rpc_server, (void * ) 1);

	while(1){}
}