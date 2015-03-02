#include <stdlib.h>
#include <stdio.h>

#define NAME "Xmlrpc-c Test Client"
#define VERSION "1.0"

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>

static void dieIfFaultOccurred (xmlrpc_env * const envP) {
    if (envP->fault_occurred) {
        printf("ERROR: %s (%d)\n", envP->fault_string, envP->fault_code);
        exit(1);
    }
}

int main(int argc, char ** argv) {
    xmlrpc_env env;
    xmlrpc_value * resultP;
    xmlrpc_int32 success;
    const char * const serverUrl = "http://localhost:8080/RPC2";
    
    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    /* Start up our XML-RPC client library. */
    xmlrpc_client_init2(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0);
    dieIfFaultOccurred(&env);

    char * methodName;

    if(strcmp(argv[1], "ping") == 0){
        methodName = "ping.request";

        //sensor id
        //itterations
        resultP = xmlrpc_client_call(&env, serverUrl, methodName, "(ii)", (xmlrpc_int32) atoi(argv[2]), (xmlrpc_int32) atoi(argv[3]));
    }else if(strcmp(argv[1], "iperf") == 0){
        methodName = "iperf.request";

        //sensor id
        //duration
        resultP = xmlrpc_client_call(&env, serverUrl, methodName, "(ii)", (xmlrpc_int32) atoi(argv[2]), (xmlrpc_int32) atoi(argv[3]));
    }else if(strcmp(argv[1], "udp") == 0){
        methodName = "udp.request";

        //sensor id
        //speed
        //size
        //duration
        //dscp
        resultP = xmlrpc_client_call(&env, serverUrl, methodName, "(iiiii)", (xmlrpc_int32) atoi(argv[2]), (xmlrpc_int32) atoi(argv[3]), (xmlrpc_int32) atoi(argv[4]), (xmlrpc_int32) atoi(argv[5]), (xmlrpc_int32) atoi(argv[6]));
    }else if(strcmp(argv[1], "dns") == 0){
        methodName = "dns.request";

        //sensor id
        resultP = xmlrpc_client_call(&env, serverUrl, methodName, "(i)", (xmlrpc_int32) atoi(argv[2]));    
    }else{
        printf("Unrecognised request type\n");
    }
    
    dieIfFaultOccurred(&env);
    
    /* Get our sum and print it out. */
    xmlrpc_read_int(&env, resultP, &success);
    dieIfFaultOccurred(&env);
    
    /* Dispose of our result value. */
    xmlrpc_DECREF(resultP);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);
    
    /* Shutdown our XML-RPC client library. */
    xmlrpc_client_cleanup();

    if(success == 0){   //success
        printf("%s sent to sensor %d\n", methodName, atoi(argv[2]));
        exit(0);
    }else{                //failed
        printf("No reference to socket for id %d\n", atoi(argv[1]));
        exit(1);
    }
}

