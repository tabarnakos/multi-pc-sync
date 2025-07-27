// Section 1: Main Header
#include "sync_command.h"
#include "md5_wrapper.h"
#include "tcp_command.h"

// Section 2: Includes
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

// Third-Party Includes
#include "termcolor/termcolor.hpp"

// Section 3: Defines and Macros
// (none)

// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
SyncCommand::SyncCommand(std::string cmd, std::string srcPath, std::string destPath, bool remote)
    : mCmd(std::move(cmd)), mRemote(remote) {
    mSrcPath = "\"" + std::move(srcPath) + "\"";
    if (destPath.empty())
        mDestPath.clear();
    else
        mDestPath = "\"" + std::move(destPath) + "\"";
}

// Section 6: Static Methods
// (none)

// Section 7: Public/Protected/Private Methods
std::string & SyncCommand::stripQuotes(std::string &path) {
    if (!path.empty() && path.front() == '"') path = path.substr(1);
    if (!path.empty() && path.back() == '"') path = path.substr(0, path.length() - 1);
    return path;
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
        commandbuf.write(hash());
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "rmdir") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 2) + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_RMDIR_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        commandbuf.write(hash());
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "mkdir") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 2) + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_MKDIR_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        commandbuf.write(hash());
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "cp") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 3) + srcPathStripped.length() + destPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_REMOTE_LOCAL_COPY;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        commandbuf.write(hash());
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
        commandbuf.write(hash());
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "push") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 3) + destPathStripped.length() + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_PUSH_FILE;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        commandbuf.write(hash());
        pathSize = destPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(destPathStripped.c_str(), pathSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else if (mCmd == "symlink") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 3) + srcPathStripped.length() + destPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_REMOTE_SYMLINK;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        commandbuf.write(hash());
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
        pathSize = destPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(destPathStripped.c_str(), pathSize);
    } else if (mCmd == "mv") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 3) + srcPathStripped.length() + destPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_REMOTE_MOVE; // Using copy for move
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        commandbuf.write(hash());
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
        pathSize = destPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(destPathStripped.c_str(), pathSize);
    } else if (mCmd == "touch") {
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 3) + srcPathStripped.length() + destPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_TOUCH;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        commandbuf.write(hash());
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
        pathSize = destPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(destPathStripped.c_str(), pathSize);
    } else if (mCmd == "system") {
        // For system commands, we just send the command string as a payload
        auto systemCommand = srcPathStripped + " " + destPathStripped;
        size_t cmdSize = TcpCommand::kCmdSize + (TcpCommand::kSizeSize * 2) + systemCommand.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_SYSTEM_CALL;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        commandbuf.write(hash());
        pathSize = systemCommand.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(systemCommand.c_str(), pathSize);
    } else {
        std::cerr << termcolor::red << "Unknown command: " << mCmd << "\r\n" << termcolor::reset;
        return nullptr;
    }
    return TcpCommand::create(commandbuf);
}

int SyncCommand::executeTcpCommand(const std::map<std::string, std::string> &args) {
    TcpCommand *cmd = createTcpCommand();
    if (cmd == nullptr) {
        std::cerr << termcolor::red << "Failed to create TCP command for: " << string() << "\r\n" << termcolor::reset;
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
        TcpCommand::SendFile(opts);
    }
    TcpCommand::unblock_transmit();

    if ( cmd->command() == TcpCommand::CMD_ID_FETCH_FILE_REQUEST )
    {
        auto opts = args;
        stripQuotes(mDestPath);
        opts["path"] = mDestPath;
        TcpCommand::ReceiveFile(opts);

        TcpCommand::unblock_receive();
    }
    delete cmd;
    return result;
}

void SyncCommand::print() const{
    std::cout << termcolor::blue << string() << "\r\n" << termcolor::reset;
}

