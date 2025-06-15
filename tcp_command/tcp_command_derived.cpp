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

// C++ Standard Library
#include <filesystem>
#include <iostream>
#include <string>

// System Includes
#include <sys/socket.h>

// Project Includes
#include "directory_indexer.h"
#include "sync_command.h"

// Section 3: Defines and Macros

// Section 4: Static Variables

// Section 5: Constructors/Destructors

MessageCmd::MessageCmd(const std::string &message) : TcpCommand()
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
    std::cout << "[localhost] " << message << '\n';
    block_transmit();
    cmd.transmit({{"txsocket", std::to_string(socket)}});
    unblock_transmit();
}

// Section 7: Public/Protected/Private Methods

int IndexFolderCmd::execute(const std::map<std::string,std::string> &args)
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
    std::cout << "starting to index " << args.at("path") << '\n';
    DirectoryIndexer indexer(args.at("path"), true, DirectoryIndexer::INDEX_TYPE_LOCAL);
    DirectoryIndexer *lastindexer = nullptr;
    if ( lastrunIndexPresent )
    {
        lastindexer = new DirectoryIndexer(args.at("path"), DirectoryIndexer::INDEX_TYPE_LOCAL_LAST_RUN);
    }
    indexer.indexonprotobuf(false);

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
    std::vector<std::string> deletions = indexer.getDeletions(lastindexer);
    appendDeletionLogToBuffer(commandbuf, deletions);
    // --- End insertion ---

    TcpCommand * command = TcpCommand::create(commandbuf);
    if ( !command )
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
        //std::cout << "DEBUG: Sending file: " << lastrunIndexFilename << '\n';
        int socket = std::stoi(args.at("txsocket"));
        //std::cout << "DEBUG: Sending file header..." << '\n';
        size_t path_size = lastrunIndexFilename.size();
        
        size_t sent_bytes = sendChunk(socket, &path_size, sizeof(size_t));
        if (sent_bytes < sizeof(size_t)) {
            std::cerr << "Failed to send path size" << '\n';
            unblock_transmit();
            return -1;
        }
        //std::cout << "DEBUG: Path size sent: " << path_size << " bytes" << '\n';
        sent_bytes = sendChunk(socket, lastrunIndexFilename.data(), path_size);
        if (sent_bytes < path_size) {
            std::cerr << "Failed to send file path" << '\n';
            unblock_transmit();
            return -1;
        }
        //std::cout << "DEBUG: File path sent: " << lastrunIndexFilename << '\n';
        size_t file_size = 0;   //file does not exist
        //std::cout << "DEBUG: File size is " << file_size << " bytes" << '\n';
        
        // Send the file size
        //std::cout << "DEBUG: Sending file size: " << file_size << " bytes" << '\n';
        sent_bytes = sendChunk(socket, &file_size, sizeof(size_t));
        if (sent_bytes < sizeof(size_t)) {
            std::cerr << "Failed to send file size" << '\n';
            unblock_transmit();
            return -1;
        }
    }

    unblock_transmit();
    
    return 0;
}

