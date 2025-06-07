#include <cmath>
#include <cstdint>
#include <cstdio>
#include <bits/getopt_core.h>
#include "network_thread.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "program_options.h"
#include <iostream>
#include <thread>

#define ALLOCATION_SIZE		(1024*1024)

struct HumanReadable
{
    std::uintmax_t size{};
 
private:
    friend std::ostream& operator<<(std::ostream& os, HumanReadable hr)
    {
        int o{};
        double mantissa = hr.size;
        for (; mantissa >= 1024.; mantissa /= 1024., ++o);
        os << std::ceil(mantissa * 10.) / 10. << "BKMGTPE"[o];
        return o ? os << "B (" << hr.size << ')' : os;
    }
};

int main(int argc, char *argv[])
{
    const auto opts = ProgramOptions::parseArgs(argc, argv);

    if (opts.ip.empty() && opts.mode == opts.MODE_CLIENT)
    {
        std::cout << "Invalid client configuration. Please specify the server IP and set mode to client." << std::endl;
        return -1;
    }

    if (opts.mode == opts.MODE_SERVER)
    {
        // Server mode
        auto server = new ServerThread(opts);
        if ( !server )
        {
            std::cout << "Error creating server thread" << std::endl;
            return -1;
        }

        server->start();
        while (!server->isActive() )
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "Server is active and waiting for connections..." << std::endl;

        while (server->isActive())
        {
            if ( server->isConnected() )

                std::cout << "Client connected, waiting for commands..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        delete server;
        std::cout << "Server thread finished" << std::endl;
    }
    else if (opts.mode == opts.MODE_CLIENT)
    {
        // Client mode
        auto client = new ClientThread(opts);
        if (!client)
        {
            std::cout << "Error creating client thread" << std::endl;
            return -1;
        }

        client->start();

        while (client->isActive())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        delete client;
        std::cout << "Client thread finished" << std::endl;
    }
}