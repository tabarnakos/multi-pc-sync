// *****************************************************************************
// Client Implementation
// *****************************************************************************

// Section 1: Includes
// C Standard Library
#include <cstddef>

// C++ Standard Library
#include <iostream>
#include <map>
#include <string>

// System Includes
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

// Project Includes
#include "growing_buffer.h"
#include "network_thread.h"
#include "tcp_command.h"
#include <termcolor/termcolor.hpp>

// Section 2: Defines and Macros
#define ALLOCATION_SIZE  (1024 * 1024)  // 1MiB

// Section 3: ClientThread Implementation
void ClientThread::runclient(context &ctx)
{
    ctx.latch.wait();

    ctx.active = true;
    ctx.active.notify_all();
    std::map<std::string, std::string> options;
    options["path"] = ctx.opts.path.string();
    options["ip"] = ctx.opts.ip;
    options["port"] = std::to_string(ctx.opts.port);
    options["auto_sync"] = ctx.opts.auto_sync ? "true" : "false";
    options["dry_run"] = ctx.opts.dry_run ? "true" : "false";
    
    const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    options["txsocket"] = std::to_string(serverSocket);

    const sockaddr_in serverAddress = { .sin_family = AF_INET,
                                        .sin_port = htons(ctx.opts.port),
                                        .sin_addr = { .s_addr = inet_addr(ctx.opts.ip.c_str()) },
                                        .sin_zero = {0} };
    const sockaddr* serverSocketAddr = reinterpret_cast<const sockaddr*>(&serverAddress);

    if (connect(serverSocket, serverSocketAddr, sizeof(serverAddress)) < 0)
    {
        std::cout << termcolor::red << "Unable to connect to server at " << ctx.opts.ip << ":" << ctx.opts.port << termcolor::reset << "\r\n";
        exit(0);
    }

    std::cout << termcolor::green << "Connected to server at " << ctx.opts.ip << ":" << ctx.opts.port << termcolor::reset << "\r\n";

    ctx.con_opened = true;

    // Request index from the server
    if (requestIndexFromServer(options) < 0)
    {
        std::cout << termcolor::red << "Error requesting index from server" << termcolor::reset << "\r\n";
        close(serverSocket);
        return;
    }

    // Receive and process commands from the server
    while (ctx.con_opened)
    {
        TcpCommand *receivedCommand = TcpCommand::receiveHeader(serverSocket);
        if (receivedCommand == nullptr)
        {
            std::cout << termcolor::red << "Error receiving command from server" << termcolor::reset << "\r\n";
            break;
        }
        const std::string cmdName = receivedCommand->commandName();
        
        int err = 0;;
        switch (receivedCommand->command())
        {
            case TcpCommand::CMD_ID_INDEX_FOLDER:
            case TcpCommand::CMD_ID_MKDIR_REQUEST:
            case TcpCommand::CMD_ID_RM_REQUEST:
            case TcpCommand::CMD_ID_FETCH_FILE_REQUEST:
            case TcpCommand::CMD_ID_PUSH_FILE:
            case TcpCommand::CMD_ID_REMOTE_LOCAL_COPY:
            case TcpCommand::CMD_ID_RMDIR_REQUEST:
            case TcpCommand::CMD_ID_SYNC_COMPLETE:
                //not possible in the client
                delete receivedCommand;
                break;
            case TcpCommand::CMD_ID_INDEX_PAYLOAD:
                TcpCommand::executeInDetachedThread(receivedCommand, options);
                // receive mutex is still locked
                break;
            case TcpCommand::CMD_ID_MESSAGE:
            case TcpCommand::CMD_ID_SYNC_DONE:
                {
                    err = receivedCommand->execute(options);
                    delete receivedCommand;
                    break;
                }
            default:
                std::cout << termcolor::yellow << "Unknown command received: " << termcolor::reset << "\r\n";
                receivedCommand->dump(std::cout);
                delete receivedCommand;
                break;
        }
        if (err < 0)
        {
            std::cout << termcolor::red << "Error executing command: " << cmdName << termcolor::reset << "\r\n";
            ctx.con_opened = false;
        }
        else if (err > 0)
        {
            std::cout << termcolor::green << "Finished " << termcolor::reset << "\r\n";
            ctx.con_opened = false;
        }
        else
            std::cout << termcolor::cyan << "Executed command: " << cmdName << termcolor::reset << "\r\n";
    }

    ctx.active = false;
    ctx.active.notify_all();
    close(serverSocket);
}

int ClientThread::requestIndexFromServer(const std::map<std::string, std::string>& options)
{
    // Prepare the command buffer
    GrowingBuffer commandbuf;
    size_t cmdSize = TcpCommand::kSizeSize + TcpCommand::kCmdSize;
    commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
    TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_INDEX_FOLDER;
    commandbuf.write(&cmd, TcpCommand::kCmdSize);

    TcpCommand *command = TcpCommand::create(commandbuf);
    if (!command)
        return -1;

    // Transmit the command
    int result = command->transmit(options);
    delete command;
    return result;
}