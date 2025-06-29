#ifndef _SOCKET_HELPERS_H_
#define _SOCKET_HELPERS_H_


#include "network_thread.h"

namespace SocketHelpers
{
    enum status_code : std::uint8_t
    {
        BYTES_RECEIVED = 0,
        RECV_FAILED,
        RECV_TIMEOUT,
        CONN_CLOSED,
        SELECT_ERROR,
    };

    uint8_t * recv_bytes(NetworkThread::context &ctx, int socket, size_t size);

    status_code recv_timeout(int sockfd, void* buffer, size_t &length, int timeout_ms);

};


#endif  //_SOCKET_HELPERS_H_