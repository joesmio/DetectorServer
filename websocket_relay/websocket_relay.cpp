
#include <stdio.h>
#include <stdlib.h>
#ifdef PLATFORM_WINDOWS
# pragma comment(lib, "Ws2_32.lib")
typedef long long ssize_t;
#else
# include <unistd.h>
# include <sys/socket.h> // Needed for the socket functions
# include <netdb.h>      // Needed for the socket functions
# define closesocket close
#endif
#include <iostream>
#include <string>
#include <vector>
#include <cstring>      // Needed for memset
#include "sha1.h"
#include "base64.h"
#include "websocket_relay.h"

extern "C" {
    size_t tinfl_decompress_mem_to_mem(void *pOut_buf, size_t out_buf_len, const void *pSrc_buf, size_t src_buf_len, int flags);
}
#define TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ((size_t)(-1))
size_t decompression_buffer_size = 1024 * 1024;
void* decompression_buffer = NULL;



std::mutex socket_mutex;
std::mutex listener_list_mutex;

std::vector<thread_local_data*> connected_sockets;





/*
 *
 * WebSocket Packets
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-------+-+-------------+-------------------------------+
 *     |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 *     |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 *     |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 *     | |1|2|3|       |K|             |                               |
 *     +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 *     |     Extended payload length continued, if payload len == 127  |
 *     + - - - - - - - - - - - - - - - +-------------------------------+
 *     |                               |Masking-key, if MASK set to 1  |
 *     +-------------------------------+-------------------------------+
 *     | Masking-key (continued)       |          Payload Data         |
 *     +-------------------------------- - - - - - - - - - - - - - - - +
 *     :                     Payload Data continued ...                :
 *     + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 *     |                     Payload Data continued ...                |
 *     +---------------------------------------------------------------+
 *
 * Opcode values
 *  *  %x0 denotes a continuation frame
 *  *  %x1 denotes a text frame
 *  *  %x2 denotes a binary frame
 *  *  %x3-7 are reserved for further non-control frames
 *
 *  *  %x8 denotes a connection close
 *  *  %x9 denotes a ping
 *  *  %xA denotes a pong
 *  *  %xB-F are reserved for further control frames
 *
 * Examples:
 *  Header bytes are 0x81, 0x85 for the five-letter message "Ping!"
 *  Header bytes are 0x81, 0x86 for the six-letter message "Ping!!"
 */

enum {
    FRAME_HEADER_BINARY = 0x82,
    FRAME_HEADER_ASCII  = 0x81
};

static size_t make_ws_frame_header(unsigned char *frame_header, size_t frame_bytes, bool is_binary)
{
    size_t head_size = 2;

    if (is_binary)
        frame_header[0] = FRAME_HEADER_BINARY;
    else
        frame_header[0] = FRAME_HEADER_ASCII;
    if (frame_bytes > 65535)
    {
        frame_header[1] = 127;
        uint64_t frame_bytes64 = frame_bytes;
        // Write the size MSB-first (big-endian)
        frame_header[head_size + 0] = (unsigned char)(frame_bytes64 >> (7 * 8));
        frame_header[head_size + 1] = (unsigned char)(frame_bytes64 >> (6 * 8));
        frame_header[head_size + 2] = (unsigned char)(frame_bytes64 >> (5 * 8));
        frame_header[head_size + 3] = (unsigned char)(frame_bytes64 >> (4 * 8));
        frame_header[head_size + 4] = (unsigned char)(frame_bytes64 >> (3 * 8));
        frame_header[head_size + 5] = (unsigned char)(frame_bytes64 >> (2 * 8));
        frame_header[head_size + 6] = (unsigned char)(frame_bytes64 >> (1 * 8));
        frame_header[head_size + 7] = (unsigned char)(frame_bytes64 >> (0 * 8));
        head_size += 8;
    }
    else if (frame_bytes > 125)
    {
        frame_header[1] = 126;
        // Write the size MSB-first (big-endian)
        frame_header[head_size + 0] = (unsigned char)(frame_bytes >> (1 * 8));
        frame_header[head_size + 1] = (unsigned char)(frame_bytes >> (0 * 8));
        head_size += 2;
    }
    else
    {
        frame_header[1] = (unsigned char)frame_bytes;
    }
    return (head_size);
}



