#include "program_options.h"
#include <bits/getopt_core.h>
#include <cstring>
#include <iostream>

void printusage()
{
	std::cout << "Usage:" << std::endl;
	std::cout << "\t" << "multi-pc-sync [-s <serverip:port> | -d <port>] <path>" << std::endl;
	std::cout << "\t" << "-s" << "\t" << "connect to <serverip:port>, indexes the path and synchronizes folders" << std::endl;
	std::cout << "\t" << "-d" << "\t" << "start a synchronization daemon on <port> for <path>" << std::endl;
	exit(0);
}

ProgramOptions ProgramOptions::parseArgs(int argc, char *argv[])
{
    ProgramOptions opts(argc, argv);

    if ( argc < 2 )
    {
        printusage();
        exit(0);
    }
    
    if ( !std::filesystem::is_directory( opts.path ) )
    {
        std::cout << opts.path << " is not a valid directory" << std::endl;
        exit(0);
    } else if ( opts.path != std::filesystem::canonical( opts.path ) )
    {
        std::cout << opts.path << " is not a valid path" << std::endl;
        exit(0);
    }

    int c;
    opts.port = -1;
    opts.mode = opts.MODE_SERVER;
    while ((c = getopt (argc, argv, "s:d:")) != -1)
    {
        switch (c)
        {
        case 's':
        {
            opts.mode = opts.MODE_CLIENT;
            opts.ip = strtok( optarg,":" );
            char * portstr = strtok( NULL, ":" );
            if ( !portstr )
                printusage();
            opts.port = atoi( portstr );
        }
            break;
        case 'd':
            opts.mode = opts.MODE_SERVER;
            opts.ip = "127.0.0.1";
            opts.port = atoi( optarg );
            break;
        default:
        case '?':
            printusage();
        }
    }
    return opts;
}