// Section 1: Compilation Guards
#ifndef _SYNC_COMMAND_H_
#define _SYNC_COMMAND_H_

// Section 2: Includes
#include <filesystem>
#include <list>
#include <map>
#include <md5.h>
#include <string>
#include <iostream>
#include "tcp_command.h"

// Section 3: Defines and Macros
// (none)

// Section 4: Classes
/**
 * Represents a single synchronization command between local and remote systems
 * Commands include operations like copy, move, delete, etc.
 */
class SyncCommand {
public:
    /**
     * Constructs a new sync command
     * @param cmd Command type (cp, mv, rm, etc.)
     * @param srcPath Source path for the operation
     * @param destPath Destination path for the operation
     * @param remote Whether this is a remote operation
     */
    SyncCommand(std::string cmd, std::string srcPath, std::string destPath, bool remote);

    /**
     * Default destructor
     */
    virtual ~SyncCommand() = default;   

    SyncCommand(const SyncCommand&) = default;
    SyncCommand& operator=(const SyncCommand&) = default;
    SyncCommand(SyncCommand&&) = default;
    SyncCommand& operator=(SyncCommand&&) = default;
    bool operator==(const SyncCommand &other) const {
        return mCmd == other.mCmd && mSrcPath == other.mSrcPath && mDestPath == other.mDestPath && mRemote == other.mRemote;
    }
    bool operator!=(const SyncCommand &other) const {
        return !(*this == other);
    }

    /**
     * Prints the command details to standard output
     */
    void print() const;

    /**
     * Executes the synchronization command
     * @param args Arguments for command execution
     * @param verbose Whether to print verbose output
     * @return 0 on success, negative value on error
     */
    int execute(const std::map<std::string, std::string> &args, bool verbose = false);

    /**
     * Gets the command as a string
     * @return String representation of the command
     */
    [[nodiscard]] std::string string() const;

    /**
     * Checks if command operates on remote system
     * @return true if remote operation
     */
    [[nodiscard]] bool isRemote() const;

    /**
     * Checks if command is a removal operation
     * @return true if removal operation
     */
    [[nodiscard]] bool isRemoval() const;

    [[nodiscard]] bool isFileMove() const;

    [[nodiscard]] bool isCopy() const;

    [[nodiscard]] bool isSymlink() const;

    [[nodiscard]] bool isSystem() const;

    [[nodiscard]] bool isChmod() const;

    /**
     * Gets the first path (usually source)
     * @return First path string
     */
    [[nodiscard]] std::string & path1();

    /**
     * Gets the second path (usually destination)
     * @return Second path string
     */
    [[nodiscard]] std::string & path2();

    /**
     * Removes quotes from path string
     * @param path Path string to process
     * @return Reference to the modified path string
     */
    static std::string & stripQuotes(std::string &path);

    [[nodiscard]] std::array<uint8_t, MD5_DIGEST_LENGTH> hash() const;

private:
    std::string mCmd;      ///< Command type
    std::string mSrcPath;  ///< Source path
    std::string mDestPath; ///< Destination path
    bool mRemote;          ///< Remote operation flag

    /**
     * Executes command via TCP
     * @param args Command arguments
     * @return 0 on success, negative value on error
     */
    int executeTcpCommand(const std::map<std::string, std::string> &args);

    /**
     * Creates appropriate TCP command object
     * @return Pointer to created TCP command
     */
    TcpCommand* createTcpCommand();
};

class SyncCommands : public std::list<SyncCommand> {
public:
    int exportToFile(const std::filesystem::path &path, bool verbose = false);
    int executeAll(const std::map<std::string, std::string> &args, bool verbose = false);

    void emplace_back(const char * arg1, const std::string &arg2, const std::string &arg3, const bool &isRemote) {
        std::cout << "Adding sync command: " << arg1 << " " << arg2 << " " << arg3 << (isRemote ? " (remote)" : "") << "\r\n";
        std::list<SyncCommand>::emplace_back(arg1, arg2, arg3, isRemote);
    }

    /**
     * Sorts the list of commands, moving all removal commands to the end.
     */
    void sortCommands();
};

#endif // _SYNC_COMMAND_H_