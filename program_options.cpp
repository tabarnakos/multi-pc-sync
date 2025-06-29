// Section 1: Main Header
#include "program_options.h"

// Section 2: Includes
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <getopt.h>
#include <array>

// Section 3: Defines and Macros
// (none)

// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
ProgramOptions::ProgramOptions(int argc, char *argv[])
    : path(argv[argc - 1]), rate_limit(0.0F), auto_sync(false), dry_run(false), exit_after_sync(false) {}

// Section 6: Static Methods
// (none)

// Section 7: Public/Protected/Private Methods
void printusage()
{
	std::cout << "Usage:" << "\r\n";
	std::cout << "\t" << "multi-pc-sync [-s <serverip:port> | -d <port>] [-r rate] [-y] [--dry-run] [--exit-after-sync] <path>" << "\r\n";
	std::cout << "\t" << "-s" << "\t" << "connect to <serverip:port>, indexes the path and synchronizes folders" << "\r\n";
	std::cout << "\t" << "-d" << "\t" << "start a synchronization daemon on <port> for <path>" << "\r\n";
	std::cout << "\t" << "-r" << "\t" << "limit TCP command rate (Hz), 0 means unlimited (default: 0)" << "\r\n";
	std::cout << "\t" << "-y" << "\t" << "skip Y/N prompt and automatically sync" << "\r\n";
	std::cout << "\t" << "--dry-run" << "\t" << "print commands but don't execute them" << "\r\n";
	std::cout << "\t" << "--exit-after-sync" << "\t" << "exit server after sending SyncDoneCmd (for unit testing)" << "\r\n";
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
        std::cout << opts.path << " is not a valid directory" << "\r\n";
        exit(0);
    } else if ( opts.path != std::filesystem::canonical( opts.path ) )
    {
        std::cout << opts.path << " is not a valid path" << "\r\n";
        exit(0);
    }

    int chr;
    opts.port = -1;
    opts.mode = ProgramOptions::MODE_SERVER;
    
    static constexpr std::array<option, 3> long_options{{
        {.name = "dry-run", .has_arg = no_argument, .flag = nullptr, .val = 1},
        {.name = "exit-after-sync", .has_arg = no_argument, .flag = nullptr, .val = 2},
        {.name = nullptr, .has_arg = 0, .flag = nullptr, .val = 0}
    }};
    
    int option_index = 0;
    while ((chr = getopt_long(argc, argv, "s:d:r:y", long_options.data(), &option_index)) != -1)
    {
        switch (chr)
        {
        case 's':
        {
            opts.mode = ProgramOptions::MODE_CLIENT;
            opts.ip = strtok( optarg,":" );
            char * portstr = strtok( nullptr, ":" );
            if ( nullptr == portstr )
                printusage();
            opts.port = atoi( portstr );
        }
            break;
        case 'd':
            opts.mode = ProgramOptions::MODE_SERVER;
            opts.ip = "127.0.0.1";
            opts.port = atoi( optarg );
            break;
        case 'r':
            opts.rate_limit = std::stof(optarg);
            if (opts.rate_limit < 0) {
                std::cout << "Rate limit must be non-negative" << "\r\n";
                exit(0);
            }
            break;
        case 'y':
            opts.auto_sync = true;
            break;
        case 1: // --dry-run
            opts.dry_run = true;
            break;
        case 2: // --exit-after-sync
            opts.exit_after_sync = true;
            break;
        default:
        case '?':
            printusage();
        }
    }
    return opts;
}