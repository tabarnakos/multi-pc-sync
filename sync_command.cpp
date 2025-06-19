// Section 1: Main Header
#include "sync_command.h"
#include "tcp_command.h"

// Section 2: Includes
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

// Section 3: Defines and Macros
// (none)

// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
SyncCommand::SyncCommand(const std::string &cmd, const std::string &srcPath, const std::string &destPath, bool remote)
    : mCmd(cmd), mRemote(remote) {
    mSrcPath = "\"" + srcPath + "\"";
    if (destPath.empty())
        mDestPath.clear();
    else
        mDestPath = "\"" + destPath + "\"";
}

// Section 6: Static Methods
// (none)

// Section 7: Public/Protected/Private Methods
void SyncCommand::stripQuotes(std::string &path) {
    if (!path.empty() && path.front() == '"') path = path.substr(1);
    if (!path.empty() && path.back() == '"') path = path.substr(0, path.length() - 1);
}

TcpCommand* SyncCommand::createTcpCommand() {
    GrowingBuffer commandbuf;
    size_t pathSize;
    std::string srcPathStripped = mSrcPath;
    std::string destPathStripped = mDestPath;
    stripQuotes(srcPathStripped);
    stripQuotes(destPathStripped);
    if (mCmd == "rm") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 2) + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_RM_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "rmdir") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 2) + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_RMDIR_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "mkdir") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 2) + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_MKDIR_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "cp") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 3) + srcPathStripped.length() + destPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_REMOTE_LOCAL_COPY;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
        pathSize = destPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(destPathStripped.c_str(), pathSize);
    } else if (mCmd == "fetch") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 2) + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_FETCH_FILE_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "push") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 2) + destPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_PUSH_FILE;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = destPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(destPathStripped.c_str(), pathSize);
    } else {
        std::cerr << "Unknown command: " << mCmd << "\n\r";
        return nullptr;
    }
    return TcpCommand::create(commandbuf);
}

int SyncCommand::executeTcpCommand(const std::map<std::string, std::string> &args) {
    TcpCommand *cmd = createTcpCommand();
    if (!cmd) {
        std::cerr << "Failed to create TCP command for: " << string() << "\n\r";
        return -1;
    }
    if ( cmd->command() == TcpCommand::CMD_ID_FETCH_FILE_REQUEST )
        TcpCommand::block_receive();

    TcpCommand::block_transmit();

    int result = cmd->transmit(args);

    if ( cmd->command() == TcpCommand::CMD_ID_PUSH_FILE )
    {
        auto opts = args;
        stripQuotes(mSrcPath);
        opts["path"] = mSrcPath;
        cmd->SendFile(opts);
    }
    TcpCommand::unblock_transmit();

    if ( cmd->command() == TcpCommand::CMD_ID_FETCH_FILE_REQUEST )
    {
        auto opts = args;
        stripQuotes(mDestPath);
        opts["path"] = mDestPath;
        cmd->ReceiveFile(opts);

        TcpCommand::unblock_receive();
    }
    delete cmd;
    return result;
}

void SyncCommand::print() {
    std::cout << string() << "\n\r";
}

int SyncCommand::execute(const std::map<std::string, std::string> &args, bool verbose) {
    if (verbose) {
        print();
        std::string confirm;
        std::cout << "Execute? (y/n): ";
        std::cin >> confirm;
        if (confirm != "y" && confirm != "Y") {
            return 0;
        }
    }
    if (mRemote || mCmd == "push" || mCmd == "fetch") {
        return executeTcpCommand(args);
    } else {
        int err = system(string().c_str());
        if (verbose) {
            std::cout << "Command returned " << err << "\n\r";
        }
        return err;
    }
}

std::string SyncCommand::string() const {
    return mCmd + " " + mSrcPath + " " + mDestPath;
}

bool SyncCommand::isRemote() const { return mRemote; }
bool SyncCommand::isRemoval() const { return mCmd == "rm" || mCmd == "rmdir"; }
bool SyncCommand::isFileMove() const { return mCmd == "mv"; }
bool SyncCommand::isCopy() const { return mCmd == "cp" || mCmd == "push" || mCmd == "fetch"; }
std::string SyncCommand::path1() const { return mSrcPath; }
std::string SyncCommand::path2() const { return mDestPath; }

int SyncCommands::exportToFile(const std::filesystem::path &path, bool verbose) {
    std::ofstream file(path);
    if (!file.is_open()) return -1;
    for (const auto &cmd : *this) {
        file << cmd.string() << "\n\r";
        if (verbose) std::cout << "Exported: " << cmd.string() << "\n\r";
    }
    file.close();
    return 0;
}

int SyncCommands::executeAll(const std::map<std::string, std::string> &args, bool verbose) {
    for (auto &cmd : *this) {
        cmd.execute(args, verbose);
    }
    return 0;
}

void SyncCommands::sortCommands() {
    this->sort([](const SyncCommand &commandA, const SyncCommand &commandB) {
        auto getPriority = [](const SyncCommand &command) {
            if (command.isCopy()) {
                return 3; // File creation commands
            }
            if (command.isFileMove()) {
                return 2; // File move operations
            }
            if (command.isRemoval()) {
                return 1; // File delete operations
            }
            return 4; // Other commands
        };
        return getPriority(commandA) < getPriority(commandB);
    });
}