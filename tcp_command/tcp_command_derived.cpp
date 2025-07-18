// *****************************************************************************
// TCP Command Implementation
// *****************************************************************************

// Section 1: Main Header
#include "tcp_command.h"

// Section 2: Includes
// C Standard Library
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h> /* Definition of AT_* constants */
#include <sys/stat.h>

// C++ Standard Library
#include <array>
#include <filesystem>
#include <iostream>
#include <string>

// System Includes
#include <sys/socket.h>

// Third-Party Includes
#include "termcolor/termcolor.hpp"

// Project Includes
#include "directory_indexer.h"
#include "sync_command.h"

// Section 3: Defines and Macros
#if defined(DEBUG) || defined(_DEBUG)
#define FORCE_SYNC_COMMANDS_FILE_EXPORT 1
#else
#define FORCE_SYNC_COMMANDS_FILE_EXPORT 0
#endif

// Section 4: Static Variables

// Section 5: Constructors/Destructors

MessageCmd::MessageCmd(const std::string &message)
{
    // Calculate the total size of the command
    size_t messageSize = message.size();
    size_t commandSize = 0; //placeholder

    // Write the command size
    mData.write(&commandSize, TcpCommand::kSizeSize);

    // Write the command ID
    cmd_id_t cmd = CMD_ID_MESSAGE;
    mData.write(&cmd, TcpCommand::kCmdSize);

    // Write the error message size
    mData.write(&messageSize, sizeof(size_t));

    // Write the error message
    mData.write(message.data(), messageSize);
}

IndexFolderCmd::~IndexFolderCmd() {}
IndexPayloadCmd::~IndexPayloadCmd() {}
MkdirCmd::~MkdirCmd() {}
RmCmd::~RmCmd() {}
FileFetchCmd::~FileFetchCmd() {}
FilePushCmd::~FilePushCmd() {}
RemoteLocalCopyCmd::~RemoteLocalCopyCmd() {}
RmdirCmd::~RmdirCmd() {}
SyncCompleteCmd::~SyncCompleteCmd() {}
SyncDoneCmd::~SyncDoneCmd() {}
MessageCmd::~MessageCmd() {}

// Section 6: Static Methods

void MessageCmd::sendMessage(const int socket, const std::string &message)
{
    MessageCmd cmd(message);
    std::cout << termcolor::cyan << "[localhost] " << message << "\r\n" << termcolor::reset;
    block_transmit();
    cmd.transmit({{"txsocket", std::to_string(socket)}});
    unblock_transmit();
}

// Section 7: Public/Protected/Private Methods

int RemoteSymlinkCmd::execute(std::map<std::string, std::string>& args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for RemoteSymlinkCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    // Extract source and destination paths
    std::string srcPath = extractStringFromPayload(kSrcPathSizeIndex, SEEK_SET);
    std::string destPath = extractStringFromPayload(0, SEEK_CUR);

    // Remove existing destination if it exists
    if (std::filesystem::exists(destPath)) {
        std::filesystem::remove(destPath);
    }
    std::error_code errorCode;
    std::filesystem::create_symlink(srcPath, destPath, errorCode);
    if (errorCode) {
        std::cerr << termcolor::red << "Failed to create symlink from " << destPath << " to " << srcPath << ": " << errorCode.message() << "\r\n" << termcolor::reset;
        return -1;
    }
    std::cout << termcolor::cyan << "Created symlink: " << destPath << " -> " << srcPath << "\r\n" << termcolor::reset;
    return 0;
}

int RemoteMoveCmd::execute(std::map<std::string, std::string>& args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for RemoteMoveCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    // Extract source and destination paths
    std::string srcPath = extractStringFromPayload(kSrcPathSizeIndex, SEEK_SET);
    std::string destPath = extractStringFromPayload(0, SEEK_CUR);

    std::error_code errorCode;
    std::filesystem::rename(srcPath, destPath, errorCode);
    if (errorCode) {
        std::cerr << termcolor::red << "Failed to move " << srcPath << " to " << destPath << ": " << errorCode.message() << "\r\n" << termcolor::reset;
        return -1;
    }
    std::cout << termcolor::green << "Moved file: " << srcPath << " -> " << destPath << "\r\n" << termcolor::reset;
    return 0;
}

