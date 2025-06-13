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
    std::cout << "[localhost] " << message << std::endl;
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
        std::filesystem::rename( indexfilename, lastrunIndexFilename );
    }
    
    /* kick off the indexing */
    std::cout << "starting to index " << args.at("path") << std::endl;
    DirectoryIndexer indexer(args.at("path"), true, DirectoryIndexer::INDEX_TYPE_LOCAL);
    indexer.indexonprotobuf(false);

    const size_t path_lenght = args.at("path").length();

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
    commandbuf.write(path_lenght);
    commandbuf.write(args.at("path").data(), path_lenght);

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
        std::cout << "DEBUG: Sending file: " << lastrunIndexFilename << std::endl;
        int socket = std::stoi(args.at("txsocket"));
        std::cout << "DEBUG: Sending file header..." << std::endl;
        size_t path_size = lastrunIndexFilename.size();
        
        size_t sent_bytes = sendChunk(socket, &path_size, sizeof(size_t));
        if (sent_bytes < sizeof(size_t)) {
            std::cerr << "Failed to send path size" << std::endl;
            unblock_transmit();
            return -1;
        }
        std::cout << "DEBUG: Path size sent: " << path_size << " bytes" << std::endl;
        sent_bytes = sendChunk(socket, lastrunIndexFilename.data(), path_size);
        if (sent_bytes < path_size) {
            std::cerr << "Failed to send file path" << std::endl;
            unblock_transmit();
            return -1;
        }
        std::cout << "DEBUG: File path sent: " << lastrunIndexFilename << std::endl;
        size_t file_size = 0;   //file does not exist
        std::cout << "DEBUG: File size is " << file_size << " bytes" << std::endl;
        
        // Send the file size
        std::cout << "DEBUG: Sending file size: " << file_size << " bytes" << std::endl;
        sent_bytes = sendChunk(socket, &file_size, sizeof(size_t));
        if (sent_bytes < sizeof(size_t)) {
            std::cerr << "Failed to send file size" << std::endl;
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
        std::cerr << "Error receiving payload for IndexPayloadCmd" << std::endl;
        unblock_receive();  // Only unlock on error
        return -1;
    }

    std::string remotePath = extractStringFromPayload(kPayloadIndex, SEEK_SET);

    std::cout << "Received index for remote path: " << remotePath << std::endl;

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
        std::cerr << "Error receiving remote index file." << std::endl;
        unblock_receive();  // Only unlock on error
        return ret;
    }

    fileargs["path"] = remoteLastRunIndexPath;
    ret = ReceiveFile(fileargs);
    if ( ret < 0 )
    {
        std::cerr << "Error receiving remote last run index file." << std::endl;
        unblock_receive();  // Only unlock on error
        return ret;
    }
    unblock_receive();  // Only unlock on error

    std::cout << "importing remote index" << std::endl;
    DirectoryIndexer remoteIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_REMOTE);
    remoteIndexer.setPath(remotePath);

    DirectoryIndexer *lastRunRemoteIndexer = nullptr;
    if (std::filesystem::exists(lastRunIndexPath))
    {
        std::cout << "importing remote index from last run" << std::endl;
        lastRunRemoteIndexer = new DirectoryIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_REMOTE_LAST_RUN);
        lastRunRemoteIndexer->setPath(remotePath);
    }

    std::cout << "remote and local indexes in hand, ready to sync" << std::endl;
    DirectoryIndexer *lastRunIndexer = nullptr;
    if (std::filesystem::exists(indexpath))
    {
        std::cout << "importing local index from last run" << std::endl;
        lastRunIndexer = new DirectoryIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_LOCAL_LAST_RUN);
    }
    DirectoryIndexer localIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_LOCAL);
    localIndexer.indexonprotobuf(false);

    std::cout << "local index size: " << localIndexer.count(nullptr, 10) << std::endl;
    std::cout << "remote index size: " << remoteIndexer.count(nullptr, 10) << std::endl;

    std::cout << "Exporting Sync commands." << std::endl;

    SyncCommands syncCommands;
    localIndexer.sync(nullptr, lastRunIndexer, &remoteIndexer, lastRunRemoteIndexer, syncCommands, true, false);

    if (syncCommands.empty())
    {
        std::cout << "No sync commands generated." << std::endl;
        return 0;
    }

    std::cout << std::endl << "Display Generated Sync Commands: ?" << std::endl;
    std::cout << "Total commands: " << syncCommands.size() << std::endl;
    std::string answer;
    do
    {
        std::cout << "Execute ? (Y/N) " << std::endl;
        std::cin >> answer;
    } while (!answer.starts_with('y') && !answer.starts_with('Y') &&
             !answer.starts_with('n') && !answer.starts_with('N'));
    
    if (answer.starts_with('y') || answer.starts_with('Y'))
    {
        for (auto &command : syncCommands)
        {
            command.print();
        }
    }
    std::filesystem::path exportPath = localPath / "sync_commands.sh";
    std::cout << "Exporting sync commands to file: " << exportPath << std::endl;
    syncCommands.exportToFile(exportPath);

    answer = "Y";/* // Uncomment this block to ask for confirmation before executing commands
    do
    {
        std::cout << "Execute ? (Y/N) ";
        std::cin >> answer;
    } while (!answer.starts_with('y') && !answer.starts_with('Y') &&
             !answer.starts_with('n') && !answer.starts_with('N'));*/

    if (answer.starts_with('y') || answer.starts_with('Y'))
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
    return 0;
}

int MkdirCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), 0);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for MkdirCmd" << std::endl;
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
        std::cerr << "Error receiving payload for RmCmd" << std::endl;
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
        std::cerr << "Error receiving payload for FileFetchCmd" << std::endl;
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    if (std::filesystem::exists(path)) {
        auto fileargs = args;
        fileargs["path"] = path;
        block_transmit();
        if ( SendFile(fileargs) < 0 ) {
            unblock_transmit();
            std::cerr << "Error sending file: " << path << std::endl;
            MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Error sending file: " + path);
            return -1;
        }
        unblock_transmit();
    }
    else {
        std::cerr << "File not found: " << path << std::endl;
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
        std::cerr << "Error receiving file path in FilePushCmd" << std::endl;
        unblock_receive();
        return -1;
    }

    std::string path = extractStringFromPayload(kPathSizeIndex);
    std::map<std::string, std::string> fileargs = args;
    fileargs["path"] = path;
    
    std::cout << "DEBUG: FilePushCmd receiving file to path: " << path << std::endl;
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
        std::cerr << "Error receiving payload for RemoteLocalCopyCmd" << std::endl;
        return -1;
    }

    std::string srcPath = extractStringFromPayload(kSrcPathSizeIndex, SEEK_SET);
    std::string destPath = extractStringFromPayload(0, SEEK_CUR);

    try {
        std::filesystem::copy(srcPath, destPath, 
            std::filesystem::copy_options::overwrite_existing | 
            std::filesystem::copy_options::recursive);
        std::cout << "Copied " << srcPath << " to " << destPath << std::endl;
        return 0;
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error copying " << srcPath << " to " << destPath 
                  << ": " << e.what() << std::endl;
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

    std::cout << "[" << args.at("ip") << "] " << message << std::endl;
    delete[] message;

    return 0;
}

int RmdirCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    unblock_receive();
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for RmdirCmd" << std::endl;
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

    std::cout << "Sync complete for " << args.at("path") << std::endl;
    return 1;
}

int SyncDoneCmd::execute(const std::map<std::string, std::string> &args)
{
    unblock_receive();
    
    std::cout << "Sync done for " << args.at("path") << std::endl;
    return 1;
}