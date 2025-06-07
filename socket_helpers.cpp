#include "socket_helpers.h"
#include <sys/socket.h>

/* when the return value is non-null, it is allocated on the heap and must be 
 * freed with delete[] */
uint8_t * SocketHelpers::recv_bytes(NetworkThread::context &ctx, int socket, size_t size)
{
    uint8_t * buf = new uint8_t[size];
    uint8_t * wptr = buf;
    do
    {
        size_t length = size - ( wptr-buf );
        int status_code = recv_timeout(socket, wptr, length, 10);
        wptr += length;
        
        if ( status_code == RECV_FAILED || status_code == SELECT_ERROR )
        {
            ctx.quit = true;
            delete[] buf;
            buf = nullptr;
            break;
        } else if ( status_code == CONN_CLOSED )
        {
            ctx.con_opened = false;
            delete[] buf;
            buf = nullptr;
            break;
        }
    } while ( wptr-buf != size );
    
    return buf;
}


SocketHelpers::status_code SocketHelpers::recv_timeout(int sockfd, void* buffer, size_t &length, int timeout_ms)
{
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sockfd + 1, &readfds, nullptr, nullptr, &tv);

    if (ret > 0 && FD_ISSET(sockfd, &readfds))
    {
        ssize_t bytes_received = recv(sockfd, buffer, length, 0);
        length = 0;
        if (bytes_received > 0)
        {
            length = bytes_received;
            return BYTES_RECEIVED;
        }
        else if (bytes_received == 0)
            return CONN_CLOSED; //doubt
        else
            return RECV_FAILED;
    }
    else if (ret == 0)
    {
        length = 0;
        return RECV_TIMEOUT;
    }
    else
        return SELECT_ERROR;
}