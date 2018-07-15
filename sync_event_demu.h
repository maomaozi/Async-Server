#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include <errno.h>
#include <unistd.h>

#include <thread>
#include <atomic>
#include <chrono>
#include <string>

#include <unordered_map>
#include <list>
#include <mutex>

#include <cstdint>

#include "transfer_info_manager.h"
#include "event_handler.h"
#include "str_content.h"
#include "queue_lock_free.h"

#define BLOCK_QUEUE
#define ONE_PIPE_PACKAGE_OUT

#define _RELEASE(x) if(x) delete (x), x=nullptr
#define _RELEASE_ARR(x) if(x) delete[] (x), x=nullptr

const size_t        MAX_CONNECT         = 100000;
const size_t        MAX_DATA_PACKAGE    = 100000;
const __uint32_t    PACKAGE_SIZE        = 4096;
const size_t        THREAD_NUM          = 8;
const size_t        GARBAGE_LIMIT       = 100;

inline bool check_err(int fd, const char *err_string){
    // check a file discriptor (or other sys-call) result is "success"
    // error -> true
    // success -> false
    if(fd == -1){
        //dbg_printf("%s\n", err_string);
#ifdef DEBUG
        perror(err_string);
#endif
        return true;
    }

    return false;
}


class sync_event_demu_tcp
{
  // keep a event loop
  // and then Accept, Read, Write async
  // forward data package to event handler
  // recv data package from event handler and forward to client
public:
    sync_event_demu_tcp(std::string ip, int port) : psocket_fd(nullptr, [](int*){}), pepoll_fd(nullptr, [](int*){})
    {
        dbg_printf("%s\n", DEBUG_SERVER_START_INIT);

        // first check if ip and port is avaliable
        if(ip.empty() || port <= 0 || port > 65535)
        {
            dbg_printf("%s\n", FATAL_SERVER_IP_PORT_NOT_CORRECT);
            return;
        }

        listen_ip = ip;
        listen_port = port;

        // init atomic recoder
        active_connect_num.store(0, std::memory_order_relaxed);
        active_dispatch_num.store(0, std::memory_order_relaxed);

        dbg_printf("%s\n", DEBUG_SERVER_START_ALLOC_MEM);

        // allocate memory for each thread container
        i_pool =                new pre_alloc_pool<connection_info_t>[THREAD_NUM];
        p_pool =                new pre_alloc_pool<data_package_t>[THREAD_NUM];

        // init container
        for (size_t i = 0; i < THREAD_NUM; ++i){
            i_pool[i].init(i, MAX_CONNECT / THREAD_NUM + 1);
            p_pool[i].init(i, MAX_DATA_PACKAGE, PACKAGE_SIZE);
        }

        // communicate to event handler(transfer data package)

#ifdef BLOCK_QUEUE
        data_package_queue =    new BlockinglockFreeQueue<pdata_package_t>[THREAD_NUM];
#else
        data_package_queue =    new lockFreeQueue<pdata_package_t>[THREAD_NUM];
#endif
        //write_cache =           new std::unordered_map<__uint64_t, std::list<pdata_package_t>>[THREAD_NUM];

        // allocate memory for epoll event
        epoll_events =          new epoll_event[MAX_CONNECT];

        epoll_fd = epoll_create(MAX_CONNECT);

        pepoll_fd = std::move(std::unique_ptr<int, void(*)(int *)>(&epoll_fd, [](int *epfd){ close(*epfd); }));

        if(check_err(epoll_fd, FATAL_EPOLL_CAN_NOT_CREATE)) return;

        is_initialized = true;
    }

    ~sync_event_demu_tcp()
    {
        is_running = false;

        _RELEASE_ARR(i_pool);
        _RELEASE_ARR(p_pool);
        _RELEASE_ARR(data_package_queue);
        _RELEASE_ARR(epoll_events);
    }

    void event_loop()
    {
        /// check if server initialzed
        if (!is_initialized){
            dbg_printf("%s\n", FATAL_SERVER_NOT_INITIALIZED);
            return;
        }

        /// is server running, return
        if (is_running){
            dbg_printf("%s\n", FATAL_SERVER_IS_RUNNING);
            return;
        }

        /// first, open a socket file discriptor
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);