int IndexPayloadCmd::execute(const std::map<std::string, std::string> &args)
{
    // Keep receive mutex locked until we've received everything
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), payloadSize);
    
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for IndexPayloadCmd" << '\n';
        unblock_receive();  // Only unlock on error
        return -1;
    }

    std::string remotePath = extractStringFromPayload(kPayloadIndex, SEEK_SET);
    size_t indexFileNameSize = 0;
    const auto deletions = parseDeletionLogFromBuffer(mData, indexFileNameSize, SEEK_CUR);

    std::cout << "Received index for remote path: " << remotePath << '\n';

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
        std::cerr << "Error receiving remote index file." << '\n';
        unblock_receive();  // Only unlock on error
        return ret;
    }

    fileargs["path"] = remoteLastRunIndexPath;
    ret = ReceiveFile(fileargs);
    if ( ret < 0 )
    {
        std::cerr << "Error receiving remote last run index file." << '\n';
        unblock_receive();  // Only unlock on error
        return ret;
    }
    unblock_receive();  // Only unlock on error

    std::cout << "importing remote index" << '\n';
    DirectoryIndexer remoteIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_REMOTE);
    remoteIndexer.setPath(remotePath);

    DirectoryIndexer *lastRunRemoteIndexer = nullptr;
    if (std::filesystem::exists(lastRunIndexPath))
    {
        std::cout << "importing remote index from last run" << '\n';
        lastRunRemoteIndexer = new DirectoryIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_REMOTE_LAST_RUN);
        lastRunRemoteIndexer->setPath(remotePath);
    }

    std::cout << "remote and local indexes in hand, ready to sync" << '\n';
    DirectoryIndexer *lastRunIndexer = nullptr;
    if (std::filesystem::exists(indexpath))
    {
        std::cout << "importing local index from last run" << '\n';
        lastRunIndexer = new DirectoryIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_LOCAL_LAST_RUN);
    }
    DirectoryIndexer localIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_LOCAL);
    localIndexer.indexonprotobuf(false);

    std::cout << "local index size: " << localIndexer.count(nullptr, 10) << '\n';
    std::cout << "remote index size: " << remoteIndexer.count(nullptr, 10) << '\n';

    std::cout << "Exporting Sync commands." << '\n';

    SyncCommands syncCommands;
    localIndexer.sync(nullptr, lastRunIndexer, &remoteIndexer, lastRunRemoteIndexer, syncCommands, true, false);

    if (syncCommands.empty())
    {
        std::cout << "No sync commands generated." << '\n';
        return 0;
    }

    for (const auto& path : deletions) {
        for (auto &command : syncCommands)
        {
            if (command.path1() == "\""+path+"\"")
            {
                syncCommands.remove(command);
                std::cout << "Removing command because of deleted file: " << command.string() << '\n';
            }
        }
    }

    std::cout << '\n' << "Display Generated Sync Commands: ?" << '\n';
    std::cout << "Total commands: " << syncCommands.size() << '\n';
    
    // Check for auto_sync and dry_run options
    const bool auto_sync = (args.find("auto_sync") != args.end() && args.at("auto_sync") == "true");
    const bool dry_run = (args.find("dry_run") != args.end() && args.at("dry_run") == "true");
    
    std::string answer = "N";
    if (!(auto_sync || dry_run))
    {
        // Show the traditional Y/N prompt
        do
        {
            std::cout << "Print commands ? (Y/N) " << '\n';
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
            std::cout << "Execute commands ? (Y/N) " << '\n';
            std::cin >> answer;
        } while (!answer.starts_with('y') && !answer.starts_with('Y') &&
                 !answer.starts_with('n') && !answer.starts_with('N'));
    } else if (auto_sync)
    {
        std::cout << "Auto-sync mode enabled, executing commands without confirmation." << '\n';
        answer = "Y";
    } else if (dry_run)
    {
        std::cout << "Dry run mode enabled, commands will not be executed." << '\n';
        answer = "N";
    }

    // Export to file if not auto_sync mode or if dry_run mode
    if (!auto_sync || dry_run)
    {
        const std::filesystem::path exportPath = localPath / "sync_commands.sh";
        std::cout << "Exporting sync commands to file: " << exportPath << '\n';
        syncCommands.exportToFile(exportPath);
    }

    // Execute commands if not dry_run mode
    if ((answer.starts_with('y') || answer.starts_with('Y')) && !dry_run)
    {
        for (auto &command : syncCommands)
        {
            command.execute(args,false);
        }
    }

    if (lastRunIndexer)
    {
        delete lastRunIndexer;
        lastRunIndexer = nullptr;
    }

    if (lastRunRemoteIndexer)
    {
        delete lastRunRemoteIndexer;
        lastRunRemoteIndexer = nullptr;
    }

    // Send SYNC_COMPLETE command to server to indicate client is done
    GrowingBuffer commandbuf;
    size_t commandSize = TcpCommand::kSizeSize + TcpCommand::kCmdSize;
    commandbuf.write(&commandSize, TcpCommand::kSizeSize);
    TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_SYNC_COMPLETE;
    commandbuf.write(&cmd, TcpCommand::kCmdSize);
    TcpCommand *command = TcpCommand::create(commandbuf);
    if (!command)
    {
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to create SyncCompleteCmd");
        return -1;
    }
    TcpCommand::block_transmit();
    command->transmit(args, true);
    TcpCommand::unblock_transmit();
    delete command;

    std::cout << "Sent SYNC_COMPLETE to server" << '\n';
    return 0;
}

int MkdirCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), 0);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for MkdirCmd" << '\n';
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    return std::filesystem::create_directory(path) ? 0 : -1;
}

int RmCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for RmCmd" << '\n';
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    return std::filesystem::exists(path) && std::filesystem::remove(path) ? 0 : -1;
}

int FileFetchCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for FileFetchCmd" << '\n';
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    if (std::filesystem::exists(path)) {
        auto fileargs = args;
        fileargs["path"] = path;
        block_transmit();
        if ( SendFile(fileargs) < 0 ) {
            unblock_transmit();
            std::cerr << "Error sending file: " << path << '\n';
            MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Error sending file: " + path);
            return -1;
        }
        unblock_transmit();
    }
    else {
        std::cerr << "File not found: " << path << '\n';
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "File not found: " + path);
        return -1;
    }
    
    return 0;
}

int FilePushCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving file path in FilePushCmd" << '\n';
        unblock_receive();
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    std::map<std::string, std::string> fileargs = args;
    fileargs["path"] = path;
    
    //std::cout << "DEBUG: FilePushCmd receiving file to path: " << path << '\n';
    int ret = ReceiveFile(fileargs);
    unblock_receive();
    return ret;
}

int RemoteLocalCopyCmd::execute(const std::map<std::string, std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for RemoteLocalCopyCmd" << '\n';
        return -1;
    }

    std::string srcPath = extractStringFromPayload(kSrcPathSizeIndex, SEEK_SET);
    std::string destPath = extractStringFromPayload(0, SEEK_CUR);

    try {
        std::filesystem::copy(srcPath, destPath, 
            std::filesystem::copy_options::overwrite_existing | 
            std::filesystem::copy_options::recursive);
        std::cout << "Copied " << srcPath << " to " << destPath << '\n';
        return 0;
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error copying " << srcPath << " to " << destPath 
                  << ": " << e.what() << '\n';
        return -1;
    }
}

int MessageCmd::execute(const std::map<std::string, std::string> &args)
{
    receivePayload(std::stoi(args.at("txsocket")), 0);
    unblock_receive();

    mData.seek(kErrorMessageSizeIndex, SEEK_SET);
    size_t messageSize;
    mData.read(&messageSize, kErrorMessageSizeSize);

    char *message = new char[messageSize + 1];
    mData.read(message, messageSize);
    message[messageSize] = '\0';

    std::cout << "[" << args.at("ip") << "] " << message << '\n';
    delete[] message;

    return 0;
}

int RmdirCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for RmdirCmd" << '\n';
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    return std::filesystem::remove_all(path) > 0 ? 0 : -1;
}

int SyncCompleteCmd::execute(const std::map<std::string, std::string> &args)
{
    unblock_receive();

    GrowingBuffer commandbuf;
    size_t commandSize = TcpCommand::kSizeSize + TcpCommand::kCmdSize;
    commandbuf.write(&commandSize, TcpCommand::kSizeSize);
    TcpCommand::cmd_id_t cmd = TcpCommand::CMD_ID_SYNC_DONE;
    commandbuf.write(&cmd, TcpCommand::kCmdSize);
    TcpCommand *command = TcpCommand::create(commandbuf);
    if (!command)
    {
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to create SyncDoneCmd");
        return -1;
    }
    command->transmit(args, true);
    delete command;

    std::cout << "Sync complete for " << args.at("path") << '\n';
    // Check if we should exit after sync (for unit testing)
    if (args.find("exit_after_sync") != args.end() && args.at("exit_after_sync") == "true") {
        std::cout << "Exiting server after sync completion (unit testing mode)" << '\n';
        exit(0);
    }
    return 1;
}

int SyncDoneCmd::execute(const std::map<std::string, std::string> &args)
{
    unblock_receive();
    
    std::cout << "Sync done for " << args.at("path") << '\n';
    return 1;
}