static bool handle_ws_receive(unsigned char *frame_header, size_t frame_bytes, thread_local_data* tld)
{
    bool is_binary;

    if (frame_header[0] == FRAME_HEADER_BINARY)
        is_binary = true;
    else if (frame_header[0] == FRAME_HEADER_ASCII)
        is_binary = false;
    else
        return false;

    size_t head_size = 2;
    uint64_t payload_frame_bytes = 0;

    if ((frame_header[1] & 127) == 127)
    {
        printf("101\n");
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 0] << (7 * 8);
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 1] << (6 * 8);
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 2] << (5 * 8);
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 3] << (4 * 8);
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 4] << (3 * 8);
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 5] << (2 * 8);
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 6] << (1 * 8);
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 7] << (0 * 8);
        head_size += 8;
    }
    else if ((frame_header[1] & 127) == 126)
    {
        printf("102\n");
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 0] << (1 * 8);
        payload_frame_bytes |= (uint64_t)frame_header[head_size + 1] << (0 * 8);
        head_size += 2;
    }
    else
    {
        printf("103\n");
        payload_frame_bytes = frame_header[1] & 0x7f;
    }

    const unsigned char* mask_ptr = NULL;
    if (frame_header[1] & 0x80)
    {
        mask_ptr = frame_header + head_size;
        head_size += 4;
    }

    // Check the sizes
    if (payload_frame_bytes + head_size != frame_bytes)
    {
        printf("websocket size mismatch: %d + %d != %d\n", (int)payload_frame_bytes, (int)head_size, (int)frame_bytes);
        printf("%d %d %d %d %d %d %d %d\n", frame_header[0], frame_header[1], frame_header[2], frame_header[3], frame_header[4], frame_header[5], frame_header[6], frame_header[7]);
        printf("%02x %02x %02x %02x %02x %02x %02x %02x\n", frame_header[0], frame_header[1], frame_header[2], frame_header[3], frame_header[4], frame_header[5], frame_header[6], frame_header[7]);
        return false;
    }

    unsigned char *payload_ptr = frame_header + head_size;

    // If there's a mask, apply it
    if (mask_ptr)
    {
        for (uint64_t i = 0; i < payload_frame_bytes; ++i)
            payload_ptr[i] ^= mask_ptr[i & 3];
    }

    printf("%d %d %d %d %d %d %d %d\n", frame_header[0], frame_header[1], frame_header[2], frame_header[3], frame_header[4], frame_header[5], frame_header[6], frame_header[7]);
    printf("%02x %02x %02x %02x %02x %02x %02x %02x\n", frame_header[0], frame_header[1], frame_header[2], frame_header[3], frame_header[4], frame_header[5], frame_header[6], frame_header[7]);

    if (is_binary)
    {
        handle_binary_receive(payload_ptr, payload_frame_bytes, tld);
    }
    else
    {
        // TODO: this should really be done in a separate buffer.
        payload_ptr[payload_frame_bytes] = '\0';
        handle_ascii_receive((const char*)payload_ptr, payload_frame_bytes, tld);
    }

    return true;
}

int send_ws_ascii(SOCKET sd, const void *buffer, size_t length)
{
    std::vector<unsigned char> buffer_vec(16 + length);
    unsigned char* ws_header = &buffer_vec[0];
    size_t ws_header_size = make_ws_frame_header(ws_header, length, false);
    memcpy(ws_header + ws_header_size, buffer, length);
#ifdef VERBOSE
    printf("sending ascii: %lu bytes: %s\n", length, (char *)buffer);
#endif

    size_t total_size = ws_header_size + length;
    int send_result = send(sd, (const char*)ws_header, (int)total_size, 0);
    return send_result;
}

size_t send_ws_binary(SOCKET sd, const void *buffer, size_t length)
{
    std::vector<unsigned char> buffer_vec(16 + length);
    unsigned char* ws_header = &buffer_vec[0];
    size_t ws_header_size = make_ws_frame_header(ws_header, length, true);
    memcpy(ws_header + ws_header_size, buffer, length);
#ifdef VERBOSE
    printf("sending binary: %lu bytes\n", length);
#endif

    size_t total_size = ws_header_size + length;
    int send_result = send(sd, (const char*)ws_header, (int)total_size, 0);
    return send_result;
}

size_t websocket_relay_get_num_listener_slots()
{
    return connected_sockets.size();
}

ListenerSetupData* websocket_relay_get_listener(int slot)
{	
	__try {
		if (connected_sockets[slot])
			return connected_sockets[slot]->listener;
		return NULL;
	}
	__except (
		// Why is this crashing? JS 24/05/19
		GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
		? EXCEPTION_EXECUTE_HANDLER
		: EXCEPTION_CONTINUE_SEARCH) {
		std::cerr << "OMG!\n";
		return NULL;
	}
}

