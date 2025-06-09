#include "sync_command.h"
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>

SyncCommand::SyncCommand( const std::string & cmd, const std::string & srcPath, const std::string & destPath, bool remote ) :
    mCmd(cmd),
    mRemote(remote)
{
    mSrcPath = "\"" + srcPath + "\"";
    if ( destPath.empty() )
        mDestPath.clear();
    else
        mDestPath = "\"" + destPath + "\"";
}

void SyncCommand::stripQuotes(std::string &path) {
    if (path.front() == '"') path = path.substr(1);
    if (path.back() == '"') path = path.substr(0, path.length() - 1);
}

TcpCommand* SyncCommand::createTcpCommand() {
    GrowingBuffer commandbuf;
    size_t pathSize;
    std::string srcPathStripped = mSrcPath;
    std::string destPathStripped = mDestPath;
    stripQuotes(srcPathStripped);
    stripQuotes(destPathStripped);

    if (mCmd == "rm") {
        size_t cmdSize = TcpCommand::kCmdSize + TcpCommand::kSizeSize * 2 + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_RM_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    }
    else if (mCmd == "rmdir") {
        size_t cmdSize = TcpCommand::kCmdSize + TcpCommand::kSizeSize * 2 + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_RMDIR_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    }
    else if (mCmd == "mkdir") {
        size_t cmdSize = TcpCommand::kCmdSize + TcpCommand::kSizeSize * 2 + srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_MKDIR_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    }
    else if (mCmd == "cp") {
        size_t cmdSize = TcpCommand::kCmdSize + TcpCommand::kSizeSize * 3 + 
                        srcPathStripped.length() + destPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_REMOTE_LOCAL_COPY;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
        
        pathSize = destPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(destPathStripped.c_str(), pathSize);
    }
    else if (mCmd == "push" || mCmd == "fetch") {
        size_t cmdSize = TcpCommand::kCmdSize + TcpCommand::kSizeSize * 2 + 
                        srcPathStripped.length();
        commandbuf.write(&cmdSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = (mCmd == "push") ? TcpCommand::CMD_ID_PUSH_FILE : TcpCommand::CMD_ID_FETCH_FILE_REQUEST;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        
        pathSize = srcPathStripped.length();
        commandbuf.write(&pathSize, sizeof(size_t));
        commandbuf.write(srcPathStripped.c_str(), pathSize);
    } else {
        std::cerr << "Unknown command: " << mCmd << std::endl;
        return nullptr;
    }

    return TcpCommand::create(commandbuf);
}

int SyncCommand::executeTcpCommand(const std::map<std::string, std::string> &args) {
    TcpCommand *cmd = createTcpCommand();
    if (!cmd) {
        std::cerr << "Failed to create TCP command for: " << string() << std::endl;
        return -1;
    }

    if (mCmd == "fetch")
        cmd->block_receive();

    int result = cmd->transmit(args);
    if (result < 0) {
        std::cerr << "Failed to transmit TCP command: " << string() << std::endl;
        delete cmd;
        return -1;
    }
    if (mCmd == "push") {
        // If it's a copy command, we need to send the file as well
        std::map<std::string, std::string> fileArgs = args;
        std::string srcPathStripped = mSrcPath;
        stripQuotes(srcPathStripped);
        fileArgs["path"] = srcPathStripped;
        cmd->SendFile(fileArgs);
        cmd->unblock_transmit();
    } else if (mCmd == "fetch") {
        cmd->unblock_transmit();
        // If it's a fetch command, we need to receive the file
        std::map<std::string, std::string> fileArgs = args;
        std::string destPathStripped = mDestPath;
        stripQuotes(destPathStripped);
        fileArgs["path"] = destPathStripped;
        if (cmd->ReceiveFile(fileArgs) < 0) {
            std::cerr << "Error receiving file: " << destPathStripped << std::endl;
            cmd->unblock_receive();
            delete cmd;
            return -1;
        }
        cmd->unblock_receive();
    } else {
        cmd->unblock_transmit();
    }

    delete cmd;
    return result;
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

    if (mRemote) {
        return executeTcpCommand(args);
    } else {
        int err = system(string().c_str());
        if (verbose) {
            std::cout << "Command returned " << err << std::endl;
        }
        return err;
    }
}

void SyncCommand::print()
{
    std::cout << string() << std::endl;
}

int SyncCommands::executeAll(const std::map<std::string, std::string> &args, bool verbose) {
    int errors = 0;
    for (auto &cmd : *this) {
        if (cmd.execute(args, verbose) != 0) {
            errors++;
        }
    }
    return errors;
}

int SyncCommands::exportToFile(std::filesystem::path &path, bool verbose)
{
    if ( std::filesystem::exists(path) )
    {
        char cmd[255];
        sprintf(cmd,"rm \"%s\"", path.c_str());
        system(cmd);
    }

    std::ofstream filestream;
    filestream.open(path, std::ios_base::out);
    if ( !filestream.is_open() )
        return -1;
    
    filestream << "#! /usr/bin/bash" << std::endl;

    for ( const auto &command : *this )
    {
        if ( verbose )
            filestream << "echo \"" << command.string() << '"' << std::endl;
        filestream << command.string() << std::endl;
    }
    filestream.close();
    return 0;
}