int IndexFolderCmd::execute(std::map<std::string,std::string> &args)
{
    unblock_receive();
    const std::string indexfilename = std::filesystem::path(args.at("path")) / ".folderindex";
	const std::string lastrunIndexFilename = indexfilename + ".last_run";
    bool lastrunIndexPresent = false;

    if ( std::filesystem::exists(indexfilename) )
    {
        lastrunIndexPresent = true;
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Local index from last run found, creating a backup");
        if (std::filesystem::exists(lastrunIndexFilename))
        {
            MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Last run index already exists, removing it");
            std::filesystem::remove(lastrunIndexFilename);
        }
        std::filesystem::rename( indexfilename, lastrunIndexFilename );
    }
    
    /* kick off the indexing */
    std::cout << termcolor::cyan << "starting to index " << args.at("path") << "\r\n" << termcolor::reset;
    
    localIndexer = std::make_shared<DirectoryIndexer>(args.at("path"), true, DirectoryIndexer::INDEX_TYPE_LOCAL);
    DirectoryIndexer *lastindexer = nullptr;
    if ( lastrunIndexPresent )
    {
        lastindexer = new DirectoryIndexer(args.at("path"), true, DirectoryIndexer::INDEX_TYPE_LOCAL_LAST_RUN);
    }
    localIndexer->indexonprotobuf(false);

    const size_t path_length = args.at("path").length();

    GrowingBuffer commandbuf;
    cmd_id_t cmd = CMD_ID_INDEX_PAYLOAD;
    //packet format: 
    // size_t commandSize
    // cmd_id_t cmd = CMD_ID_INDEX_PAYLOAD
    // size_t path_length
    // char path[path_length]
    // size_t indexfilename_size
    // char indexfilename[indexfilename_size]
    // size_t indexfiledata_size
    // char indexfiledata[indexfiledata_size]
    // size_t lastrunIndexFilename_size
    // char lastrunIndexFilename[lastrunIndexFilename_size]
    // size_t lastrunIndexFiledata_size
    // char lastrunIndexFiledata[lastrunIndexFiledata_size]
    size_t commandSize = 0; //placeholder
    commandbuf.write(commandSize);
    commandbuf.write(cmd);
    commandbuf.write(path_length);
    commandbuf.write(args.at("path").data(), path_length);
    // --- Insert deletion log into commandbuf ---
    // You must implement getDeletions() in DirectoryIndexer to return a std::vector<std::string>
    std::vector<std::string> deletions = localIndexer->getDeletions(lastindexer);
    appendDeletionLogToBuffer(commandbuf, deletions);
    // --- End insertion ---

    TcpCommand * command = TcpCommand::create(commandbuf);
    if ( command == nullptr )
    {
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to create command for sending index.");
        return -1;
    }
    block_transmit();
    command->transmit(args, true);
    delete command;

    // Now send the index files
    auto fileargs = args;
    fileargs["path"] = indexfilename;
    if ( SendFile(fileargs) < 0 )
    {
        unblock_transmit();
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to send index file.");
        return -1;
    }

    if ( lastrunIndexPresent )
    {
        fileargs["path"] = lastrunIndexFilename;
        if ( SendFile(fileargs) < 0 )
        {
            unblock_transmit();
            MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to send last run index file.");
            return -1;
        }
    } else 
    {
        //std::cout << "DEBUG: Sending file: " << lastrunIndexFilename << "\r\n";
        int socket = std::stoi(args.at("txsocket"));
        //std::cout << "DEBUG: Sending file header..." << "\r\n";
        size_t path_size = lastrunIndexFilename.size();
        
        size_t sent_bytes = sendChunk(socket, &path_size, sizeof(size_t));
        if (sent_bytes < sizeof(size_t)) {
            std::cerr << termcolor::red << "Failed to send path size" << "\r\n" << termcolor::reset;
            unblock_transmit();
            return -1;
        }
        //std::cout << "DEBUG: Path size sent: " << path_size << " bytes" << "\r\n";
        sent_bytes = sendChunk(socket, lastrunIndexFilename.data(), path_size);
        if (sent_bytes < path_size) {
            std::cerr << termcolor::red << "Failed to send file path" << "\r\n" << termcolor::reset;
            unblock_transmit();
            return -1;
        }

        // Send a fake file modified time
        std::filesystem::file_time_type modTime = std::filesystem::file_time_type::clock::now();
        std::string modTimeStr = DirectoryIndexer::file_time_to_string(modTime);
        size_t modTimeSize = modTimeStr.size();
        sent_bytes = sendChunk(socket, &modTimeSize, sizeof(size_t));
        if (sent_bytes < sizeof(size_t)) {
            std::cerr << termcolor::red << "Failed to send modified time size" << "\r\n" << termcolor::reset;
            return -1;
        }
        sent_bytes = sendChunk(socket, modTimeStr.data(), modTimeSize);
        if (sent_bytes < modTimeSize) {
            std::cerr << termcolor::red << "Failed to send modified time" << "\r\n" << termcolor::reset;
            return -1;
        }

        //std::cout << "DEBUG: File path sent: " << lastrunIndexFilename << "\r\n";
        size_t file_size = 0;   //file does not exist
        //std::cout << "DEBUG: File size is " << file_size << " bytes" << "\r\n";
        
        // Send the file size
        //std::cout << "DEBUG: Sending file size: " << file_size << " bytes" << "\r\n";
        sent_bytes = sendChunk(socket, &file_size, sizeof(size_t));
        if (sent_bytes < sizeof(size_t)) {
            std::cerr << termcolor::red << "Failed to send file size" << "\r\n" << termcolor::reset;
            unblock_transmit();
            return -1;
        }
    }

    unblock_transmit();
    
    return 0;
}

