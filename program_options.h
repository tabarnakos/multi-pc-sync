// Section 1: Compilation Guards
#ifndef _PROGRAM_OPTIONS_H_
#define _PROGRAM_OPTIONS_H_

// Section 2: Includes
#include <filesystem>
#include <string>

// Section 3: Defines and Macros
// (none)

// Section 4: Classes
class ProgramOptions {
public:
    enum MODE {
        MODE_CLIENT = 0,
        MODE_SERVER,
    };
    const std::filesystem::path path;
    std::string ip;
    int port;
    MODE mode;
    float rate_limit;  // Rate limit in Hz, 0 means unlimited
    bool auto_sync;     // Skip Y/N prompt and automatically sync
    bool dry_run;       // Print commands but don't execute
    bool exit_after_sync; // Exit server after sending SyncDoneCmd (for unit testing)

    static ProgramOptions parseArgs(int argc, char *argv[]);

private:
    ProgramOptions(int argc, char *argv[]);
};

#endif  // _PROGRAM_OPTIONS_H_