void websocket_relay_send(size_t slot, const void* data, size_t length, bool is_ascii)
{
//printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> locker(socket_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    thread_local_data* client_tld = connected_sockets[slot];
    if (client_tld && client_tld->sd != INVALID_SOCKET)
    {
//        printf(" Relay to listener %lu (%ld bytes)\n", slot, length);

        int send_result = -1;
        if (is_ascii)
        {
            if (client_tld->is_websocket)
                send_result = send_ws_ascii(client_tld->sd, data, length);
            else
                send_result = send(client_tld->sd, (const char*)data, (int)length, 0);
        }
        else
        {
            if (client_tld->is_websocket)
                send_result = (int)send_ws_binary(client_tld->sd, data, length);
            else
                send_result = send(client_tld->sd, (const char*)data, (int)length, 0);
        }
        if (send_result < 0)
        {
            int max_fail_count_to_disconnect = 5;
            client_tld->consecutive_send_fail_count++;
            if (client_tld->consecutive_send_fail_count >= max_fail_count_to_disconnect)
            {
                printf("Send failed %d times, disconnecting...\n", client_tld->consecutive_send_fail_count);
                client_tld->disconnect_requested = true;
                // Also force it closed to break the recv loop.
                if (client_tld->sd != INVALID_SOCKET)
                {
                    closesocket(client_tld->sd);
                    client_tld->sd = INVALID_SOCKET;
                }
            }
        }
        else
            client_tld->consecutive_send_fail_count = 0;
    }
}

void websocket_relay_broadcast(const void* data, size_t length, bool is_ascii)
{
    for (size_t slot = 0; slot < connected_sockets.size(); ++slot)
    {
        if (connected_sockets[slot])
            websocket_relay_send(slot, data, length, is_ascii);
    }
}

static size_t count_connected_sockets(const char* message = NULL)
{
    size_t result = 0;
    for (size_t i = 0; i < connected_sockets.size(); ++i)
    {
        if (connected_sockets[i])
            result++;
    }
    if (message)
        printf("%s: There are %lu sockets connected, vec=%p.\n", message, (unsigned long)result, &connected_sockets);
    return result;
}

static void add_connected_socket(thread_local_data* tld)
{
//printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> listener_locker(listener_list_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);
//printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> locker(socket_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    bool found_slot = false;
    
    for (size_t i = 0; i < connected_sockets.size() && !found_slot; ++i)
    {
        if (connected_sockets[i] == NULL)
        {
            connected_sockets[i] = tld;
            found_slot = true;
        }
    }
    if (!found_slot)
    {
        connected_sockets.push_back(tld);
    }
    count_connected_sockets();
}

static void remove_connected_socket(thread_local_data* tld)
{
//printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> listener_locker(listener_list_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);
//printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> locker(socket_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    for (size_t i = 0; i < connected_sockets.size(); ++i)
    {
        if (connected_sockets[i] == tld)
            connected_sockets[i] = NULL;
    }
    count_connected_sockets();
}

static std::string get_response_field(const char* label, char* packet)
{
    std::string result;
    const char* cp = strstr(packet, label);
    if (cp)
    {
        char value[1024];
        value[0] = 0;
        sscanf(cp + strlen(label), " %s ", value);
        result = value;
    }
    return result;
}