int IndexPayloadCmd::execute(std::map<std::string, std::string> &args)
{
    // Keep receive mutex locked until we've received everything
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), payloadSize);
    
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for IndexPayloadCmd" << "\r\n" << termcolor::reset;
        unblock_receive();  // Only unlock on error
        return -1;
    }

    std::string remotePath = extractStringFromPayload(kPayloadIndex, SEEK_SET);
    size_t indexFileNameSize = 0;
    const auto remoteDeletions = parseDeletionLogFromBuffer(mData, indexFileNameSize, SEEK_CUR);

    std::cout << termcolor::green << "Received index for remote path: " << remotePath << "\r\n" << termcolor::reset;

    const std::filesystem::path localPath = args.at("path");
    const std::filesystem::path indexpath = std::filesystem::path(localPath) / ".folderindex";
    const std::filesystem::path lastRunIndexPath = std::filesystem::path(localPath) / ".folderindex.last_run";;
    //local path used intentionally to save the remote index
    const std::filesystem::path remoteIndexPath = std::filesystem::path(localPath) / ".remote.folderindex";
    const std::filesystem::path remoteLastRunIndexPath = std::filesystem::path(localPath) / ".remote.folderindex.last_run";


    auto fileargs = args;
    fileargs["path"] = remoteIndexPath;
    int ret = ReceiveFile(fileargs);
    if ( ret < 0 )
    {
        std::cerr << termcolor::red << "Error receiving remote index file." << "\r\n" << termcolor::reset;
        unblock_receive();  // Only unlock on error
        return ret;
    }

    fileargs["path"] = remoteLastRunIndexPath;
    ret = ReceiveFile(fileargs);
    if ( ret < 0 )
    {
        std::cerr << termcolor::red << "Error receiving remote last run index file." << "\r\n" << termcolor::reset;
        unblock_receive();  // Only unlock on error
        return ret;
    }
    unblock_receive();  // unlock for real now

    bool lastrunIndexPresent = false;

    if ( std::filesystem::exists(indexpath) )
    {
        lastrunIndexPresent = true;
        if (std::filesystem::exists(lastRunIndexPath))
        {
            std::filesystem::remove(lastRunIndexPath);
        }
        std::filesystem::rename(indexpath, lastRunIndexPath);
    }

    std::cout << termcolor::cyan << "importing remote index" << "\r\n" << termcolor::reset;
    DirectoryIndexer remoteIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_REMOTE);
    remoteIndexer.setPath(remotePath);

    DirectoryIndexer *lastRunRemoteIndexer = nullptr;
    if (std::filesystem::exists(remoteLastRunIndexPath))
    {
        std::cout << termcolor::cyan << "importing remote index from last run" << "\r\n" << termcolor::reset;
        lastRunRemoteIndexer = new DirectoryIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_REMOTE_LAST_RUN);
        lastRunRemoteIndexer->setPath(remotePath);
    }

    std::cout << termcolor::cyan << "remote and local indexes in hand, ready to sync" << "\r\n" << termcolor::reset;
    DirectoryIndexer *lastRunIndexer = nullptr;
    if (lastrunIndexPresent)
    {
        std::cout << termcolor::cyan << "importing local index from last run" << "\r\n" << termcolor::reset;
        lastRunIndexer = new DirectoryIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_LOCAL_LAST_RUN);
    }
    DirectoryIndexer localIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_LOCAL);
    localIndexer.indexonprotobuf(false);

    const auto localDeletions = localIndexer.getDeletions(lastRunIndexer);

    //std::cout << termcolor::white << "local index size: " << localIndexer.count(nullptr, 10) << "\n\r" << termcolor::reset;
    //std::cout << termcolor::white << "remote index size: " << remoteIndexer.count(nullptr, 10) << "\n\r" << termcolor::reset;

    std::cout << termcolor::cyan << "Exporting Sync commands." << "\r\n" << termcolor::reset;

    SyncCommands syncCommands;
    localIndexer.sync(nullptr, lastRunIndexer, &remoteIndexer, lastRunRemoteIndexer, syncCommands, true, false);

    if (syncCommands.empty())
    {
        std::cout << termcolor::cyan << "No sync commands generated." << "\r\n" << termcolor::reset;
        
        // Send SYNC_COMPLETE command to server to indicate client is done
        GrowingBuffer commandbuf;
        size_t commandSize = TcpCommand::kSizeSize + TcpCommand::kCmdSize;
        commandbuf.write(&commandSize, TcpCommand::kSizeSize);
        TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_SYNC_COMPLETE;
        commandbuf.write(&cmd, TcpCommand::kCmdSize);
        TcpCommand *command = TcpCommand::create(commandbuf);
        if (command == nullptr)
        {
            MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to create SyncCompleteCmd");
            return -1;
        }
        TcpCommand::block_transmit();
        command->transmit(args, true);
        TcpCommand::unblock_transmit();
        delete command;

        return 0;
    }

    // Need to put the removals in a list since it invalidates the iterator
    // when we remove elements from syncCommands
    // This is necessary to avoid modifying the list while iterating over it
    std::list <SyncCommand> commandsToRemove;

    for (const auto& path : remoteDeletions) {
        for (auto &command : syncCommands)
        {
            if (command.path1() == "\""+path+"\"")
            {
                commandsToRemove.push_back(command);
                std::cout << termcolor::magenta << "Removing command because of deleted file: " << command.string() << "\r\n" << termcolor::reset;
            }
        }
    }
    for (const auto& path : localDeletions) {
        for (auto &command : syncCommands)
        {
            if (command.path1() == "\""+path+"\"")
            {
                commandsToRemove.push_back(command);
                std::cout << termcolor::magenta << "Removing command because of deleted file: " << command.string() << "\r\n" << termcolor::reset;
            }
        }
    }
     // Remove the commands that match the deletions
    for (const auto& command : commandsToRemove)
    {
        syncCommands.remove(command);
    }

    // Sort the commands based on their priority
    // This will ensure that file creation commands are executed before deletions
    // and that the order of operations is correct
    std::cout << termcolor::cyan << "Sorting sync commands." << "\r\n" << termcolor::reset;
    syncCommands.sortCommands();

    std::cout << termcolor::white << "\r\n" << "Display Generated Sync Commands: ?" << "\r\n" << termcolor::reset;
    std::cout << termcolor::cyan << "Total commands: " << syncCommands.size() << "\r\n" << termcolor::reset;
    
    // Check for auto_sync and dry_run options
    const bool auto_sync = (args.find("auto_sync") != args.end() && args.at("auto_sync") == "true");
    const bool dry_run = (args.find("dry_run") != args.end() && args.at("dry_run") == "true");
    
    std::string answer = "N";
    if (!(auto_sync || dry_run))
    {
        // Show the traditional Y/N prompt
        do
        {
            std::cout << termcolor::white << "Print commands ? (Y/N) " << "\r\n" << termcolor::reset;
            std::cin >> answer;
        } while (!answer.starts_with('y') && !answer.starts_with('Y') &&
                 !answer.starts_with('n') && !answer.starts_with('N'));
    } else if ( dry_run )
    {
        // If dry_run is true, we skip the prompt and print commands
        // but do not execute them
        answer = "Y";
    } else if ( auto_sync )
    {
        // If auto_sync is true, we skip the prompt and execute commands
        // without waiting for user confirmation
        answer = "N";
    }
    
    if (answer.starts_with('y') || answer.starts_with('Y'))
    {
        for (auto &command : syncCommands)
        {
            command.print();
        }
    }
    answer = "N";

    // If auto_sync is true, we skip the prompt and execute commands
    if (!(auto_sync || dry_run))
    {
        // Show the traditional Y/N prompt for execution
        do
        {
            std::cout << termcolor::white << "Execute commands ? (Y/N) " << "\r\n" << termcolor::reset;
            std::cin >> answer;
        } while (!answer.starts_with('y') && !answer.starts_with('Y') &&
                 !answer.starts_with('n') && !answer.starts_with('N'));
    } else if (auto_sync)
    {
        std::cout << termcolor::cyan << "Auto-sync mode enabled, executing commands without confirmation." << "\r\n" << termcolor::reset;
        answer = "Y";
    } else if (dry_run)
    {
        std::cout << termcolor::cyan << "Dry run mode enabled, commands will not be executed." << "\r\n" << termcolor::reset;
        answer = "N";
    }

    // Export to file if not auto_sync mode or if dry_run mode
    if ( FORCE_SYNC_COMMANDS_FILE_EXPORT || !auto_sync || dry_run)
    {
        const std::filesystem::path exportPath = localPath / "sync_commands.sh";
        std::cout << termcolor::cyan << "Exporting sync commands to file: " << exportPath << "\r\n" << termcolor::reset;
        syncCommands.exportToFile(exportPath);
    }

    // Execute commands if not dry_run mode
    if ((answer.starts_with('y') || answer.starts_with('Y')) && (!dry_run || auto_sync))
    {
        for (auto &command : syncCommands)
        {
            command.execute(args,false);

            if (!command.isRemote() && command.isRemoval())
            {
                // If the command is a removal, we need to remove it from the local indexer
                // This is necessary to keep the local indexer in sync with the remote indexer
                std::cout << termcolor::cyan << "Removing path from local index: " << command.path1() << "\r\n" << termcolor::reset;

                DirectoryIndexer::PATH_TYPE pathType = std::filesystem::is_directory(SyncCommand::stripQuotes(command.path1())) ? DirectoryIndexer::PATH_TYPE::FOLDER : DirectoryIndexer::PATH_TYPE::FILE;

                localIndexer.removePath(nullptr, SyncCommand::stripQuotes(command.path1()), pathType);
            }

        }
    }

    if (lastRunIndexer != nullptr)
    {
        delete lastRunIndexer;
        lastRunIndexer = nullptr;
    }

    if (lastRunRemoteIndexer != nullptr)
    {
        delete lastRunRemoteIndexer;
        lastRunRemoteIndexer = nullptr;
    }

    // Finally, store the local index after sync completion
    std::cout << termcolor::cyan << "Storing local index after sync" << "\r\n" << termcolor::reset;
    localIndexer.dumpIndexToFile({});

    // Send SYNC_COMPLETE command to server to indicate client is done
    GrowingBuffer commandbuf;
    size_t commandSize = TcpCommand::kSizeSize + TcpCommand::kCmdSize;
    commandbuf.write(&commandSize, TcpCommand::kSizeSize);
    TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_SYNC_COMPLETE;
    commandbuf.write(&cmd, TcpCommand::kCmdSize);
    TcpCommand *command = TcpCommand::create(commandbuf);
    if (command == nullptr)
    {
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to create SyncCompleteCmd");
        return -1;
    }
    TcpCommand::block_transmit();
    command->transmit(args, true);
    TcpCommand::unblock_transmit();
    delete command;

    std::cout << termcolor::green << "Sent SYNC_COMPLETE to server" << "\r\n" << termcolor::reset;

    return 0;
}

