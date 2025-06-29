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
	std::cout << "Usage:" << "\r\n";
	std::cout << "\t" << "multi-pc-sync [-s <serverip:port> | -d <port>] [-r rate] [-y] [--cfg=<cfgfile>] [--dry-run] [--exit-after-sync] <path>" << "\r\n";
	std::cout << "\t" << "-s" << "\t" << "connect to <serverip:port>, indexes the path and synchronizes folders" << "\r\n";
	std::cout << "\t" << "-d" << "\t" << "start a synchronization daemon on <port> for <path>" << "\r\n";
	std::cout << "\t" << "-r" << "\t" << "limit TCP command rate (Hz), 0 means unlimited (default: 0)" << "\r\n";
	std::cout << "\t" << "-y" << "\t" << "skip Y/N prompt and automatically sync" << "\r\n";
	std::cout << "\t" << "--cfg=<cfgfile>" << "\t" << "path to configuration file for additional options" << "\r\n";
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
    
    static constexpr std::array<option, 4> long_options{{
        {.name = "dry-run", .has_arg = no_argument, .flag = nullptr, .val = 1},
        {.name = "exit-after-sync", .has_arg = no_argument, .flag = nullptr, .val = 2},
        {.name = "cfg", .has_arg = required_argument, .flag = nullptr, .val = 3},
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
        case 3: // --cfg=<cfgfile>
            opts.config_file = std::filesystem::path(optarg);
            if (!std::filesystem::exists(*opts.config_file) || !std::filesystem::is_regular_file(*opts.config_file)) {
                std::cout << "Config file not found or not a regular file: " << optarg << "\r\n";
                exit(0);
            }
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
    
    std::cerr << "Invalid value '" << value << "' for " << option_name << "\r\n";
    return PRIORITY_CLIENT; // Default to client
}

ProgramOptions::ConflictBehavior ProgramOptions::parseConflictBehavior(const std::string& value, const std::string& option_name) {
    if (value == "overwrite") {
        return BEHAVIOR_OVERWRITE;
    }
    
    if (value == "rename") {
        return BEHAVIOR_RENAME;
    }
    
    std::cerr << "Invalid value '" << value << "' for " << option_name << "\r\n";
    return BEHAVIOR_OVERWRITE; // Default to overwrite
}

ProgramOptions::DeletedModifiedBehavior ProgramOptions::parseDeletedModifiedBehavior(const std::string& value) {
    if (value == "delete") {
        return DELETED_MODIFIED_DELETE;
    }
    
    if (value == "keep") {
        return DELETED_MODIFIED_KEEP;
    }
    
    std::cerr << "Invalid value '" << value << "' for CONFLICT_ON_DELETED_AND_MODIFIED\r\n";
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
    
    std::cerr << "Invalid value '" << value << "' for CONFLICT_ON_DOUBLE_MOVE\r\n";
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
        std::cerr << "Failed to open config file: " << *config_file << "\r\n";
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