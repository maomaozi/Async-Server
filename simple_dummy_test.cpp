#include <string.h>
#include <stdio.h>

#include "sync_event_demu.h"


const char *response = "HTTP/1.1 200 OK\r\n"\
"Server: maoserver/0.0.1\r\n"\
"Date: Fri, 13 Jul 2018 11:44:53 GMT\r\n"\
"Content-Type: text/html; charset=utf-8\r\n"\
"Content-Length: 11\r\n"\
"Connection: close\r\n\r\n"\
"Hello world";


 int main(){
     sync_event_demu_tcp sed("127.0.0.1", 8081);
     sed.serve_forever();

     while(1){

         pdata_package_t data_package = sed.asyn_read(0);

         if(data_package)
         {
             printf("%u %s\n", data_package->data_length, data_package->data);
             sprintf(data_package->data, "%s", response);
             data_package->data_length = strlen(response);
             sed.asyn_write(0, data_package, true);
         }
         else
         {
             printf("No data\n");
         }
     }

     return 0;
 }
