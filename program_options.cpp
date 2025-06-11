// Section 1: Main Header
#include "program_options.h"

// Section 2: Includes
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

// Section 3: Defines and Macros
// (none)

// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
ProgramOptions::ProgramOptions(int argc, char *argv[])
    : path(argv[argc - 1]), rate_limit(0.0f) {}

// Section 6: Static Methods
// (none)

// Section 7: Public/Protected/Private Methods
void printusage()
{
	std::cout << "Usage:" << std::endl;
	std::cout << "\t" << "multi-pc-sync [-s <serverip:port> | -d <port>] [-r rate] <path>" << std::endl;
	std::cout << "\t" << "-s" << "\t" << "connect to <serverip:port>, indexes the path and synchronizes folders" << std::endl;
	std::cout << "\t" << "-d" << "\t" << "start a synchronization daemon on <port> for <path>" << std::endl;
	std::cout << "\t" << "-r" << "\t" << "limit TCP command rate (Hz), 0 means unlimited (default: 0)" << std::endl;
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
    while ((c = getopt (argc, argv, "s:d:r:")) != -1)
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
        case 'r':
            opts.rate_limit = std::stof(optarg);
            if (opts.rate_limit < 0) {
                std::cout << "Rate limit must be non-negative" << std::endl;
                exit(0);
            }
            break;
        default:
        case '?':
            printusage();
        }
    }
    return opts;
}