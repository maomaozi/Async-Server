#include <cstdio>

#define DEBUG

#ifdef DEBUG
    #define dbg_printf(...) printf(__VA_ARGS__)
#else
    #define dbg_printf(...)
#endif


//======================================================
#define DEBUG_SERVER_START_INIT                     "Debug: Starting server..."
#define DEBUG_SERVER_CREATE_DISPATCH_THREAD         "Debug: Creating dispatch threads..."
#define DEBUG_SERVER_START_LISTEN                   "Debug: Server Listen on "
#define DEBUG_SERVER_START_ALLOC_MEM                "Debug: Server Init data..."
#define DEBUG_SERVER_START_EVENT_LOOP               "Debug: Start event loop..."
#define DEBUG_SERVER_DISPATCHER_START               "Debug: Dispatcher start! idx="
#define DEBUG_SERVER_WORK_START_DISPATCH            "Debug: Got work! Start dispatch..."
#define DEBUG_SERVER_DISPATCHER_GOT_WORK            "Debug: Process work on dispatcher idx="
#define DEBUG_SERVER_GC_THREAD_STARTED              "Debug: GC thread started"
#define DEBUG_SERVER_GC_COLLECT_ONCE                "Debug: GC collected resource"

#define DEBUG_SERVER_DISPATCHER_ACCEPT              "Debug: Accept connection in dispatcher idx="
#define DEBUG_SERVER_DISPATCHER_READ                "Debug: Read data from client in dispatcher idx="
#define DEBUG_SERVER_DISPATCHER_WRITE               "Debug: Write data to client in dispatcher idx="
#define DEBUG_SERVER_CONNECTION_CLOSE               "Debug: Connection close"


//======================================================

#define FATAL_SERVER_IP_PORT_NOT_CORRECT    "Fatal: Error listen IP or port"
#define FATAL_SERVER_NOT_INITIALIZED        "Fatal: Server is not initialzed correctly"
#define FATAL_SERVER_IS_RUNNING             "Fatal: Server is already running"

#define FATAL_SOCKET_CAN_NOT_CREATE    "Fatal: Can't create socket"
#define FATAL_SOCKET_CAN_NOT_BIND      "Fatal: Can't bind socket"
#define FATAL_SOCKET_CAN_NOT_LISTEN    "Fatal: Can't listen socket"
#define FATAL_SOCKET_CAN_NOT_ACCEPT    "Error: Can't accept connect"

#define FATAL_EPOLL_CAN_NOT_CREATE     "Fatal: Can't create epoll"

#define ERROR_EPOLL_CAN_NOT_ADD        "Error: Can't add new fd to epoll"
#define ERROR_EPOLL_CAN_NOT_MOD        "Error: Can't modified fd in epoll"
#define ERROR_EPOLL_CAN_NOT_DELETE     "Error: Can't delete fd in epoll"
