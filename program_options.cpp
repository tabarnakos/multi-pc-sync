// Section 1: Main Header
#include "program_options.h"

// Section 2: Includes
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <getopt.h>
#include <array>
#include <utility> // for std::pair
#include <termcolor/termcolor.hpp>

// Section 3: Defines and Macros
// (none)

// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
ProgramOptions::ProgramOptions(int argc, char *argv[])
    : path(argv[argc - 1]), rate_limit(0.0F), auto_sync(false), dry_run(false), exit_after_sync(false), config_file(std::nullopt) {}

// Section 6: Static Methods
// (none)

// Section 7: Public/Protected/Private Methods
void printusage()
{
	std::cout << termcolor::white << "Usage:" << "\r\n" << termcolor::reset;
	std::cout << termcolor::white << "\t" << "multi-pc-sync [-s <serverip:port> | -d <port>] [-r rate] [-y] [--cfg=<cfgfile>] [--dry-run] [--print-before-sync] [--exit-after-sync] <path>" << "\r\n" << termcolor::reset;
	std::cout << termcolor::white << "\t" << "-s" << "\t" << "connect to <serverip:port>, indexes the path and synchronizes folders" << "\r\n" << termcolor::reset;
	std::cout << termcolor::white << "\t" << "-d" << "\t" << "start a synchronization daemon on <port> for <path>" << "\r\n" << termcolor::reset;
	std::cout << termcolor::white << "\t" << "-r" << "\t" << "limit TCP command rate (Hz), 0 means unlimited (default: 0)" << "\r\n" << termcolor::reset;
	std::cout << termcolor::white << "\t" << "-y" << "\t" << "skip Y/N prompt and automatically sync" << "\r\n" << termcolor::reset;
    std::cout << termcolor::white << "\t" << "--print-before-sync" << "\t" << "print commands before executing them (equivalent to --dry-run -y)" << "\r\n" << termcolor::reset;
	std::cout << termcolor::white << "\t" << "--cfg=<cfgfile>" << "\t" << "path to configuration file for additional options" << "\r\n" << termcolor::reset;
	std::cout << termcolor::white << "\t" << "--dry-run" << "\t" << "print commands but don't execute them" << "\r\n" << termcolor::reset;
	std::cout << termcolor::white << "\t" << "--exit-after-sync" << "\t" << "exit server after sending SyncDoneCmd (for unit testing)" << "\r\n" << termcolor::reset;
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
        std::cout << termcolor::red << opts.path << " is not a valid directory" << "\r\n" << termcolor::reset;
        exit(0);
    } else if ( opts.path != std::filesystem::canonical( opts.path ) )
    {
        std::cout << termcolor::red << opts.path << " is not a valid path" << "\r\n" << termcolor::reset;
        std::cout << termcolor::cyan << "Did you mean: " << std::filesystem::canonical(opts.path) << "\r\n" << termcolor::reset;
        std::cout << termcolor::cyan << "Only absolute paths are supported" << "\r\n" << termcolor::reset;
        exit(0);
    }

    int chr;
    opts.port = -1;
    opts.mode = ProgramOptions::MODE_SERVER;

    static constexpr int kDryRunOption = 1;
    static constexpr int kExitAfterSyncOption = 2;
    static constexpr int kConfigFileOption = 3;
    static constexpr int kPrintBeforeSyncOption = 4;
    
    static constexpr std::array<option, 5> long_options{{
        {.name = "dry-run", .has_arg = no_argument, .flag = nullptr, .val = kDryRunOption},
        {.name = "exit-after-sync", .has_arg = no_argument, .flag = nullptr, .val = kExitAfterSyncOption},
        {.name = "cfg", .has_arg = required_argument, .flag = nullptr, .val = kConfigFileOption},
        {.name = "print-before-sync", .has_arg = no_argument, .flag = nullptr, .val = kPrintBeforeSyncOption},
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
                std::cout << termcolor::red << "Rate limit must be non-negative" << "\r\n" << termcolor::reset;
                exit(0);
            }
            break;
        case 'y':
            opts.auto_sync = true;
            break;
        case kDryRunOption:
            opts.dry_run = true;
            break;
        case kExitAfterSyncOption:
            opts.exit_after_sync = true;
            break;
        case kConfigFileOption:
            opts.config_file = std::filesystem::path(optarg);
            if (!std::filesystem::exists(*opts.config_file) || !std::filesystem::is_regular_file(*opts.config_file)) {
                std::cout << termcolor::red << "Config file not found or not a regular file: " << optarg << "\r\n" << termcolor::reset;
                exit(0);
            }
            break;
        case kPrintBeforeSyncOption:
            // --print-before-sync is equivalent to --dry-run and --auto-sync
            opts.dry_run = true;
            opts.auto_sync = true;
            break;
        default:
        case '?':
            printusage();
        }
    }
    // Parse config file if provided
    if (opts.config_file) {
        opts.parseConfigFile();
    }
    
    return opts;
}

