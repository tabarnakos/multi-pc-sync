// Section 1: Compilation Guards
#ifndef _PROGRAM_OPTIONS_H_
#define _PROGRAM_OPTIONS_H_

// Section 2: Includes
#include <filesystem>
#include <string>
#include <cstdint>
#include <optional>

// Section 3: Defines and Macros
// (none)

class ProgramOptions {
public:
    // Command line options
    enum MODE : std::uint8_t {
        MODE_CLIENT = 0,
        MODE_SERVER,
    };
    
    // Config file options
    enum ConflictPriority : std::uint8_t {
        PRIORITY_CLIENT = 0,
        PRIORITY_SERVER,
        PRIORITY_NEWEST,
        PRIORITY_OLDEST
    };
    
    enum ConflictBehavior : std::uint8_t {
        BEHAVIOR_OVERWRITE = 0,
        BEHAVIOR_RENAME
    };
    
    enum DeletedModifiedBehavior : std::uint8_t {
        DELETED_MODIFIED_DELETE = 0,
        DELETED_MODIFIED_KEEP
    };
    
    enum DoubleMoveStrategy : std::uint8_t {
        DOUBLE_MOVE_KEEP_BOTH = 0,
        DOUBLE_MOVE_CLIENT,
        DOUBLE_MOVE_SERVER
    };

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
    ConflictPriority conflict_file_creation_priority = PRIORITY_CLIENT;
    ConflictBehavior conflict_file_creation_behavior = BEHAVIOR_OVERWRITE;
    ConflictPriority conflict_file_modification_priority = PRIORITY_CLIENT;
    ConflictBehavior conflict_file_modification_behavior = BEHAVIOR_OVERWRITE;
    DeletedModifiedBehavior conflict_deleted_modified = DELETED_MODIFIED_DELETE;
    DoubleMoveStrategy conflict_double_move = DOUBLE_MOVE_KEEP_BOTH;

    static ProgramOptions parseArgs(int argc, char *argv[]);
    void parseConfigFile();

private:
    ProgramOptions(int argc, char *argv[]);
    
    // Helper methods for config file parsing
    static ConflictPriority parseConflictPriority(const std::string& value, const std::string& option_name);
    static ConflictBehavior parseConflictBehavior(const std::string& value, const std::string& option_name);
    static DeletedModifiedBehavior parseDeletedModifiedBehavior(const std::string& value);
    static DoubleMoveStrategy parseDoubleMoveStrategy(const std::string& value);
    static std::pair<std::string, std::string> parseConfigLine(const std::string& line);
};

#endif  // _PROGRAM_OPTIONS_H_