        if(check_err(socket_fd, FATAL_SOCKET_CAN_NOT_CREATE)) return;

        /// put socket file discriptor into a unique_ptr
        /// in order to auto release after server destructor
        psocket_fd = std::move(std::unique_ptr<int, void(*)(int *)>(&socket_fd, [](int *socket){ close(*socket); }));


        /// set socket to non-block mode
        int flags = fcntl(socket_fd, F_GETFL, 0);
        fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

        /// fill a listen address struct
        sockaddr_in addr_struct;

        addr_struct.sin_family = AF_INET;
        addr_struct.sin_addr.s_addr = inet_addr(listen_ip.c_str());   //INADDR_ANY
        addr_struct.sin_port = htons(listen_port);

        /// bind socket fd to listen address
        int res = bind(socket_fd, (sockaddr *)&addr_struct, sizeof(addr_struct));

        if(check_err(res, FATAL_SOCKET_CAN_NOT_BIND)) return;

        is_running = true;

        std::thread t = std::thread(std::mem_fn(&sync_event_demu_tcp::connection_resource_gc), this);
        t.detach();

        dbg_printf("%s\n", DEBUG_SERVER_CREATE_DISPATCH_THREAD);
        for(size_t i = 0; i < THREAD_NUM;++i)
        {
            std::thread t = std::thread(std::mem_fn(&sync_event_demu_tcp::dispatch_worker), this, i);
            t.detach();
        }

        /// listen to socket
        dbg_printf("%s%s:%d...\n", DEBUG_SERVER_START_LISTEN, listen_ip.c_str(), listen_port);
        res = listen(socket_fd, 10);

        if(check_err(res, FATAL_SOCKET_CAN_NOT_LISTEN)) return;

        pconnection_info_t pconnection_info = i_pool[0].get();
        pconnection_info->fd = socket_fd;

        // add acceptor to epoll
        add_fd_to_epoll(socket_fd, pconnection_info);

        dbg_printf("%s\n", DEBUG_SERVER_START_EVENT_LOOP);

        while(is_running)
        {
            /// dispatch to thread pool
            /// if there is any worker, wait for it finish
            /// if this thread been scheduled continue, no error will happend
            /// because epoll work in LEVEL trigger mode, event_num will increse
            /// until worker process them
            locker_dispatch.lock();     // writer lock
            if (active_dispatch_num.load(std::memory_order_relaxed))    // test reader lock
            {
                locker_dispatch.unlock();
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            else
            {
                // if got lock, hold writer lock and finish job
                event_num =  epoll_wait(epoll_fd, epoll_events, MAX_CONNECT, -1);
                locker_dispatch.unlock();
                //dbg_printf("%s\n", DEBUG_SERVER_WORK_START_DISPATCH);
            }
        }
    }

    void serve_forever(){
        std::thread t = std::thread(std::mem_fn(&sync_event_demu_tcp::event_loop), this);
        t.detach();
    }

public:
    pdata_package_t asyn_read(int idx=0)
    {
        // fetch a random data package with connection info
        // ensure sequence

        pdata_package_t package = nullptr;

#ifdef ONE_PIPE_PACKAGE_OUT
        idx = 0;
#endif

dbg_printf("info_pool remain %d, package_pool remain %d\n", i_pool[0].remain(), p_pool[0].remain());
dbg_printf("info_pool remain %d, package_pool remain %d\n", i_pool[1].remain(), p_pool[1].remain());

#ifdef BLOCK_QUEUE
        data_package_queue[idx].wait_dequeue(package);
        return package;
#else
        if (data_package_queue[idx].try_dequeue(package))
        {
            package->status = data_package_t::KEEP;
            return package;
        }
        else
        {
            return nullptr;
        }
#endif


    }

