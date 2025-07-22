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

// Third-Party Includes
#include "termcolor/termcolor.hpp"

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
constexpr int SLEEP_DURATION_MS = 10;

int main(int argc, char *argv[])
{
    const auto opts = ProgramOptions::parseArgs(argc, argv);
    TcpCommand::setRateLimit(opts.rate_limit);  // Set global rate limit
    TcpCommand::setMaxFileSize(opts.max_file_size_bytes);  // Set configurable max file size

    if (opts.ip.empty() && opts.mode == ProgramOptions::MODE_CLIENT)
    {
        std::cout << termcolor::red << "Invalid client configuration. Please specify the server IP and set mode to client." << "\r\n" << termcolor::reset;
        return -1;
    }

    if (opts.mode == ProgramOptions::MODE_SERVER)
    {
        // Server mode
        auto *server = new ServerThread(opts);
        if (server == nullptr)
        {
            std::cout << termcolor::red << "Error creating server thread" << "\r\n" << termcolor::reset;
            return -1;
        }

        server->start();
        server->waitForActive();
        
        std::cout << termcolor::green << "Server is active and waiting for connections..." << "\r\n" << termcolor::reset;

        while (server->isActive())
        {
            static bool connected = false;
            if (server->isConnected() && !connected)
            {
                std::cout << termcolor::green << "Client connected." << "\r\n" << termcolor::reset;
                connected = true;
            }
            else if (!server->isConnected() && connected)
            {
                std::cout << termcolor::cyan << "Client disconnected." << "\r\n" << termcolor::reset;
                connected = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_DURATION_MS));
        }

        delete server;
        std::cout << termcolor::green << "Server thread finished" << "\r\n" << termcolor::reset;
    }
    else if (opts.mode == ProgramOptions::MODE_CLIENT)
    {
        // Client mode
        auto *client = new ClientThread(opts);
        if (client == nullptr)
        {
            std::cout << termcolor::red << "Error creating client thread" << "\r\n" << termcolor::reset;
            return -1;
        }

        client->start();
        client->waitForActive();
        
        while (client->isActive())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_DURATION_MS));
        }

        delete client;
        std::cout << termcolor::green << "Client thread finished" << "\r\n" << termcolor::reset;
    }
}