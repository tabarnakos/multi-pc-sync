// *****************************************************************************
// Main Program Entry Point
// *****************************************************************************

// Section 1: Includes
// C Standard Library
#include <cmath>
#include <cstdio>

// C++ Standard Library
#include <iostream>
#include <thread>

// System Includes
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <bits/getopt_core.h>

// Project Includes
#include "network_thread.h"
#include "program_options.h"
#include "tcp_command.h"

// Section 2: Main Function
int main(int argc, char *argv[])
{
    const auto opts = ProgramOptions::parseArgs(argc, argv);
    TcpCommand::setRateLimit(opts.rate_limit);  // Set global rate limit

    if (opts.ip.empty() && opts.mode == ProgramOptions::MODE_CLIENT)
    {
        std::cout << "Invalid client configuration. Please specify the server IP and set mode to client." << "\r\n";
        return -1;
    }

    if (opts.mode == ProgramOptions::MODE_SERVER)
    {
        // Server mode
        auto *server = new ServerThread(opts);
        if (server == nullptr)
        {
            std::cout << "Error creating server thread" << "\r\n";
            return -1;
        }

        server->start();
        while (!server->isActive())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "Server is active and waiting for connections..." << "\r\n";

        while (server->isActive())
        {
            static bool connected = false;
            if (server->isConnected() && !connected)
            {
                std::cout << "Client connected." << "\r\n";
                connected = true;
            }
            else if (!server->isConnected() && connected)
            {
                std::cout << "Client disconnected." << "\r\n";
                connected = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        delete server;
        std::cout << "Server thread finished" << "\r\n";
    }
    else if (opts.mode == ProgramOptions::MODE_CLIENT)
    {
        // Client mode
        auto *client = new ClientThread(opts);
        if (client == nullptr)
        {
            std::cout << "Error creating client thread" << "\r\n";
            return -1;
        }

        client->start();
        if ( !client->waitForActive() )
        {
            std::cout << "Client thread failed to start" << "\r\n";
            delete client;
            return -1;
        }

        std::cout << "Client is active and connecting to server..." << "\r\n";

        while (client->isActive())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        delete client;
        std::cout << "Client thread finished" << "\r\n";
    }
}