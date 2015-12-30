/*
* Config struct to be loaded in 
*/
typedef struct{
    //rcp
    int notifier_rpc_port;

    //database
    const char* mysql_addr;
    const char* mysql_usr;
    const char* mysql_pass;
} configuration;
configuration config;

void notify_log(const char * type, const char * fmt, ...){

    FILE* logs = fopen("/var/log/network-sensor-server/server.log", "a");

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

    fprintf(logs, "%s - Notifier - [%s] - %s\n", stime, type, msg);       //write to log
    fflush(logs);

    // if not deamon - print logs
    if(!deamon)
        printf("%s - %s\n", type, msg);

    fclose(logs);
}

static int handler(void* user, const char* section, const char* name, const char* value){
    configuration* pconfig = (configuration*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("rpc", "notifier_port")) {
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
        notify_log("ERROR: %s (%d)", envP->fault_string, envP->fault_code);
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

    notify_log("Info", "Deamonised");
}

void stop_notifier(int sig){
    notify_log("Info", "Scheduler stopped");
    mysql_stop_all();
    exit(1);
}

int mysql_add_alarm(){
	return 1;
}

int load_alarms(){
	return 1;
}

static xmlrpc_value * add_alarm(	xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){


}

static xmlrpc_value * metric(		xmlrpc_env *    const envP,
                                    xmlrpc_value *  const paramArrayP,
                                    void *          const serverInfo,
                                    void *          const channelInfo){


}

int main(int argc, char ** argv){	
	signal(SIGINT, stop_notifier);
    signal(SIGTERM, stop_notifier);

	notify_log("Info", "Strated scheduler");

	//load in config
	if (ini_parse("/etc/network-sensor-server/config.ini", handler, &config) < 0) {
        notify_log("Error", "Can't load 'config.ini'\n");
        return 1;
    }

	//make deamon
    if(argc == 2)
        deamonise();

    /*                  */
    /* -- RPC server -- */
    /*                  */

    struct xmlrpc_method_info3 const add_alarm_method = {
        "add_alarm.request",
        &add_alarm,
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
        notify_log("Error", "xmlrpc_registry_new() failed.  %s", env.fault_string);
        exit(1);
    }

    //add methods
    xmlrpc_registry_add_method3(&env, registryP, &add_alarm_method);
    if (env.fault_occurred) {
        notify_log("Error", "xmlrpc_registry_add_method3() add_alarm failed.  %s", env.fault_string);
        exit(1);
    }

    serverparm.config_file_name = NULL;   /* Select the modern normal API */
    serverparm.registryP        = registryP;
    serverparm.port_number      = config.notifier_rpc_port;
    serverparm.runfirst         = NULL;
    serverparm.runfirst_arg     = NULL;
    serverparm.log_file_name    = "/var/log/network-sensor-server/xmlrpc_log";

    xmlrpc_server_abyss_create(&env, &serverparm, XMLRPC_APSIZE(log_file_name), &serverP);

    notify_log("Info", "Started XML-RPC server");

    xmlrpc_server_abyss_run_server(&env, serverP);

    if (env.fault_occurred) {
        printf("xmlrpc_server_abyss() failed.  %s\n", env.fault_string);
        exit(1);
    }

    notify_log("Info", "Stopping XML-RPC server");

    xmlrpc_server_abyss_destroy(serverP);
    xmlrpc_server_abyss_global_term();
}