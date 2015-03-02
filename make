#comiple server
gcc -c -I. -Ixmlrpc-c-1.33.16/include -Ixmlrpc-c-1.33.16/include $(mysql_config --cflags) src/server.c $(mysql_config --libs)
gcc -o bin/server $(mysql_config --cflags) server.o $(mysql_config --libs) xmlrpc-c-1.33.16/src/libxmlrpc_server_abyss.a xmlrpc-c-1.33.16/src/libxmlrpc_server.a xmlrpc-c-1.33.16/lib/abyss/src/libxmlrpc_abyss.a  -lpthread xmlrpc-c-1.33.16/src/libxmlrpc.a xmlrpc-c-1.33.16/lib/expat/xmlparse/libxmlrpc_xmlparse.a xmlrpc-c-1.33.16/lib/expat/xmltok/libxmlrpc_xmltok.a xmlrpc-c-1.33.16/lib/libutil/libxmlrpc_util.a

#compile rpc client
gcc -c -I. -Ixmlrpc-c-1.33.16/include -Ixmlrpc-c-1.33.16/include src/rpc_request.c
gcc -o bin/rpc_request rpc_request.o xmlrpc-c-1.33.16/src/libxmlrpc_client.a xmlrpc-c-1.33.16/src/libxmlrpc.a xmlrpc-c-1.33.16/lib/expat/xmlparse/libxmlrpc_xmlparse.a xmlrpc-c-1.33.16/lib/expat/xmltok/libxmlrpc_xmltok.a xmlrpc-c-1.33.16/lib/libutil/libxmlrpc_util.a -L/usr/lib/x86_64-linux-gnu -lcurl   

rm *.o