int MkdirCmd::execute(std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), 0);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for MkdirCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    return std::filesystem::create_directory(path) ? 0 : -1;
}

int RmCmd::execute(std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for RmCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    if ( std::filesystem::exists(path) && std::filesystem::remove(path) )
    {
        args["removed_path"] = path;
        // Notify the server about the removed path
        return 0;
    }
    return -1;
}

int FileFetchCmd::execute(std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for FileFetchCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    if (std::filesystem::exists(path)) {
        auto fileargs = args;
        fileargs["path"] = path;
        block_transmit();
        if ( SendFile(fileargs) < 0 ) {
            unblock_transmit();
            std::cerr << termcolor::red << "Error sending file: " << path << "\r\n" << termcolor::reset;
            MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Error sending file: " + path);
            return -1;
        }
        unblock_transmit();
    }
    else {
        std::cerr << termcolor::red << "File not found: " << path << "\r\n" << termcolor::reset;
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "File not found: " + path);
        return -1;
    }
    
    return 0;
}

int FilePushCmd::execute(std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving file path in FilePushCmd" << "\r\n" << termcolor::reset;
        unblock_receive();
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    std::map<std::string, std::string> fileargs = args;
    fileargs["path"] = path;
    
    //std::cout << "DEBUG: FilePushCmd receiving file to path: " << path << "\r\n";
    int ret = ReceiveFile(fileargs);
    unblock_receive();
    return ret;
}