    void asyn_write(pdata_package_t &package, bool pass_data_by_ref=false, bool close_connection=false)
    {
        // require a write work, will finish in background
        // ensure sequence

        pconnection_info_t pconnection_info = package->connect;
        package->offset = 0;

        package->status = close_connection ? data_package_t::CLOSE : data_package_t::KEEP;

        // if data pass by pointer store in ref_data
        package->by_ref = pass_data_by_ref;

        // take "package list" iter of map
        locker_connection_write_map.lock();
        // check if connection still alive
        if(write_cache.find(pconnection_info->connection_id) != write_cache.end())
        {
            write_cache[pconnection_info->connection_id].push_back(package);
            locker_connection_write_map.unlock();
            mod_fd_in_epoll(package->connect->fd, package->connect, EPOLLOUT);
        }
        else
        {
            locker_connection_write_map.unlock();
            p_pool[package->owner].put(package);
        }

        package = nullptr;
    }


    pdata_package_t acquire_package(pconnection_info_t connection)
    {
        pdata_package_t package = p_pool[rand() % THREAD_NUM].get();
        package->connect = connection;

        return package;
    }


private:

    void dispatch_worker(int this_thread_id)
    {
        dbg_printf("%s%d\n", DEBUG_SERVER_DISPATCHER_START, this_thread_id);
        while(is_running)
        {
            locker_dispatch.lock();
            // there is no job
            if(!event_num){
                locker_dispatch.unlock();
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            else
            {
                // if got lock and has job, acquire reader lock and release writer lock
                active_dispatch_num.fetch_add(1, std::memory_order_relaxed);
                locker_dispatch.unlock();
            }

            //dbg_printf("%s%d\n", DEBUG_SERVER_DISPATCHER_GOT_WORK, this_thread_id);

            /// handle event
            int work_seg_start = 0;
            int work_seg_end = 0;

            // calculate work segment of thiw thread
            if(event_num >= THREAD_NUM)
            {
                // if there is enough work
                int work_seg_size = event_num / THREAD_NUM;
                work_seg_start = this_thread_id * work_seg_size;
                work_seg_end = (this_thread_id + 1) * work_seg_size;

                /// deal with last thread
                if (this_thread_id == THREAD_NUM - 1)
                {
                    work_seg_end = event_num;
                }
            }
            else if(this_thread_id < event_num)
            {
                work_seg_start = this_thread_id;
                work_seg_end = this_thread_id + 1;
            }

            // forward event to each handler
            for (int i = work_seg_start; i < work_seg_end; ++i){
                if(((pconnection_info_t)(epoll_events[i].data.ptr))->fd == socket_fd)
                {
                    accept_handler(this_thread_id, socket_fd);
                }
                else
                {
                    if(epoll_events[i].events & EPOLLIN)
                    {
                        read_handler(this_thread_id, &epoll_events[i]);
                    }
                    else if(epoll_events[i].events & EPOLLOUT)
                    {
                        write_handler(this_thread_id, &epoll_events[i]);
                    }
                }
            }

            event_num = 0;
            active_dispatch_num.fetch_sub(1, std::memory_order_relaxed);
        }
    }


    bool add_fd_to_epoll(int fd, void *connection_info)
    {
        /// add a new fd to epoll with it own connection data
        /// failed return false
        /// success return true
        static epoll_event event;
        static int res;

        event.events = EPOLLIN;
        event.data.ptr = connection_info;

        res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);

        if(check_err(res, ERROR_EPOLL_CAN_NOT_ADD))
        {
            return false;
        }

        return true;
    };


    bool mod_fd_in_epoll(int fd, void *connection_info, int new_mode)
    {
        static epoll_event event;
        static int res;

        event.events = new_mode;
        event.data.ptr = connection_info;

        res = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);

        if(check_err(res, ERROR_EPOLL_CAN_NOT_MOD))
        {
            return false;
        }

