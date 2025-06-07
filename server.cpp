#include "network_thread.h"
#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include "tcp_command.h"
#include "socket_helpers.h"

#define ALLOCATION_SIZE  (1024 * 1024)  //1MiB

void ServerThread::runserver(context &ctx)
{
    ctx.latch.wait();

    ctx.active = true;
    ctx.active.notify_all();
    std::map<std::string, std::string> options;
    options["path"] = ctx.opts.path.string();

    const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    const sockaddr_in serverAddress = { .sin_family = AF_INET, 
                                        .sin_port = htons(ctx.opts.port),
                                        .sin_addr = { .s_addr = INADDR_ANY } };
    const sockaddr* serverSocketAddr = reinterpret_cast<const sockaddr*>(&serverAddress);

    if (bind(serverSocket, serverSocketAddr, sizeof(serverAddress)) != 0)
    {
        std::cout << "Unable to bind to port " << ntohs(serverAddress.sin_port) << std::endl;
        exit(0);
    }

    if (listen(serverSocket, 5) != 0)
    {
        std::cout << "Unable to listen on socket" << std::endl;
        exit(0);
    }

    while (!ctx.quit.load())
    {
        std::cout << "Waiting for incoming connections on port " << ctx.opts.port << std::endl;

        sockaddr_in clientAddress;
        sockaddr* clientSocketAddr = reinterpret_cast<sockaddr*>(&clientAddress);
        socklen_t clientAddressLen = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, clientSocketAddr, &clientAddressLen);
        if (clientSocket < 0)
        {
            std::cout << "Error accepting connection" << std::endl;
            break;
        }
        options["txsocket"] = std::to_string(clientSocket);
        std::cout << "Incoming connection from " << inet_ntoa(clientAddress.sin_addr) << ":" << clientAddress.sin_port << std::endl;
        ctx.con_opened = true;

        while (!ctx.quit.load() && ctx.con_opened)
        {
            TcpCommand *receivedCommand = TcpCommand::receiveHeader(clientSocket);
            if (!receivedCommand)
            {
                std::cout << "Error receiving command from client" << std::endl;
                ctx.con_opened = false;
                break;
            }

            size_t commandSize = receivedCommand->cmdSize();
            size_t bytesReceived = receivedCommand->receivePayload(clientSocket, ALLOCATION_SIZE);
            if (bytesReceived < commandSize)
            {
                std::cout << "Error receiving command payload from client" << std::endl;
                delete receivedCommand;
                ctx.con_opened = false;
                break;
            }

            std::cout << "Executing received command" << std::endl;
            if (receivedCommand->execute(options) < 0)
            {
                std::cout << "Error executing command" << std::endl;
            }
            delete receivedCommand;
        }
    }

    ctx.active = false;
    ctx.active.notify_all();
}