int RemoteLocalCopyCmd::execute(std::map<std::string, std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for RemoteLocalCopyCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    std::string srcPath = extractStringFromPayload(kSrcPathSizeIndex, SEEK_SET);
    std::string destPath = extractStringFromPayload(0, SEEK_CUR);

    try {
        std::filesystem::copy(srcPath, destPath, 
            std::filesystem::copy_options::overwrite_existing | 
            std::filesystem::copy_options::recursive);
        std::cout << termcolor::cyan << "Copied " << srcPath << " to " << destPath << "\r\n" << termcolor::reset;

        // Need to copy the file modified time and permissions
        std::filesystem::permissions(destPath, std::filesystem::status(srcPath).permissions(), std::filesystem::perm_options::replace);
        auto modifiedTime = std::filesystem::last_write_time(srcPath);
        std::filesystem::last_write_time(destPath, modifiedTime);
        std::cout << termcolor::cyan << "Copied permissions and modified time: " << srcPath << " to " << destPath << termcolor::reset << "\r\n";

        return 0;
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << termcolor::red << "Error copying " << srcPath << " to " << destPath  << termcolor::reset
                  << ": " << e.what() << "\r\n";
        return -1;
    }
}

int MessageCmd::execute(std::map<std::string, std::string> &args)
{
    receivePayload(std::stoi(args.at("txsocket")), 0);
    unblock_receive();

    mData.seek(kErrorMessageSizeIndex, SEEK_SET);
    size_t messageSize;
    mData.read(&messageSize, kErrorMessageSizeSize);

    char *message = new char[messageSize + 1];
    mData.read(message, messageSize);
    message[messageSize] = '\0';

    std::cout << termcolor::cyan << "[" << args.at("ip") << "] " << message << "\r\n" << termcolor::reset;
    delete[] message;

    return 0;
}

