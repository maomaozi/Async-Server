#include <string.h>
#include <stdio.h>
#include <string>
#include <unordered_map>

#include "async_event_processor.h"

const char *response_header = "HTTP/1.1 200 OK\r\n"\
"Server: maoserver/0.0.1\r\n"\
"Content-Type: text/html; charset=utf-8\r\n"\
"Content-Length: %d\r\n"\
"Connection: close\r\n\r\n";


struct conn_states
{
    conn_states()
    {
        buffer = new char[1000];
        buffer[0] = 0;
    }
    
    ~conn_states()
    {
        //delete []buffer;
    }
    
    void release()
    {
        delete []buffer;
    }
    
    int len = -1;
    bool is_header_finished = false;
    char *content_ptr = nullptr;
    char *buffer = nullptr;
};


void echo_worker(async_event_processor_tcp &processor, int idx)
{
    
    std::unordered_map<__uint64_t, conn_states> states;

    while(1)
    {
        pdata_package_t data_package = processor.asyn_read(idx);
        pconnection_info_t connection = data_package->connect;

        conn_states &this_conn = states[connection->connection_id];
        
        // if more header to recv
        if(!this_conn.is_header_finished)
        {
            strcat(this_conn.buffer, data_package->data);
            // start of main text
            char *text_start = nullptr;
            
            // "=" not "=="
            if(text_start = strstr(this_conn.buffer, "\r\n\r\n"))
            {
                // all header finished, then find content length
                // assign and compare, use "=" not "=="
                char *length_start = nullptr;
                if ((length_start = strstr(this_conn.buffer, "Content-Length")) || 
                (length_start = strstr(this_conn.buffer, "content-length")))
                {
                    this_conn.len = strtod(strstr(length_start, ": ") + 2, nullptr);
                }
                else
                {
                    // no data, just echo headers
                    this_conn.len = 0;
                }
                
                this_conn.is_header_finished = true;
                
                // 1. send header (not includes \r\n)
                int len_client_header = text_start - this_conn.buffer;
                // create header, len has two part, client_header and client_text
                sprintf(data_package->data, response_header, this_conn.len + len_client_header);       
                data_package->data_length = strlen(data_package->data);

                processor.asyn_write(data_package, false, false);
                
                // 2. then echo client header
                data_package = processor.acquire_package(connection);
                if(this_conn.len)
                {
                    // has more text data
                    // echo client headers
                    data_package->ref_data = this_conn.buffer;
                    data_package->data_length = len_client_header;
                    processor.asyn_write(data_package, true, false);
                    
                    // and echo part of data, if exists
                    text_start += 4;        // skip \r\n\r\n
                    int len_text_data = strlen(text_start);
                    if(len_text_data)
                    {
                        data_package = processor.acquire_package(connection);
                        
                        data_package->ref_data = text_start;
                        data_package->data_length = strlen(text_start);
                        
                        this_conn.len -= len_text_data;
                        
                        processor.asyn_write(data_package, true, false, [=](void*x){delete []this_conn.buffer;});
                    }
                    
                }
                else
                {
                    //echo client headers and close
                    data_package->ref_data = this_conn.buffer;
                    // send back headers
                    data_package->data_length = len_client_header;
                    processor.asyn_write(data_package, true, true, [](void *x){delete []x;});
                    states.erase(connection->connection_id);
                }

            }
            else
            {
                processor.release_package(data_package);
            }
        }
        else
        {
            this_conn.len -= data_package->data_length;
            
            if(this_conn.len <= 0)
            {
                processor.asyn_write(data_package, false, true);
                states.erase(connection->connection_id);
            }
            else
            {            
                processor.asyn_write(data_package, false, false);
            }
        }
        
    }
}

int main()
{
    async_event_processor_tcp processor("0.0.0.0", 8081);
    processor.serve_forever();

    std::thread t[8];

    for (size_t i = 0; i < 8; ++i)
    {
        t[i] = std::move(std::thread(echo_worker, std::ref(processor), i));
    }

    for (size_t i = 0; i < 8; ++i)
    {
        t[i].join();
    }
}
