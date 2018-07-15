#include <string.h>
#include <stdio.h>
#include <string>

#include "sync_event_demu.h"


const char *response_header = "HTTP/1.1 200 OK\r\n"\
"Server: maoserver/0.0.1\r\n"\
"Date: Fri, 13 Jul 2018 11:44:53 GMT\r\n"\
"Content-Type: text/html; charset=utf-8\r\n"\
"Content-Length: 4000\r\n"\
"Connection: close\r\n\r\n";

char response_data[4001];

/*

BlockinglockFreeQueue<int> qqueue[2];
std::atomic<size_t> ccounter1;
std::atomic<size_t> ccounter2;

void sendq(int idx){
    while(1){
        qqueue[idx].enqueue(1);
        ccounter1.fetch_add(1, std::memory_order_relaxed);
    }
}

void recvq(int idx){
    int a;
    while(1){
        qqueue[idx].wait_dequeue(a);
        ccounter2.fetch_add(1, std::memory_order_relaxed);
    }
}



int main(){


    for (int i = 0; i < 8; ++i){
        std::thread t(recvq, i % 2);
        t.detach();
    }

    for (int i = 0; i < 8; ++i){
        std::thread t(sendq, i % 2);
        t.detach();
    }

    while(1){
        printf("In %lu, Out %lu\n", ccounter1.load(), ccounter2.load());
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

*/


 int main(){
     memset(response_data, 'A', sizeof(char) * 4000);
     response_data[4000] = '\0';

     sync_event_demu_tcp sed("127.0.0.1", 8081);
     sed.serve_forever();

     while(1){

         pdata_package_t data_package = sed.asyn_read();
         pconnection_info_t connection = data_package->connect;

         //printf("%u %s\n", data_package->data_length, data_package->data);
         sprintf(data_package->data, "%s", response_header);
         data_package->data_length = strlen(response_header);
         sed.asyn_write(data_package, false, false);

         pdata_package_t package = sed.acquire_package(connection);
         //sprintf(package->data, "%s", response_data);

         package->ref_data = response_data;
         package->data_length = 4000;

         sed.asyn_write(package, true, true);

     }

     return 0;
 }