// Helper methods for parseConfigFile to reduce complexity
ProgramOptions::ConflictPriority ProgramOptions::parseConflictPriority(const std::string& value, const std::string& option_name) {
    if (value == "client") {
        return PRIORITY_CLIENT;
    }
    
    if (value == "server") {
        return PRIORITY_SERVER;
    }
    
    if (value == "newest") {
        return PRIORITY_NEWEST;
    }
    
    if (value == "oldest") {
        return PRIORITY_OLDEST;
    }
    
    std::cerr << termcolor::red << "Invalid value '" << value << "' for " << option_name << "\r\n" << termcolor::reset;
    return PRIORITY_CLIENT; // Default to client
}

ProgramOptions::ConflictBehavior ProgramOptions::parseConflictBehavior(const std::string& value, const std::string& option_name) {
    if (value == "overwrite") {
        return BEHAVIOR_OVERWRITE;
    }
    
    if (value == "rename") {
        return BEHAVIOR_RENAME;
    }
    
    std::cerr << termcolor::red << "Invalid value '" << value << "' for " << option_name << "\r\n" << termcolor::reset;
    return BEHAVIOR_OVERWRITE; // Default to overwrite
}

ProgramOptions::DeletedModifiedBehavior ProgramOptions::parseDeletedModifiedBehavior(const std::string& value) {
    if (value == "delete") {
        return DELETED_MODIFIED_DELETE;
    }
    
    if (value == "keep") {
        return DELETED_MODIFIED_KEEP;
    }
    
    std::cerr << termcolor::red << "Invalid value '" << value << "' for CONFLICT_ON_DELETED_AND_MODIFIED\r\n" << termcolor::reset;
    return DELETED_MODIFIED_DELETE; // Default to delete
}

ProgramOptions::DoubleMoveStrategy ProgramOptions::parseDoubleMoveStrategy(const std::string& value) {
    if (value == "keep") {
        return DOUBLE_MOVE_KEEP_BOTH;
    }
    
    if (value == "client") {
        return DOUBLE_MOVE_CLIENT;
    }
    
    if (value == "server") {
        return DOUBLE_MOVE_SERVER;
    }
    
    std::cerr << termcolor::red << "Invalid value '" << value << "' for CONFLICT_ON_DOUBLE_MOVE\r\n" << termcolor::reset;
    return DOUBLE_MOVE_KEEP_BOTH; // Default to keep both
}

std::pair<std::string, std::string> ProgramOptions::parseConfigLine(const std::string& line) {
    // Find key-value delimiter
    auto pos = line.find('=');
    if (pos == std::string::npos) {
        return {"", ""}; // No key-value delimiter
    }
    
    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);
    
    // Trim whitespace
    key.erase(key.find_last_not_of(" \t\r\n") + 1);
    value.erase(0, value.find_first_not_of(" \t\r\n"));
    value.erase(value.find_last_not_of(" \t\r\n") + 1);
    
    return {key, value};
}

void ProgramOptions::parseConfigFile() {
    if (!config_file) {
        return;
    }
    
    std::ifstream file(*config_file);
    if (!file.is_open()) {
        std::cerr << termcolor::red << "Failed to open config file: " << *config_file << "\r\n" << termcolor::reset;
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ' ' || 
            line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        
        auto [key, value] = parseConfigLine(line);
        if (key.empty()) {
            continue;
        }
        
        // Parse the values based on keys
        if (key == "CONFLICT_ON_FILE_CREATION_PRIORITY") {
            conflict_file_creation_priority = parseConflictPriority(value, key);
        } 
        else if (key == "CONFLICT_ON_FILE_CREATION_BEHAVIOR") {
            conflict_file_creation_behavior = parseConflictBehavior(value, key);
        }
        else if (key == "CONFLICT_ON_FILE_MODIFICATION_PRIORITY") {
            conflict_file_modification_priority = parseConflictPriority(value, key);
        }
        else if (key == "CONFLICT_ON_FILE_MODIFICATION_BEHAVIOR") {
            conflict_file_modification_behavior = parseConflictBehavior(value, key);
        }
        else if (key == "CONFLICT_ON_DELETED_AND_MODIFIED") {
            conflict_deleted_modified = parseDeletedModifiedBehavior(value);
        }
        else if (key == "CONFLICT_ON_DOUBLE_MOVE") {
            conflict_double_move = parseDoubleMoveStrategy(value);
        }
        // Add other config options here as needed
    }
}