        return true;
    };


    bool del_fd_from_epoll(int fd)
    {
        static epoll_event event;
        static int res;

        res = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event);

        if(check_err(res, ERROR_EPOLL_CAN_NOT_DELETE))
        {
            return false;
        }

        return true;
    };


    void connection_resource_gc(){
        dbg_printf("%s\n", DEBUG_SERVER_GC_THREAD_STARTED);
        pconnection_info_t pconnection_info;

        while (1)
        {
            int garbage_num = 0;

            if ((garbage_num = garbage_can.size_approx()) < GARBAGE_LIMIT)
            {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            dbg_printf("%ssize=%lu\n", DEBUG_SERVER_GC_COLLECT_ONCE, garbage_can.size_approx());

            while (garbage_num--){
                garbage_can.wait_dequeue(pconnection_info);

                locker_connection_write_map.lock();
                auto &package_list = write_cache[pconnection_info->connection_id];

                if (!package_list.empty())
                {
                    for(auto &package: package_list)
                    {
                        p_pool[package->owner].put(package);
                    }

                    // cancle all pending work
                    write_cache.erase(pconnection_info->connection_id);
                }

                locker_connection_write_map.unlock();

                // recycle connection_info
                i_pool[pconnection_info->owner].put(pconnection_info);
            }

        }
    }

    void accept_handler(int thread_id, int fd)
    {
        /// handle an epoll accept event,
        /// if a readable fd == socket_fd, this method will involved
        /// it will allocate a new connection_info_t struct to store connection info
        /// remember put it back after connection close
        /// failed return false
        /// success return true
        dbg_printf("%s%d, connection num=%lu\n", DEBUG_SERVER_DISPATCHER_ACCEPT, thread_id,
        active_connect_num.load(std::memory_order_relaxed));

        if(active_connect_num.load(std::memory_order_relaxed) >= MAX_CONNECT - 10)
        {
            return;
        }

        // first accept the connection
        static sockaddr_in client_addr;
        static socklen_t addr_length = sizeof(client_addr);
        int client_fd = accept(fd, (sockaddr *)&client_addr, &addr_length);
        if(check_err(client_fd, FATAL_SOCKET_CAN_NOT_ACCEPT)) return;

        // then get a connection_info_t struct from manager
        pconnection_info_t pconnection_info = i_pool[thread_id].get();

        // and fill basic information into connection_info_t struct
        pconnection_info->fd = client_fd;
        pconnection_info->ip = inet_ntoa(client_addr.sin_addr);
        pconnection_info->port = ntohs(client_addr.sin_port);
        pconnection_info->connection_id = (ntohl(client_addr.sin_addr.s_addr) << 4) | pconnection_info->port;

        // create a new write list for this connection
        write_cache.emplace(pconnection_info->connection_id, std::move(std::list<pdata_package_t>()));

        // finally add fd to epoll
        if(add_fd_to_epoll(client_fd, pconnection_info)){
            // add success
            active_connect_num.fetch_add(1, std::memory_order_relaxed);
        }else{

            close(client_fd);

            dbg_printf("%s\n", DEBUG_SERVER_CONNECTION_CLOSE);

            // add to epoll failed, recycle resource
            garbage_can.enqueue(pconnection_info);
        }
    };

    void read_handler(int thread_id, epoll_event *event)
    {
        dbg_printf("%s%d\n", DEBUG_SERVER_DISPATCHER_READ, thread_id);

        // first allocate a package_num
        // it can use to get the "instant return data" by event handler
        // or put back by event handler
        pdata_package_t package = p_pool[thread_id].get();
        pconnection_info_t pconnection_info = (pconnection_info_t)(event->data.ptr);

        // try to recv data
        size_t data_size = recv(pconnection_info->fd, package->data, PACKAGE_SIZE, 0);

        // if this connection should close (close by peer or error occured)
        bool need_close_connection = false;

        if(data_size > 0)
        {
            // deliver data package
            package->connect = (pconnection_info_t)event->data.ptr;
            package->data_length = data_size;
#ifdef ONE_PIPE_PACKAGE_OUT
        data_package_queue[0].enqueue(package);
#else
        data_package_queue[thread_id].enqueue(package);
#endif
        }
        else if(data_size == 0)
        {
            need_close_connection = true;
        }
        else if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            need_close_connection = true;
        }

        if (need_close_connection)
        {
            // socket is closed

            // put back this data package
            p_pool[package->owner].put(package);

            active_connect_num.fetch_sub(1, std::memory_order_relaxed);

            // close fd and remove this fd from epoll
            del_fd_from_epoll(pconnection_info->fd);
            close(pconnection_info->fd);

            // recycle
            garbage_can.enqueue(pconnection_info);
        }
    };

    void write_handler(int thread_id, epoll_event *event)
    {
        // when a fd is writable, fetch a data to write

        // get connection info
        dbg_printf("%s%d\n", DEBUG_SERVER_DISPATCHER_WRITE, thread_id);
        pconnection_info_t pconnection_info = (pconnection_info_t)event->data.ptr;

        // take "package list" iter of map
        locker_connection_write_map.lock();
        auto &package_list = write_cache[pconnection_info->connection_id];

        // if there is really a list
        if(!package_list.empty())
        {
            // found the list
            // take first package in list
            pdata_package_t package = package_list.front();
            locker_connection_write_map.unlock();

            int sent_length = send(pconnection_info->fd, (package->by_ref ? package->ref_data : package->data) + package->offset, package->data_length, 0);

            if (sent_length >= 0)
            {
                // some or all data is sent successfully

                // only part of data is sent
                if(sent_length < package->data_length)
                {
                    package->data_length -= sent_length;
                    package->offset += sent_length;
                }
                else
                {
                    // all data is sent
                    // recycle package
                    p_pool[package->owner].put(package);
                    // remove this package from front
                    locker_connection_write_map.lock();
                    package_list.pop_front();
                    locker_connection_write_map.unlock();
                }
            }

            if ((sent_length < 0 && errno != EAGAIN && errno != EWOULDBLOCK) ||
                package->status == data_package_t::CLOSE)
            {
                // connection is close by peer or request close by client

                // collection all package pending in write list and connection_info
                // if connection close in read this will never trigger twice
                active_connect_num.fetch_sub(1, std::memory_order_relaxed);

                // close fd and remove this fd from epoll
                del_fd_from_epoll(pconnection_info->fd);
                close(pconnection_info->fd);

                dbg_printf("%s\n", DEBUG_SERVER_CONNECTION_CLOSE);
                garbage_can.enqueue(pconnection_info);
            }
        }
        else
        {
            locker_connection_write_map.unlock();
            // continue listen
            mod_fd_in_epoll(pconnection_info->fd, pconnection_info, EPOLLIN);
        }
    };


