#ifndef _SYNC_COMMAND_H_
#define _SYNC_COMMAND_H_

#include <string>
#include <list>
#include <filesystem>
#include "tcp_command.h"

class SyncCommand
{
public:
    SyncCommand(const std::string &cmd, const std::string &srcPath, 
                const std::string &destPath, bool remote);
    void print();
    int execute(const std::map<std::string, std::string> &args, bool verbose = false);
    std::string string() const { return mCmd + " " + mSrcPath + (mDestPath.empty() ? "" : " " + mDestPath); }
    bool isRemote() const { return mRemote; }
    bool isRemoval() const { return mCmd == "rm" || mCmd == "rmdir"; }
    std::string path1() const { return mSrcPath; }
    std::string path2() const { return mDestPath; }

private:
    std::string mCmd;
    std::string mSrcPath;
    std::string mDestPath;
    bool mRemote;

    int executeTcpCommand(const std::map<std::string, std::string> &args);
    TcpCommand* createTcpCommand();
    void stripQuotes(std::string &path);
};

class SyncCommands : public std::list<SyncCommand>
{
public:
    int exportToFile(std::filesystem::path &path, bool verbose = false);
    int executeAll(const std::map<std::string, std::string> &args, bool verbose = false);
};

#endif //_SYNC_COMMAND_H_