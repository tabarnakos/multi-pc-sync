// Section 1: Compilation Guards
#ifndef _PROGRAM_OPTIONS_H_
#define _PROGRAM_OPTIONS_H_

// Section 2: Includes
#include <filesystem>
#include <string>
#include <cstdint>
#include <optional>

// Section 3: Defines and Macros
constexpr uint64_t BYTES_PER_GB = 1ULL << 30; // 1 GiB = 1024^3 bytes
constexpr uint64_t DEFAULT_MAX_FILE_SIZE_GB = 64ULL;
constexpr uint64_t DEFAULT_MAX_FILE_SIZE_BYTES = (DEFAULT_MAX_FILE_SIZE_GB * BYTES_PER_GB) - 1;

class ProgramOptions {
public:
    // Command line options
    enum MODE : std::uint8_t {
        MODE_CLIENT = 0,
        MODE_SERVER,
    };
    
    // Config file options

    // Command line options
    const std::filesystem::path path;
    std::string ip;
    int port;
    MODE mode;
    float rate_limit;  // Rate limit in Hz, 0 means unlimited
    bool auto_sync;     // Skip Y/N prompt and automatically sync
    bool dry_run;       // Print commands but don't execute
    bool exit_after_sync; // Exit server after sending SyncDoneCmd (for unit testing)
    std::optional<std::filesystem::path> config_file; // Path to configuration file
    
    // Config file options
    uint64_t max_file_size_bytes = DEFAULT_MAX_FILE_SIZE_BYTES; // 64GiB default

    static ProgramOptions parseArgs(int argc, char *argv[]);
    void parseConfigFile();

private:
    ProgramOptions(int argc, char *argv[]);
    
    // Helper methods for config file parsing
    static uint64_t parseMaxFileSize(const std::string& value);
    static std::pair<std::string, std::string> parseConfigLine(const std::string& line);
};

#endif  // _PROGRAM_OPTIONS_H_