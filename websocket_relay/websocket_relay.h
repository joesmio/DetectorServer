
#include <stdint.h>
#include <vector>
#include <string>
# include <mutex>
#ifdef PLATFORM_WINDOWS
// TODO: Likely we should move Linux over to std::thread as well.
# include <thread>
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <pthread.h>    // needed for mutex and pthread
#endif

class ListenerSetupData;

bool websocket_relay_start();
void websocket_relay_broadcast(const void* data, size_t length, bool is_ascii);
void websocket_relay_send(size_t slot, const void* data, size_t length, bool is_ascii);
size_t websocket_relay_get_num_listener_slots();
ListenerSetupData* websocket_relay_get_listener(int slot);
void websocket_relay_request_disconnect(int slot);

ListenerSetupData* new_listener_data();
void delete_listener_data(ListenerSetupData* listener);

#ifndef PLATFORM_WINDOWS
#define SOCKET int
#define INVALID_SOCKET -1
#endif

struct thread_local_data {
    SOCKET sd;              // The connection
    size_t buffer_size;     // Size of the receive buffer
    unsigned char*  buffer; // The receive buffer
    bool is_websocket;      // true if this connection is a websocket
    bool any_data_received; // true after we've received our first data.
    bool disconnect_requested; // when possible, disconnect this socket.
    int  consecutive_send_fail_count; // the number of times in a row we've failed to send.
#ifdef PLATFORM_WINDOWS
	std::thread* rcv_thread;// The thread handling the receiving
#else
	pthread_t* pthread;     // The thread handling the receiving
#endif

    ListenerSetupData* listener;
};

void handle_ascii_receive(const char *data, size_t length, thread_local_data* tld);
void handle_binary_receive(const unsigned char *data, size_t length, thread_local_data* tld);

extern std::mutex socket_mutex;         // lock this when usin the socket
extern std::mutex listener_list_mutex;   // Lock this before modifying or using the listeners list
