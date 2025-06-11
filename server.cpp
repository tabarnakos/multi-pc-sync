// *****************************************************************************
// Server Implementation
// *****************************************************************************

// Section 1: Includes
// C++ Standard Library
#include <iostream>
#include <string>
#include <map>

// System Includes
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

// Project Includes
#include "network_thread.h"
#include "tcp_command.h"

// Section 2: Defines and Macros
#define ALLOCATION_SIZE  (1024 * 1024)  // 1MiB

// Section 3: ServerThread Implementation
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

    int yes = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

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
        options["ip"] = inet_ntoa(clientAddress.sin_addr);
        std::cout << "Incoming connection from " << options["ip"] << ":" << clientAddress.sin_port << std::endl;
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

            int err = receivedCommand->execute(options);
            if (err < 0)
            {
                std::cout << "Error executing command: " << receivedCommand->commandName() << std::endl;
                ctx.con_opened = false;
            } else if (err > 0)
            {
                std::cout << "Finished" << std::endl;
                ctx.con_opened = false;
            } else
            {
                std::cout << "Executed command: " << receivedCommand->commandName() << std::endl;
            }
            delete receivedCommand;
        }
    }

    ctx.active = false;
    ctx.active.notify_all();
}