private:

    // each THREAD has its own connection_info_pool and package_pool
    // connection_info_pool create and recycle per-connection info
    pre_alloc_pool<connection_info_t> *i_pool = nullptr;
    // package_pool create and recycle some data package
    // package deliver between "sync_event_demu" and "event_handler"
    pre_alloc_pool<data_package_t> *p_pool = nullptr;

    // record all connected fd num
    std::atomic<size_t> active_connect_num;

    // per-thread pipe connect to event handler
#ifdef BLOCK_QUEUE
    BlockinglockFreeQueue<pdata_package_t> *data_package_queue = nullptr;
#else
    lockFreeQueue<pdata_package_t> *data_package_queue = nullptr;
#endif

    BlockinglockFreeQueue<pconnection_info_t> garbage_can;

    // use to cache package need to write_handler
    std::unordered_map<__uint64_t, std::list<pdata_package_t>> write_cache;

    // real thread id to index
    std::unordered_map<std::thread::id, int> thread_id_map;

    // active_dispatch_num record how many event is now under porcessing
    std::atomic<size_t> active_dispatch_num;

    // mutex
    std::mutex locker_dispatch;
    std::mutex locker_connection_write_map;
    std::mutex locker_connection_write_list;

    // how many active event number
    int event_num = 0;
    epoll_event *epoll_events = nullptr;

    // acceptor
    int socket_fd = -1;
    // epoll file discriptor
    int epoll_fd = -1;

    // use to auto release socket_fd and epoll_fd
    std::unique_ptr<int, void(*)(int *)> psocket_fd;
    std::unique_ptr<int, void(*)(int *)> pepoll_fd;

    // if every member initialzed correctly and memory and allocated
    bool is_initialized = false;
    // if main-loop is running
    bool is_running = false;

    // which ip and port server is now(or will) listen on
    std::string listen_ip;
    int listen_port = 0;
};
