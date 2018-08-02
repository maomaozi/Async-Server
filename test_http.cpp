#include <string.h>
#include <stdio.h>
#include <string>

#include "async_event_processor.h"


const char *response_header = "HTTP/1.1 200 OK\r\n"\
"Server: maoserver/0.0.1\r\n"\
"Date: Fri, 13 Jul 2018 11:44:53 GMT\r\n"\
"Content-Type: text/html; charset=utf-8\r\n"\
"Content-Length: 4000\r\n"\
"Connection: close\r\n\r\n";

char response_data[4001];


void worker(async_event_processor_tcp &processor, int idx)
{
    while(1)
    {
        pdata_package_t data_package = processor.asyn_read(idx);
        pconnection_info_t connection = data_package->connect;

        sprintf(data_package->data, "%s", response_header);
        data_package->data_length = strlen(response_header);
        processor.asyn_write(data_package, false, false);
        
        pdata_package_t package = processor.acquire_package(connection);

        package->ref_data = response_data;
        package->data_length = 4000;

        processor.asyn_write(package, true, true);
    }
}


 int main()
 {
    memset(response_data, 'A', sizeof(char) * 4000);
    response_data[4000] = '\0';

    async_event_processor_tcp processor("0.0.0.0", 8081);
    processor.serve_forever();
     
    std::thread t[8];
     
    for(int i = 0; i < 8; ++i)
    {
         t[i] = std::thread(worker, std::ref(processor), i);
         
    }
    
    for(int i = 0; i < 8; ++i)
    {
         t[i].join();
         
    }

     return 0;
 }