int RmdirCmd::execute(std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for RmdirCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    if ( std::filesystem::remove_all(path) > 0 )
    {
        args["removed_path"] = path;
        return 0;
    }
    return -1;
}

int SyncCompleteCmd::execute(std::map<std::string, std::string> &args)
{
    unblock_receive();

    GrowingBuffer commandbuf;
    size_t commandSize = TcpCommand::kSizeSize + TcpCommand::kCmdSize;
    commandbuf.write(&commandSize, TcpCommand::kSizeSize);
    TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_SYNC_DONE;
    commandbuf.write(&cmd, TcpCommand::kCmdSize);
    TcpCommand *command = TcpCommand::create(commandbuf);
    if (command == nullptr)
    {
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to create SyncDoneCmd");
        return -1;
    }
    block_transmit();
    command->transmit(args, true);
    unblock_transmit();
    delete command;

    std::cout << termcolor::green << "Sync complete for " << args.at("path") << "\r\n" << termcolor::reset;
    // Check if we should exit after sync (for unit testing)
    if (args.find("exit_after_sync") != args.end() && args.at("exit_after_sync") == "true") {
        std::cout << termcolor::green << "Exiting server after sync completion (unit testing mode)" << "\r\n" << termcolor::reset;
        exit(0);
    }
    return 1;
}

