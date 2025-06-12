// *****************************************************************************
// TCP Command Implementation
// *****************************************************************************

// Section 1: Main Header
#include "tcp_command.h"

// Section 2: Includes
// C Standard Library
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// C++ Standard Library
#include <algorithm>  // For std::min
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>

// System Includes
#include <sys/socket.h>

// Project Includes
#include "human_readable.h"
#include "directory_indexer.h"
#include "sync_command.h"

// Section 3: Defines and Macros
#define ALLOCATION_SIZE  (1024 * 1024)  // 1MiB
#define MAX_PAYLOAD_SIZE (64 * ALLOCATION_SIZE)  // 64MiB
#define MAX_STRING_SIZE  (256 * 1024)  // 256KiB
#define MAX_FILE_SIZE    (64ULL * 1024ULL * 1024ULL * 1024ULL)  // 64GiB

// Section 4: Static Variables
std::mutex TcpCommand::TCPSendMutex;
std::mutex TcpCommand::TCPReceiveMutex;
std::chrono::steady_clock::time_point TcpCommand::lastTransmitTime = std::chrono::steady_clock::now();
float TcpCommand::transmitRateLimit = 0.0f;

// Section 5: Constructors/Destructors
TcpCommand::TcpCommand() : mData() {}

TcpCommand::TcpCommand(GrowingBuffer &data) : mData() {
    const size_t size = data.size();
    uint8_t *buf = new uint8_t[size];
    data.seek(0, SEEK_SET);
    data.read(buf, size);
    mData.write(buf, size);
    delete[] buf;
}
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
void TcpCommand::block_transmit() {
    TCPSendMutex.lock();
    if (transmitRateLimit > 0) {
        auto now = std::chrono::steady_clock::now();
        auto minInterval = std::chrono::microseconds(static_cast<int64_t>(1000000.0f / transmitRateLimit));
        auto elapsed = now - lastTransmitTime;
        if (elapsed < minInterval) {
            std::this_thread::sleep_for(minInterval - elapsed);
        }
        lastTransmitTime = std::chrono::steady_clock::now();
    }
}

void TcpCommand::unblock_transmit() {
    TCPSendMutex.unlock();
}

void TcpCommand::block_receive() {
    TCPReceiveMutex.lock();
}

void TcpCommand::unblock_receive() {
    TCPReceiveMutex.unlock();
}

void TcpCommand::setRateLimit(float rateHz) {
    transmitRateLimit = rateHz;
}

TcpCommand* TcpCommand::create(GrowingBuffer& data) {
    data.seek(kCmdIndex, SEEK_SET);
    cmd_id_t cmd;
    data.read(&cmd, kCmdSize);
    switch (cmd)
    {
        default:
            return nullptr;
        case CMD_ID_INDEX_FOLDER:
            return new IndexFolderCmd(data);
        case CMD_ID_INDEX_PAYLOAD:
            return new IndexPayloadCmd(data);
        case CMD_ID_MKDIR_REQUEST:
            return new MkdirCmd(data);
        case CMD_ID_RM_REQUEST:
            return new RmCmd(data);
        case CMD_ID_FETCH_FILE_REQUEST:
            return new FileFetchCmd(data);
        case CMD_ID_PUSH_FILE:
            return new FilePushCmd(data);
        case CMD_ID_REMOTE_LOCAL_COPY:
            return new RemoteLocalCopyCmd(data);
        case CMD_ID_RMDIR_REQUEST:
            return new RmdirCmd(data);
        case CMD_ID_SYNC_COMPLETE:
            return new SyncCompleteCmd(data);
        case CMD_ID_MESSAGE:
            return new MessageCmd(data);
        case CMD_ID_SYNC_DONE:
            return new SyncDoneCmd(data);
    }
}

