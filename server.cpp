// *****************************************************************************
// Server Implementation
// *****************************************************************************

// Section 1: Includes
// C++ Standard Library
#include <iostream>
#include <memory>
#include <string>
#include <map>

// System Includes
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

// Project Includes
#include "network_thread.h"
#include "tcp_command.h"
#include "directory_indexer.h"

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
    const auto* serverSocketAddr = reinterpret_cast<const sockaddr*>(&serverAddress);

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
        std::cout << termcolor::red << "Unable to bind to port " << ntohs(serverAddress.sin_port) << termcolor::reset << "\r\n";
        exit(0);
    }
    if (listen(serverSocket, SERVER_LISTEN_BACKLOG) != 0)
    {
        std::cout << termcolor::red << "Unable to listen on socket" << termcolor::reset << "\r\n";
        exit(0);
    }

    std::shared_ptr<DirectoryIndexer> localIndexer = nullptr;

    while (!ctx.quit.load())
    {
        std::cout << termcolor::green << "Waiting for incoming connections on port " << ctx.opts.port << termcolor::reset << "\r\n";

        sockaddr_in clientAddress;
        auto* clientSocketAddr = reinterpret_cast<sockaddr*>(&clientAddress);
        socklen_t clientAddressLen = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, clientSocketAddr, &clientAddressLen);
        if (clientSocket < 0)
        {
            std::cout << termcolor::red << "Error accepting connection" << "\r\n" << termcolor::reset;
            break;
        }
        options["txsocket"] = std::to_string(clientSocket);
        options["ip"] = inet_ntoa(clientAddress.sin_addr);
        std::cout << termcolor::cyan << "Incoming connection from " << options["ip"] << ":" << clientAddress.sin_port << termcolor::reset << "\r\n";
        ctx.con_opened = true;

        while ((!ctx.quit.load()) && ctx.con_opened)
        {
            TcpCommand *receivedCommand = TcpCommand::receiveHeader(clientSocket);
            if (receivedCommand == nullptr)
            {
                std::cout << termcolor::red << "Error receiving command from client" << termcolor::reset << "\r\n";
                ctx.con_opened = false;
                break;
            }

            int err = receivedCommand->execute(options);
            if (err < 0)
            {
                std::cout << termcolor::red << "Error executing command: " << receivedCommand->commandName() << termcolor::reset << "\r\n";
                ctx.con_opened = false;
            } else if (err > 0)
            {
                std::cout << termcolor::green << "Finished" << termcolor::reset << "\r\n";
                ctx.con_opened = false;
            } else
            {
                std::cout << termcolor::green << "Executed command: " << receivedCommand->commandName() << termcolor::reset << "\r\n";

                // Handle command-specific logic here
                if (receivedCommand->command() == TcpCommand::CMD_ID_INDEX_FOLDER)
                {
                    localIndexer = receivedCommand->getLocalIndexer();
                }
                else if (receivedCommand->command() == TcpCommand::CMD_ID_RM_REQUEST ||
                         receivedCommand->command() == TcpCommand::CMD_ID_RMDIR_REQUEST)
                {
                    //update the index
                    // If the command is a removal, we need to remove it from the local indexer
                    // This is necessary to keep the local indexer in sync with the remote indexer
                    std::cout << termcolor::cyan << "Removing path from local index: " << options["removed_path"] << termcolor::reset << "\r\n";

                    DirectoryIndexer::PATH_TYPE pathType = std::filesystem::is_directory(options["removed_path"]) ? DirectoryIndexer::PATH_TYPE::FOLDER : DirectoryIndexer::PATH_TYPE::FILE;

                    localIndexer->removePath(nullptr, options["removed_path"], pathType);
                    options.erase("removed_path");
                }
            }
            delete receivedCommand;
        }
        if (localIndexer != nullptr)
        {
            // Store the local index after each command execution
            std::cout << termcolor::cyan << "Storing local index after command execution" << termcolor::reset << "\r\n";
            localIndexer->dumpIndexToFile({});
        }
    }

    ctx.active = false;
    ctx.active.notify_all();
    close(serverSocket);
    std::cout << termcolor::blue << "Server thread exiting" << termcolor::reset << "\r\n";
}