int SyncDoneCmd::execute(std::map<std::string, std::string> &args)
{
    unblock_receive();
    
    std::cout << termcolor::green << "Sync done for " << args.at("path") << "\r\n" << termcolor::reset;
    return 1;
}

int SystemCallCmd::execute(std::map<std::string, std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for SystemCallCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    std::string systemCmd = extractStringFromPayload(kCmdStringIndex, SEEK_SET);

    int ret = std::system(systemCmd.c_str());
    if (ret != 0) {
        std::cerr << termcolor::red << "System command failed with return code: " << ret << "\r\n" << termcolor::reset;
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "System command failed: " + systemCmd);
        return -1;
    }
    return 0;
}

int TouchCmd::execute(std::map<std::string, std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << termcolor::red << "Error receiving payload for TouchCmd" << "\r\n" << termcolor::reset;
        return -1;
    }

    std::string srcPath = extractStringFromPayload(kSrcPathSizeIndex, SEEK_SET);
    std::string modTimeStr = extractStringFromPayload(0, SEEK_CUR);

    std::array<struct timespec, 2> timeSpecsArray{ timespec{.tv_sec = 0, .tv_nsec = UTIME_OMIT}, 
                                                   timespec{.tv_sec = 0, .tv_nsec = 0} };
    DirectoryIndexer::make_timespec(modTimeStr, &timeSpecsArray[1]);
    utimensat(0, srcPath.c_str(), timeSpecsArray.data(), 0);
    return 0;
}