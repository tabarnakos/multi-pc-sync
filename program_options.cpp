// Section 1: Main Header
#include "program_options.h"

// Section 2: Includes
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <getopt.h>

// Section 3: Defines and Macros
// (none)

// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
ProgramOptions::ProgramOptions(int argc, char *argv[])
    : path(argv[argc - 1]), rate_limit(0.0F), auto_sync(false), dry_run(false) {}

// Section 6: Static Methods
// (none)

// Section 7: Public/Protected/Private Methods
void printusage()
{
	std::cout << "Usage:" << '\n';
	std::cout << "\t" << "multi-pc-sync [-s <serverip:port> | -d <port>] [-r rate] [-y] [--dry-run] <path>" << '\n';
	std::cout << "\t" << "-s" << "\t" << "connect to <serverip:port>, indexes the path and synchronizes folders" << '\n';
	std::cout << "\t" << "-d" << "\t" << "start a synchronization daemon on <port> for <path>" << '\n';
	std::cout << "\t" << "-r" << "\t" << "limit TCP command rate (Hz), 0 means unlimited (default: 0)" << '\n';
	std::cout << "\t" << "-y" << "\t" << "skip Y/N prompt and automatically sync" << '\n';
	std::cout << "\t" << "--dry-run" << "\t" << "print commands but don't execute them" << '\n';
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
        std::cout << opts.path << " is not a valid directory" << '\n';
        exit(0);
    } else if ( opts.path != std::filesystem::canonical( opts.path ) )
    {
        std::cout << opts.path << " is not a valid path" << '\n';
        exit(0);
    }

    int chr;
    opts.port = -1;
    opts.mode = opts.MODE_SERVER;
    
    static struct option long_options[] = {
        {"dry-run", no_argument, nullptr, 1},
        {nullptr, 0, nullptr, 0}
    };
    
    int option_index = 0;
    while ((chr = getopt_long(argc, argv, "s:d:r:y", long_options, &option_index)) != -1)
    {
        switch (chr)
        {
        case 's':
        {
            opts.mode = opts.MODE_CLIENT;
            opts.ip = strtok( optarg,":" );
            char * portstr = strtok( nullptr, ":" );
            if ( nullptr == portstr )
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
                std::cout << "Rate limit must be non-negative" << '\n';
                exit(0);
            }
            break;
        case 'y':
            opts.auto_sync = true;
            break;
        case 1: // --dry-run
            opts.dry_run = true;
            break;
        default:
        case '?':
            printusage();
        }
    }
    return opts;
}