static bool websocket_server_initialize_socket(SOCKET* out_socketfd, struct addrinfo** out_host_info_list, const char* port_num_str)
{
    int             status;
    struct addrinfo host_info;
    struct addrinfo *host_info_list;
    SOCKET          socketfd;

#ifdef PLATFORM_WINDOWS
	WSADATA wsa_data;
	// Initialize Winsock
	int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
	if (result != 0) {
		printf("WSAStartup failed: %d\n", result);
		return false;
	}
#endif
    decompression_buffer = malloc(decompression_buffer_size);

    printf("Opening socket on port %s...\n", port_num_str);
    memset(&host_info, 0, sizeof host_info);
    host_info.ai_family = AF_UNSPEC;     // IP version not specified. Can be both.
    host_info.ai_socktype = SOCK_STREAM; // Use SOCK_STREAM for TCP or SOCK_DGRAM for UDP.
    host_info.ai_flags = AI_PASSIVE;     // IP Wildcard
#ifdef PLATFORM_WINDOWS
	status = getaddrinfo("0.0.0.0", port_num_str, &host_info, &host_info_list);
#else
	status = getaddrinfo(NULL, port_num_str, &host_info, &host_info_list);
#endif
    if (status != 0)
    {
        fprintf(stderr, "getaddrinfo() error %d at %s:%d\n", status, __FILE__, __LINE__);
#ifdef PLATFORM_WINDOWS
        WSACleanup();
#endif
		return false;
    }

    socketfd = socket(host_info_list->ai_family, host_info_list->ai_socktype, host_info_list->ai_protocol);
    if (socketfd == INVALID_SOCKET)
    {
        fprintf(stderr, "socket() error at %s:%d\n", __FILE__, __LINE__);
#ifdef PLATFORM_WINDOWS
        WSACleanup();
#endif
		return false;
    }

    // Set a timeout in case we have send failures
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (setsockopt (socketfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
        printf("setsockopt (time limit on send) failed\n");

    int yes = 1;
    status = setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(int));
    status = bind(socketfd, host_info_list->ai_addr, (int)host_info_list->ai_addrlen);
    if (status == -1)
    {
        fprintf(stderr, "bind() error at %s:%d\n", __FILE__, __LINE__);
#ifdef PLATFORM_WINDOWS
        WSACleanup();
#endif
		return false;
    }

    *out_socketfd = socketfd;
    *out_host_info_list = host_info_list;
    return true;
}

static bool websocket_server_wait_for_connection(SOCKET socketfd, SOCKET* out_new_sd)
{
    printf("Listening for connection...\n");
printf("at line %d\n", __LINE__);
	int status = listen(socketfd, 5);
	printf("at line %d\n", __LINE__);
	if (status == -1)
    {
		printf("at line %d\n", __LINE__);
		fprintf(stderr, "listen() error at %s:%d\n", __FILE__, __LINE__);
        return false;
    }

	printf("at line %d\n", __LINE__);
	struct sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);
	printf("at line %d\n", __LINE__);
	SOCKET new_sd = accept(socketfd, (struct sockaddr *)&their_addr, &addr_size);
	printf("at line %d\n", __LINE__);
	if (new_sd == INVALID_SOCKET)
    {
        printf("...failed accept()\n");
        fprintf(stderr, "accept() error at %s:%d\n", __FILE__, __LINE__);
        return false;
    }

    printf("Connected ok.\n");

    *out_new_sd = new_sd;
    return true;
}


// Return true if we should keep listening, false if we're done.
static bool websocket_wait_for_data(thread_local_data* tld)
{
   ssize_t bytes_received;

    // Wait here until there's new data
    if (tld->sd != INVALID_SOCKET)
        bytes_received = recv(tld->sd, (char*)tld->buffer, (int)tld->buffer_size, 0);

    // When we get data, we'll lock the mutex to deal with it.
//printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> locker(socket_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);

    if (bytes_received == 0)
    {
        printf("host shut down,\n");
        return false;
    }
    if (bytes_received == -1)
    {
        fprintf(stderr, "recv() error at %s:%d\n", __FILE__, __LINE__);
        return false;
    }
    tld->buffer[bytes_received] = '\0';

    bool handled = false;

    if (!tld->any_data_received)
    {
        tld->any_data_received = true;
        // No data received yet, so check for websocket headers, etc.
        // If we get a connection key, send the correct response.
        std::string key = get_response_field("Sec-WebSocket-Key:", (char*)(tld->buffer));
        if (!key.empty())
        {
            printf("WebSocket key request (%ld bytes): '%s'\n", (long)bytes_received, tld->buffer);

            tld->is_websocket = true;
            printf("Got websocket connect, key = %s\n", key.c_str());

            // Construct the response string
            std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            key.append(magic);
            unsigned char hash[20];
            sha1::calc(key.c_str(), (int)key.length(), hash);
            std::string encoded_key = base64_encode(reinterpret_cast<const unsigned char*>(hash), 20);
            std::string response;

            response += "HTTP/1.1 101 Web Socket Protocol Handshake\r\n";
            response += "Server: websocket relay\r\n";
            response += "Upgrade: WebSocket\r\n";
            response += "Access-Control-Allow-Origin: http://www.websocket.org\r\n";
            response += "Access-Control-Allow-Credentials: true\r\n";
            response += "Sec-WebSocket-Accept: ";
            response += encoded_key;
            response += "\r\n";
            response += "Connection: Upgrade\r\n";
            response += "Access-Control-Allow-Headers: content-type\r\n";
            response += "Access-Control-Allow-Headers: authorization\r\n";
            response += "Access-Control-Allow-Headers: x-websocket-extensions\r\n";
            response += "Access-Control-Allow-Headers: x-websocket-version\r\n";
            response += "Access-Control-Allow-Headers: x-websocket-protocol\r\n";
            response += "\r\n";
            ssize_t bytes_sent = -1;
            if (tld->sd != INVALID_SOCKET)
                bytes_sent = send(tld->sd, response.c_str(), (int)response.length(), 0);
            handled = true;
        }
    }

    if (!handled)    // Standard data, so relay it.
    {
        if (tld->is_websocket)
            handle_ws_receive(tld->buffer, bytes_received, tld);
        else
            handle_binary_receive(tld->buffer, bytes_received, tld);
    }
    return true;
}

