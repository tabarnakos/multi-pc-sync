#include "growing_buffer.h"
#include "network_thread.h"
#include <arpa/inet.h>
#include <cstddef>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include "tcp_command.h"

#define ALLOCATION_SIZE  (1024 * 1024)  //1MiB

void ClientThread::runclient(context &ctx)
{
    ctx.latch.wait();

    ctx.active = true;
    ctx.active.notify_all();
    std::map<std::string, std::string> options;
    options["path"] = ctx.opts.path.string();
    options["ip"] = ctx.opts.ip;
    options["port"] = std::to_string(ctx.opts.port);
    
    const int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    options["txsocket"] = std::to_string(serverSocket);

    const sockaddr_in serverAddress = { .sin_family = AF_INET,
                                        .sin_port = htons(ctx.opts.port),
                                        .sin_addr = { .s_addr = inet_addr(ctx.opts.ip.c_str()) } };
    const sockaddr* serverSocketAddr = reinterpret_cast<const sockaddr*>(&serverAddress);

    if (connect(serverSocket, serverSocketAddr, sizeof(serverAddress)) < 0)
    {
        std::cout << "Unable to connect to server at " << ctx.opts.ip << ":" << ctx.opts.port << std::endl;
        exit(0);
    }

    std::cout << "Connected to server at " << ctx.opts.ip << ":" << ctx.opts.port << std::endl;

    ctx.con_opened = true;

    // Request index from the server
    if (requestIndexFromServer(options) < 0)
    {
        std::cout << "Error requesting index from server" << std::endl;
        close(serverSocket);
        return;
    }

    // Receive and process commands from the server
    while (ctx.con_opened)
    {
        TcpCommand *receivedCommand = TcpCommand::receiveHeader(serverSocket);
        if (!receivedCommand)
        {
            std::cout << "Error receiving command from server" << std::endl;
            break;
        }
        
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
                break;
            case TcpCommand::CMD_ID_MESSAGE:
            case TcpCommand::CMD_ID_SYNC_DONE:
                {
                    err = receivedCommand->execute(options);
                    delete receivedCommand;
                    break;
                }
            default:
                std::cout << "Unknown command received: " << std::endl;
                receivedCommand->dump(std::cout);
                delete receivedCommand;
                break;
        }
        if (err < 0)
        {
            std::cout << "Error executing command: " << receivedCommand->commandName() << std::endl;
            ctx.con_opened = false;
        }
        else if (err > 0)
        {
            std::cout << "Finished " << std::endl;
            ctx.con_opened = false;
        }
        else
            std::cout << "Executed command: " << receivedCommand->command() << std::endl;
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