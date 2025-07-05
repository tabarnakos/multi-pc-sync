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

// Third-Party Includes
#include "termcolor/termcolor.hpp"

// Section 2: Defines and Macros
#define ALLOCATION_SIZE  (1024 * 1024)  // 1MiB
#define SERVER_LISTEN_BACKLOG 5         // Maximum pending connections for listen()

// Section 3: ServerThread Implementation
void ServerThread::runserver(context &ctx)
{
    ctx.latch.wait();

    ctx.active = true;
    ctx.active.notify_all();
    std::map<std::string, std::string> options;
    options["path"] = ctx.opts.path.string();
    options["exit_after_sync"] = ctx.opts.exit_after_sync ? "true" : "false";

    const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    const sockaddr_in serverAddress = { .sin_family = AF_INET, 
                                        .sin_port = htons(ctx.opts.port),
                                        .sin_addr = { .s_addr = INADDR_ANY },
                                        .sin_zero = {0} };
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
        std::cout << termcolor::red << "Unable to bind to port " << ntohs(serverAddress.sin_port) << "\r\n" << termcolor::reset;
        exit(0);
    }
    if (listen(serverSocket, SERVER_LISTEN_BACKLOG) != 0)
    {
        std::cout << termcolor::red << "Unable to listen on socket" << "\r\n" << termcolor::reset;
        exit(0);
    }

    while (!ctx.quit.load())
    {
        std::cout << termcolor::green << "Waiting for incoming connections on port " << ctx.opts.port << "\r\n" << termcolor::reset;

        sockaddr_in clientAddress;
        sockaddr* clientSocketAddr = reinterpret_cast<sockaddr*>(&clientAddress);
        socklen_t clientAddressLen = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, clientSocketAddr, &clientAddressLen);
        if (clientSocket < 0)
        {
            std::cout << termcolor::red << "Error accepting connection" << "\r\n" << termcolor::reset;
            break;
        }
        options["txsocket"] = std::to_string(clientSocket);
        options["ip"] = inet_ntoa(clientAddress.sin_addr);
        std::cout << termcolor::cyan << "Incoming connection from " << options["ip"] << ":" << clientAddress.sin_port << "\r\n" << termcolor::reset;
        ctx.con_opened = true;

        while ((!ctx.quit.load()) && ctx.con_opened)
        {
            TcpCommand *receivedCommand = TcpCommand::receiveHeader(clientSocket);
            if (receivedCommand == nullptr)
            {
                std::cout << termcolor::red << "Error receiving command from client" << "\r\n" << termcolor::reset;
                ctx.con_opened = false;
                break;
            }

            int err = receivedCommand->execute(options);
            if (err < 0)
            {
                std::cout << termcolor::red << "Error executing command: " << receivedCommand->commandName() << "\r\n" << termcolor::reset;
                ctx.con_opened = false;
            } else if (err > 0)
            {
                std::cout << termcolor::green << "Finished" << "\r\n" << termcolor::reset;
                ctx.con_opened = false;
            } else
            {
                std::cout << termcolor::cyan << "Executed command: " << receivedCommand->commandName() << "\r\n" << termcolor::reset;
            }
            delete receivedCommand;
        }
    }

    ctx.active = false;
    ctx.active.notify_all();
    close(serverSocket);
    std::cout << termcolor::blue << "Server thread exiting" << "\r\n" << termcolor::reset;
}