TcpCommand* TcpCommand::receiveHeader(const int socket) {
    GrowingBuffer buffer;

    // Receive the size of the command with a 10ms timeout and retry loop
    size_t commandSize = 0;
    fd_set readfds;
    struct timeval timeout;
    int ret;
    ssize_t received = 0;
    char *sizePtr = reinterpret_cast<char*>(&commandSize);
    size_t bytesLeft = kSizeSize;
    while (bytesLeft > 0) {
        FD_ZERO(&readfds);
        FD_SET(socket, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10ms
        
        if (received == 0) {
            block_receive();
        }
        ret = select(socket + 1, &readfds, nullptr, nullptr, &timeout);
        if (ret > 0 && FD_ISSET(socket, &readfds)) {
            ssize_t n = recv(socket, sizePtr + received, bytesLeft-received, 0);
            if (n <= 0) {
                // client disconnected or error
                unblock_receive();
                return nullptr;
            }
            received += n;
            bytesLeft -= n;
        }
        if (received == 0) {
            // No data received, unlock mutex
            unblock_receive();
        }
    }
    buffer.write(commandSize);

    cmd_id_t cmd;
    if (recv(socket, &cmd, kCmdSize, 0) <= 0)
    {
        MessageCmd::sendMessage(socket, "Failed to receive command ID");
        return nullptr;
    }
    buffer.write(cmd);

    TcpCommand *command = TcpCommand::create(buffer);
    if (!command)
    {
        std::cout << "Received unknown command ID: " << cmd << std::endl;
        return nullptr;
    }
    std::cout << "Received command " << command->commandName() << " of size " << commandSize << std::endl;
    
    return command;
}

// Section 7: Public/Protected/Private Methods

int IndexFolderCmd::execute(const std::map<std::string,std::string> &args)
{
    unblock_receive();
    const std::string indexfilename = std::filesystem::path(args.at("path")) / ".folderindex";
	const std::string lastrunIndexFilename = indexfilename + ".last_run";

    if ( std::filesystem::exists(indexfilename) )
    {
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

    fileargs["path"] = lastrunIndexFilename;
    if ( SendFile(fileargs) < 0 )
    {
        unblock_transmit();
        MessageCmd::sendMessage(std::stoi(args.at("txsocket")), "Failed to send last run index file.");
        return -1;
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

    mData.seek(kPayloadIndex, SEEK_SET);
    std::string remotePath = extractStringFromPayload();

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
        return ret;
    }

    fileargs["path"] = remoteLastRunIndexPath;
    ret = ReceiveFile(fileargs);
    if ( ret < 0 )
    {
        std::cerr << "Error receiving remote last run index file." << std::endl;
        return ret;
    }
    unblock_receive();

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
        std::cout << "Execute ? (Y/N) ";
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

    std::string path = readPathFromBuffer(kPathSizeIndex);
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

    std::string path = readPathFromBuffer(kPathSizeIndex);
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

    std::string path = readPathFromBuffer(kPathSizeIndex);
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

    std::string path = readPathFromBuffer(kPathSizeIndex);
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

    std::string srcPath = readPathFromBuffer(kSrcPathSizeIndex, SEEK_SET);
    std::string destPath = readPathFromBuffer(0, SEEK_CUR);

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

const char* TcpCommand::commandName() {
    switch (command()) {
        case CMD_ID_INDEX_FOLDER: return "INDEX_FOLDER";
        case CMD_ID_INDEX_PAYLOAD: return "INDEX_PAYLOAD";
        case CMD_ID_MKDIR_REQUEST: return "MKDIR_REQUEST";
        case CMD_ID_RM_REQUEST: return "RM_REQUEST";
        case CMD_ID_FETCH_FILE_REQUEST: return "FETCH_FILE_REQUEST";
        case CMD_ID_PUSH_FILE: return "PUSH_FILE";
        case CMD_ID_REMOTE_LOCAL_COPY: return "REMOTE_LOCAL_COPY";
        case CMD_ID_MESSAGE: return "MESSAGE";
        case CMD_ID_RMDIR_REQUEST: return "RMDIR_REQUEST";
        case CMD_ID_SYNC_COMPLETE: return "SYNC_COMPLETE";
        case CMD_ID_SYNC_DONE: return "SYNC_DONE";
        default: return "UNKNOWN";
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

    std::string path = readPathFromBuffer(kPathSizeIndex);
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

void MessageCmd::sendMessage(const int socket, const std::string &message)
{
    MessageCmd cmd(message);
    std::cout << "[localhost] " << message << std::endl;
    cmd.transmit({{"txsocket", std::to_string(socket)}});
}

size_t TcpCommand::receivePayload(const int socket, const size_t maxlen) {
    const size_t cmdSize = this->cmdSize();
    const size_t bufSize = maxlen > 0 ? std::min<size_t>(maxlen, ALLOCATION_SIZE) : ALLOCATION_SIZE;
    uint8_t* buffer = new uint8_t[bufSize];
    size_t totalReceived = 0;
    const size_t targetSize = maxlen > 0 ? std::min<size_t>(maxlen, cmdSize - mData.size()) : cmdSize - mData.size();

    std::cout << "DEBUG: receivePayload starting with"
              << "\n  Target size: " << targetSize << " bytes"
              << "\n  Command size: " << cmdSize << " bytes"
              << "\n  Current buffer size: " << mData.size() << " bytes" << std::endl;

    if (mData.seek(mData.size(), SEEK_SET) < 0) {
        std::cerr << "Error seeking to end of buffer" << std::endl;
        delete[] buffer;
        return 0;
    }

    while (totalReceived < targetSize) {
        size_t remainingBytes = targetSize - totalReceived;
        size_t bytesToReceive = std::min<size_t>(remainingBytes, bufSize);
        
        ssize_t n = recv(socket, buffer, bytesToReceive, 0);
        if (n <= 0) {
            if (n == 0) {
                std::cerr << "Connection closed by peer after receiving " << totalReceived << " bytes" << std::endl;
            } else {
                std::cerr << "Receive error after " << totalReceived << " bytes: " << strerror(errno) << std::endl;
            }
            delete[] buffer;
            return totalReceived;
        }

        if (mData.write(buffer, n) != n) {
            std::cerr << "Error writing " << n << " bytes to buffer" << std::endl;
            delete[] buffer;
            return totalReceived;
        }

        totalReceived += n;
        std::cout << "DEBUG: receivePayload received " << n << " bytes (total: " << totalReceived 
                  << "/" << targetSize << ")" << std::endl;
    }

    delete[] buffer;
    return totalReceived;
}

int TcpCommand::transmit(const std::map<std::string, std::string>& args, bool calculateSize) {
    if (calculateSize) {
        size_t size = mData.size();
        mData.seek(kSizeIndex, SEEK_SET);
        mData.write(&size, kSizeSize);
    }

    int socket = std::stoi(args.at("txsocket"));
    uint8_t* buffer = new uint8_t[ALLOCATION_SIZE];
    mData.seek(0, SEEK_SET);
    size_t remaining = mData.size();
    
    while (remaining > 0) {
        size_t readSize = std::min(remaining, (size_t)ALLOCATION_SIZE);
        mData.read(buffer, readSize);
        ssize_t sent = send(socket, buffer, readSize, 0);
        if (sent <= 0) {
            delete[] buffer;
            return -1;
        }
        remaining -= sent;
    }
    
    delete[] buffer;
    return 0;
}

void TcpCommand::dump(std::ostream& os) {
#if 0
    size_t size = cmdSize();
    cmd_id_t cmd = command();
    os << "Command size: " << size << "\n";
    os << "Command ID: " << static_cast<int>(cmd) << " (" << commandName() << ")\n";
#endif
    mData.dump(os);
}

size_t TcpCommand::cmdSize() {
    return mData.operator[]<size_t>(kSizeIndex);
}

void TcpCommand::setCmdSize(size_t size) {
    mData.seek(kSizeIndex, SEEK_SET);
    mData.write(&size, kSizeSize);
}

size_t TcpCommand::bufferSize() {
    return mData.size();
}

TcpCommand::cmd_id_t TcpCommand::command() {
    mData.seek(kCmdIndex, SEEK_SET);
    cmd_id_t cmd;
    mData.read(&cmd, kCmdSize);
    return cmd;
}

std::string TcpCommand::readPathFromBuffer(size_t off, int whence) {
    mData.seek(off, whence);
    size_t pathSize;
    mData.read(&pathSize, sizeof(size_t));
    
    std::string path(pathSize, '\0');
    mData.read(&path[0], pathSize);
    return path;
}

std::string TcpCommand::extractStringFromPayload() {
    size_t strSize;
    mData.read(&strSize, sizeof(size_t));
    if (strSize > MAX_STRING_SIZE) {
        std::cerr << "String size exceeds maximum allowed size: " << strSize << " > " << MAX_STRING_SIZE << std::endl;
        return "";
    }
    std::string str(strSize, '\0');
    mData.read(&str[0], strSize);
    return str;
}

void TcpCommand::executeInDetachedThread(TcpCommand* command, const std::map<std::string, std::string>& args) {
    std::thread([command, args]() {
        command->execute(args);
        delete command;
    }).detach();
}

int TcpCommand::SendFile(const std::map<std::string, std::string>& args) {
    const std::string& path = args.at("path");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open file for reading: " << path << " - " << strerror(errno) << std::endl;
        return -1;
    }
    std::cout << "DEBUG: Sending file: " << path << std::endl;
    int socket = std::stoi(args.at("txsocket"));
    std::cout << "DEBUG: Sending file header..." << std::endl;
    size_t path_size = path.size();
    
    size_t sent_bytes = sendChunk(socket, &path_size, sizeof(size_t));
    if (sent_bytes < sizeof(size_t)) {
        std::cerr << "Failed to send path size" << std::endl;
        return -1;
    }
    std::cout << "DEBUG: Path size sent: " << path_size << " bytes" << std::endl;
    sent_bytes = sendChunk(socket, path.data(), path_size);
    if (sent_bytes < path_size) {
        std::cerr << "Failed to send file path" << std::endl;
        return -1;
    }
    std::cout << "DEBUG: File path sent: " << path << std::endl;
    // Get the file size
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::cout << "DEBUG: File size is " << file_size << " bytes" << std::endl;
    if (file_size < 0 || file_size > MAX_FILE_SIZE) {
        std::cerr << "Invalid file size: " << file_size << " bytes" << std::endl;
        return -1;
    }
    // Send the file size
    size_t file_size_net = static_cast<size_t>(file_size);
    std::cout << "DEBUG: Sending file size: " << file_size_net << " bytes" << std::endl;
    sent_bytes = sendChunk(socket, &file_size_net, sizeof(size_t));
    if (sent_bytes < sizeof(size_t)) {
        std::cerr << "Failed to send file size" << std::endl;
        return -1;
    }
    
    // Now send the file contents in chunks
    uint8_t* buffer = new uint8_t[ALLOCATION_SIZE];
    size_t total_bytes_sent = 0;

    while (total_bytes_sent < file_size) {
        size_t bytes_to_read = std::min<size_t>(ALLOCATION_SIZE, file_size - total_bytes_sent);
        
        // Read chunk from file
        if (!file.read(reinterpret_cast<char*>(buffer), bytes_to_read)) {
            std::cerr << "Failed to read from file after " << HumanReadable(total_bytes_sent) << " bytes" << std::endl;
            delete[] buffer;
            return -1;
        }

        size_t chunk_sent = sendChunk(socket, buffer, bytes_to_read);
        if (chunk_sent < bytes_to_read) {
            std::cerr << "Failed to send file chunk after " << HumanReadable(total_bytes_sent) << " bytes" << std::endl;
            delete[] buffer;
            return -1;
        }
        total_bytes_sent += chunk_sent;
        std::cout << "DEBUG: Sent chunk of " << chunk_sent 
                  << " bytes (total sent: " << HumanReadable(total_bytes_sent) 
                  << "/" << HumanReadable(file_size) << ")" << std::endl;

        // Force flush output to ensure logs appear in real-time
        std::cout << "Progress: " << HumanReadable(total_bytes_sent) << " of " << HumanReadable(file_size) 
                  << " (" << (total_bytes_sent * 100 / file_size) << "%)" << std::endl;
    }

    std::cout << "DEBUG: File send complete. Total bytes sent: " << HumanReadable(total_bytes_sent) 
              << " of " << HumanReadable(file_size) << " expected" << std::endl;

    delete[] buffer;
    file.close();
    std::cout << "DEBUG: File " << path << " sent successfully." << std::endl;
    return 0;
}

size_t TcpCommand::sendChunk(const int socket, const void* buffer, size_t len)
{
    size_t chunk_sent = 0;
    const uint8_t* buf = static_cast<const uint8_t*>(buffer);
    while (chunk_sent < len) {
        ssize_t n = send(socket, buf + chunk_sent, len - chunk_sent, 0);
        if (n <= 0) {
            if (n == 0) {
                std::cerr << "Connection closed by peer after sending "
                          << HumanReadable(chunk_sent) << std::endl;
            } else {
                std::cerr << "Send error at " << HumanReadable(chunk_sent)
                          << ": " << strerror(errno) << std::endl;
            }
            return -1;
        }
        std::cout << "DEBUG: Sent chunk of " << n
                  << " (chunk progress: " << HumanReadable(chunk_sent) << "/" << HumanReadable(len) << ")" << std::endl;
        chunk_sent += n;
    }
    return chunk_sent;
}

size_t TcpCommand::ReceiveChunk(const int socket, void* buffer, size_t len)
{
    size_t chunk_received = 0;

    while (chunk_received < len) {
        ssize_t n = recv(socket, static_cast<uint8_t*>(buffer) + chunk_received, len - chunk_received, 0);
        if (n <= 0) {
            if (n == 0) {
                std::cerr << "Connection closed by peer after receiving "
                            << chunk_received << " bytes" << std::endl;
            } else {
                std::cerr << "Receive error at " << chunk_received 
                            << " bytes: " << strerror(errno) << std::endl;
            }
            return -1;
        }

        std::cout << "DEBUG: Received chunk of " << HumanReadable(n) 
                    << "(chunk progress: " << HumanReadable(chunk_received) 
                    << "/" << HumanReadable(len) << std::endl;
        chunk_received += n;
    }

    return chunk_received;
}


int TcpCommand::ReceiveFile(const std::map<std::string, std::string>& args) {
    std::cout << "DEBUG: Starting ReceiveFile..." << std::endl;
    
    const std::string& path = args.at("path");
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file for writing: " << path << " - " << strerror(errno) << std::endl;
        return -1;
    }

    int socket = std::stoi(args.at("txsocket"));

    // Now receive the file data in chunks
    size_t total_bytes_received = 0;
            
    size_t path_size;
    int received_bytes = ReceiveChunk(socket, &path_size, kSizeSize);
    if (received_bytes < kSizeSize) {
        std::cerr << "Failed to receive path size" << std::endl;
        return -1;
    }
    if (path_size > MAX_STRING_SIZE) {
        std::cerr << "Path size exceeds maximum allowed size: " << path_size << " > " << MAX_STRING_SIZE << std::endl;
        return -1;
    }
    std::string received_path(path_size, '\0');
    received_bytes = ReceiveChunk(socket, &received_path[0], path_size);
    if (received_bytes < path_size) {
        std::cerr << "Failed to receive file path" << std::endl;
        return -1;
    }
    std::cout << "DEBUG: Received file path: " << received_path << std::endl;
    
    size_t file_size;
    received_bytes = ReceiveChunk(socket, &file_size, kSizeSize);
    if (received_bytes < kSizeSize) {
        std::cerr << "Failed to receive file size" << std::endl;
        return -1;
    }
    if (file_size > MAX_FILE_SIZE) {
        std::cerr << "File size exceeds maximum allowed size: " << file_size << " > " << MAX_FILE_SIZE << std::endl;
        return -1;
    }
    std::cout << "DEBUG: Expected file size: " << HumanReadable(file_size) << std::endl;
            
    uint8_t* buffer = new uint8_t[ALLOCATION_SIZE];
    received_bytes = 0;
            
    while (received_bytes < file_size) {
        size_t bytes_to_read = std::min<size_t>(ALLOCATION_SIZE, file_size - received_bytes);

        size_t chunk_received = ReceiveChunk(socket, buffer, bytes_to_read);
        if (chunk_received < 0) {
            std::cerr << "Error receiving file chunk after " << HumanReadable(received_bytes) << std::endl;
            delete[] buffer;
            file.flush();
            return -1;
        }
        if (chunk_received == 0) {
            std::cerr << "No more data received, connection may have been closed prematurely" << std::endl;
            delete[] buffer;
            file.flush();
            return -1;
        }
        
        // Write the received chunk to file
        if (!file.write(reinterpret_cast<char*>(buffer), chunk_received)) {
            std::cerr << "Failed to write to file at " << HumanReadable(received_bytes) << " bytes" << std::endl;
            delete[] buffer;
            file.flush();
            return -1;
        }
        received_bytes += chunk_received;
        
        // Force flush output to ensure logs appear in real-time
        std::cout << "Progress: " << HumanReadable(received_bytes) << " of " << HumanReadable(file_size) 
                  << " (" << (received_bytes * 100. / file_size) << "%)" << std::endl;
    }
    std::cout << "DEBUG: File receive complete. Wrote: " << HumanReadable(received_bytes)
              << " of " << HumanReadable(file_size) << " to disk" << std::endl;  
    file.close();   // Will automatically flush the file buffer
    delete[] buffer;
    return 0;
}

// Section 8: Helper Functions
TcpCommand* TcpCommand::create(cmd_id_t cmd, std::map<std::string, std::string>& args) {
    TcpCommand* command = nullptr;
    GrowingBuffer buffer;
    size_t commandSize = kSizeSize + kCmdSize;  // header only
    buffer.write(&commandSize, kSizeSize);
    buffer.write(&cmd, kCmdSize);

    switch (cmd) {
        case CMD_ID_INDEX_FOLDER:
            // No payload for INDEX_FOLDER
            command = new IndexFolderCmd(buffer);
            break;

        case CMD_ID_MKDIR_REQUEST:
        case CMD_ID_RM_REQUEST:
        case CMD_ID_RMDIR_REQUEST:
        case CMD_ID_FETCH_FILE_REQUEST:
            if (args.find("path1") == args.end()) {
                std::cerr << "Error: Missing required 'path1' argument for path-based command" << std::endl;
                return nullptr;
            }
            {
                size_t pathSize = args["path1"].size();
                buffer.write(&pathSize, sizeof(size_t));
                buffer.write(args["path1"].data(), pathSize);
                commandSize += sizeof(size_t) + pathSize;
                switch (cmd) {
                    case CMD_ID_MKDIR_REQUEST:
                        command = new MkdirCmd(buffer);
                        break;
                    case CMD_ID_RM_REQUEST:
                        command = new RmCmd(buffer);
                        break;
                    case CMD_ID_RMDIR_REQUEST:
                        command = new RmdirCmd(buffer);
                        break;
                    case CMD_ID_FETCH_FILE_REQUEST:
                        command = new FileFetchCmd(buffer);
                        break;
                    default:
                        break;
                }
            }
            break;

        case CMD_ID_PUSH_FILE:
            // No payload for PUSH_FILE, path is sent in SendFile
            command = new FilePushCmd(buffer);
            break;

        case CMD_ID_REMOTE_LOCAL_COPY:
            if (args.find("path1") == args.end() || args.find("path2") == args.end()) {
                std::cerr << "Error: Missing required 'path1' or 'path2' argument for REMOTE_LOCAL_COPY command" << std::endl;
                return nullptr;
            }
            {
                size_t path1Size = args["path1"].size();
                size_t path2Size = args["path2"].size();
                buffer.write(&path1Size, sizeof(size_t));
                buffer.write(args["path1"].data(), path1Size);
                buffer.write(&path2Size, sizeof(size_t));
                buffer.write(args["path2"].data(), path2Size);
                commandSize += sizeof(size_t) + path1Size + sizeof(size_t) + path2Size;
                command = new RemoteLocalCopyCmd(buffer);
            }
            break;

        case CMD_ID_MESSAGE:
            if (args.find("path1") == args.end()) {
                std::cerr << "Error: Missing required 'path1' argument for MESSAGE command" << std::endl;
                return nullptr;
            }
            command = new MessageCmd(args["path1"]);
            break;

        case CMD_ID_SYNC_COMPLETE:
            command = new SyncCompleteCmd(buffer);
            break;
        case CMD_ID_SYNC_DONE:
            command = new SyncDoneCmd(buffer);
            break;
        default:
            std::cerr << "Error: Unknown command type: " << cmd << std::endl;
            return nullptr;
    }

    if (!command) {
        std::cerr << "Error: Failed to create command object" << std::endl;
        return nullptr;
    }

    // Update the command size in the buffer after payload is written
    buffer.seek(kSizeIndex, SEEK_SET);
    buffer.write(&commandSize, kSizeSize);

    return command;
}