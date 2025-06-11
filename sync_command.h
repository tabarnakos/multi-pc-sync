// Section 1: Compilation Guards
#ifndef _SYNC_COMMAND_H_
#define _SYNC_COMMAND_H_

// Section 2: Includes
#include <filesystem>
#include <list>
#include <map>
#include <string>
#include "tcp_command.h"

// Section 3: Defines and Macros
// (none)

// Section 4: Classes
class SyncCommand {
public:
    SyncCommand(const std::string &cmd, const std::string &srcPath, const std::string &destPath, bool remote);
    void print();
    int execute(const std::map<std::string, std::string> &args, bool verbose = false);
    std::string string() const;
    bool isRemote() const;
    bool isRemoval() const;
    std::string path1() const;
    std::string path2() const;

private:
    std::string mCmd;
    std::string mSrcPath;
    std::string mDestPath;
    bool mRemote;
    int executeTcpCommand(const std::map<std::string, std::string> &args);
    TcpCommand* createTcpCommand();
    void stripQuotes(std::string &path);
};

class SyncCommands : public std::list<SyncCommand> {
public:
    int exportToFile(std::filesystem::path &path, bool verbose = false);
    int executeAll(const std::map<std::string, std::string> &args, bool verbose = false);
};

#endif // _SYNC_COMMAND_H_