int SyncCommand::execute(const std::map<std::string, std::string> &args, bool verbose) {
    if (verbose) {
        print();
        std::string confirm;
        std::cout << termcolor::magenta << "Execute? (y/n): " << termcolor::reset;
        std::cin >> confirm;
        if (confirm != "y" && confirm != "Y") {
            return 0;
        }
    }
    if (mRemote || mCmd == "push" || mCmd == "fetch") {
        return executeTcpCommand(args);
    }

    if (mCmd == "touch") {
        std::cerr << termcolor::red << "touch local sync command created, should be handled already. Path =" << mSrcPath << termcolor::reset << "\r\n";
        return -1;
    }

    if (mCmd == "symlink") {
        // Remove existing destination if it exists
        stripQuotes(mSrcPath);
        stripQuotes(mDestPath);
        if (std::filesystem::exists(mDestPath)) {
            std::filesystem::remove(mDestPath);
        }
        std::error_code errorCode;
        std::filesystem::create_symlink(mSrcPath, mDestPath, errorCode);
        if (errorCode) {
            std::cerr << termcolor::red << "Failed to create symlink from " << mDestPath << " to " << mSrcPath << ": " << errorCode.message() << "\r\n" << termcolor::reset;
            return -1;
        }
        std::cout << termcolor::cyan << "Created symlink: " << mDestPath << " -> " << mSrcPath << "\r\n" << termcolor::reset;
        return 0;
    }

    std::cout << termcolor::cyan << "Executing command: " << string() << "\r\n" << termcolor::reset;

    int err = system(string().c_str());
    if (verbose) {
        std::cout << termcolor::blue << "Command returned " << err << "\r\n" << termcolor::reset;
    }

    if (mCmd == "cp")
    {
        // Need to copy the file modified time and permissions
        if (err == 0)
        {
            stripQuotes(mSrcPath);
            stripQuotes(mDestPath);
            std::filesystem::permissions(mDestPath, std::filesystem::status(mSrcPath).permissions(), std::filesystem::perm_options::replace);
            auto modifiedTime = std::filesystem::last_write_time(mSrcPath);
            std::filesystem::last_write_time(mDestPath, modifiedTime);
            std::cout << termcolor::cyan << "Copied permissions and modified time: " << mSrcPath << " to " << mDestPath << termcolor::reset << "\r\n";
        }
    }

    return err;
}

std::string SyncCommand::string() const {
    return mCmd + " " + mSrcPath + " " + mDestPath;
}

bool SyncCommand::isRemote() const { return mRemote; }
bool SyncCommand::isRemoval() const { return mCmd == "rm" || mCmd == "rmdir"; }
bool SyncCommand::isFileMove() const { return mCmd == "mv"; }
bool SyncCommand::isCopy() const { return mCmd == "cp" || mCmd == "push" || mCmd == "fetch"; }
bool SyncCommand::isSymlink() const { return mCmd == "symlink"; }
bool SyncCommand::isSystem() const { return mCmd == "system"; }
bool SyncCommand::isChmod() const { return mCmd == "chmod"; }
std::string & SyncCommand::path1() { return mSrcPath; }
std::string & SyncCommand::path2() { return mDestPath; }
std::array<uint8_t, MD5_DIGEST_LENGTH> SyncCommand::hash() const
{
    MD5Calculator sum(string().c_str(), string().length(), false);
    return std::to_array(sum.getDigest().digest_bytes);
}

int SyncCommands::exportToFile(const std::filesystem::path &path, bool verbose) {
    std::ofstream file(path);
    if (!file.is_open()) return -1;
    for (const auto &cmd : *this) {
        file << cmd.string() << "\r\n";
        if (verbose) std::cout << termcolor::blue << "Exported: " << cmd.string() << "\r\n" << termcolor::reset;
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
                return 5; // File creation commands
            }
            if (command.isSystem() || command.isChmod()) { //System commands like chmod
                return 4; // System commands
            }
            if (command.isFileMove()) {
                return 3; // File move operations
            }
            if (command.isRemoval()) {
                return 2; // File delete operations
            }
            if (command.isSymlink()) {
                return 1; // Symlink creation
            }
            return 6; // Other commands like mkdir, touch
        };
        return getPriority(commandA) > getPriority(commandB);
    });
}