static void connection_close(thread_local_data* tld)
{
////printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> listener_locker(listener_list_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);
//printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> locker(socket_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    printf("Dropping connection.\n");
    if (tld->sd != INVALID_SOCKET)
    {
        closesocket(tld->sd);
        tld->sd = INVALID_SOCKET;
    }
    free(tld->buffer);
    delete_listener_data(tld->listener);
}

void websocket_relay_request_disconnect(int slot)
{
    thread_local_data* tld = connected_sockets[slot];
    if (tld)
    {
        tld->disconnect_requested = true;
//        connection_close(tld);
    }
}

static void* receive_thread_func(void *arg)
{
    thread_local_data* tld = (thread_local_data*)arg;
    add_connected_socket(tld);

    bool connected = true;
    while (connected && !tld->disconnect_requested)
        connected = websocket_wait_for_data(tld);
    remove_connected_socket(tld);
    connection_close(tld);
    free(tld);
    return NULL;
}

static void websocket_relay_close(SOCKET socketfd, struct addrinfo* host_info_list)
{
    printf("Closing socket.\n");
    freeaddrinfo(host_info_list);
    closesocket(socketfd);
#ifdef PLATFORM_WINDOWS
    WSACleanup();
#endif
}

#ifdef PLATFORM_WINDOWS
static char port_num_str[256] = "5001";
#else
static char port_num_str[256] = "8080";
#endif

void* websocket_relay_main_loop(void *arg)
{
    struct addrinfo *host_info_list;
    SOCKET          socketfd;
    
    bool socket_ok = websocket_server_initialize_socket(&socketfd, &host_info_list, port_num_str);

    if (!socket_ok)
    {
        printf("Error opening the socket. This usually means another instance of this program is already running.\n");
        return NULL;
    }

    while (1)
    {
        SOCKET sd;
        count_connected_sockets("5000");
        bool connected = websocket_server_wait_for_connection(socketfd, &sd);
        if (connected)
        {
            count_connected_sockets("5001");
            // A new connection eas established! Start a thread to manage it.

            count_connected_sockets("5002 AAAAA");
            thread_local_data* tld = (thread_local_data*)malloc(sizeof(thread_local_data));
            tld->sd = sd;
            tld->any_data_received = false;
            tld->is_websocket = false;
            tld->buffer_size = 256 * 1024 * 1024;
            tld->disconnect_requested = false;
            tld->buffer = (unsigned char*)malloc(tld->buffer_size);
            tld->listener = new_listener_data();
            tld->consecutive_send_fail_count = 0;
#ifdef PLATFORM_WINDOWS
            tld->rcv_thread = new std::thread(receive_thread_func, (void*)tld);
#else
            tld->pthread = (pthread_t*)malloc(sizeof(pthread_t));
            // Start the thread
            if (pthread_create(tld->pthread, NULL, receive_thread_func, (void*)tld) != 0)
            {
                fprintf(stderr, "pthread_create() error at %s:%d\n", __FILE__, __LINE__);
            }
#endif
        }
    }
    websocket_relay_close(socketfd, host_info_list);
    return NULL;
}

bool websocket_relay_start()
{
#ifdef PLATFORM_WINDOWS
	static std::thread pth(websocket_relay_main_loop, (void*)NULL);
	return true;
#else
    static pthread_t pth;
    if (pthread_create(&pth, NULL, websocket_relay_main_loop, NULL) != 0)
    {
        fprintf(stderr, "pthread_create() error at %s:%d\n", __FILE__, __LINE__);
        return false;
    }
    return true;
#endif
}

#ifdef STANDALONE
int main(int argc, char** argv)
{
    for (int i = 0; i < argc; ++i)
    {
        if (!strncmp(argv[i], "-port:", 6))
            strcpy(port_num_str, argv[i] + 6);
    }
    websocket_relay_main_loop();
}
#endif
