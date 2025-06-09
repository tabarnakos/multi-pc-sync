#include "tcp_command.h"
#include "directory_indexer.h"
#include "growing_buffer.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <cstring>
#include "sync_command.h"

#define ALLOCATION_SIZE  (1024 * 1024)  //1MiB
#define MAX_PAYLOAD_SIZE (64 * ALLOCATION_SIZE)  //64MiB

std::mutex TcpCommand::TCPSendMutex;
std::mutex TcpCommand::TCPReceiveMutex;

struct HumanReadable
{
    std::uintmax_t size{};
 
private:
    friend std::ostream& operator<<(std::ostream& os, HumanReadable hr)
    {
        int o{};
        double mantissa = hr.size;
        for (; mantissa >= 1024.; mantissa /= 1024., ++o);
        os << std::ceil(mantissa * 10.) / 10. << "BKMGTPE"[o];
        return o ? os << "B (" << hr.size << ')' : os;
    }
};

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

void MessageCmd::sendMessage(const int socket, const std::string &message)
{
    MessageCmd cmd(message);
    std::cout << "[localhost] " << message << std::endl;
    cmd.transmit({{"txsocket", std::to_string(socket)}});
}

TcpCommand::TcpCommand(GrowingBuffer &data) : mData()
{
    const size_t size = data.size();
    uint8_t *buf = new uint8_t[size];
    data.seek(0, SEEK_SET);
    data.read(buf, size);
    mData.write(buf, size);
    delete[] buf;
}

TcpCommand * TcpCommand::create( GrowingBuffer & data )
{
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
        case CMD_ID_MESSAGE:
            return new MessageCmd(data);
    }
}

size_t TcpCommand::cmdSize()
{
    mData.seek(kSizeIndex, SEEK_SET);
    size_t size;
    mData.read(&size, kSizeSize);
    return size;
}

size_t TcpCommand::bufferSize()
{
    return mData.size();
}

TcpCommand::cmd_id_t TcpCommand::command()
{
    mData.seek(kCmdIndex, SEEK_SET);
    cmd_id_t cmd;
    mData.read(&cmd, kCmdSize);
    return cmd;
}

void TcpCommand::setCmdSize(size_t size)
{
    mData.seek(kSizeIndex, SEEK_SET);
    mData.write(&size, kSizeSize);
}

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
std::string TcpCommand::extractStringFromPayload() {
    char buffer[ALLOCATION_SIZE];
    size_t size;
    std::string result;
    
    mData.read(&size, sizeof(size_t));
    mData.read(buffer, size);
    result.assign(buffer, size);
    return result;
}
void TcpCommand::dump(std::ostream& os)
{
    mData.dump(os);
}

int IndexPayloadCmd::execute(const std::map<std::string, std::string> &args)
{
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), 0);
    
    if (bytesReceived < payloadSize) {
        unblock_receive();
        std::cerr << "Error receiving payload for IndexPayloadCmd" << std::endl;
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
    /*
    size_t payloadSize = cmdSize() - kPayloadIndex;
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < payloadSize) {
        std::cerr << "Error receiving payload for FilePushCmd" << std::endl;
        return -1;
    }*/
    // I think it will have the correct path in the TCP command directly.
/*
    std::string path = readPathFromBuffer(kPathSizeIndex);
    auto fileargs = args;
    fileargs["path"] = path;
*/  
    std::map <std::string,std::string> fileargs;
    fileargs["txsocket"] = args.at("txsocket");
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

int TcpCommand::SendFile(const std::map<std::string,std::string> &args)
{
    const auto path = args.at("path");
    const auto txsock_str = args.at("txsocket");
    const int txsock = std::strtol( txsock_str.c_str(), NULL, 10 );
    
    // Packet format:
    // size_t filename_size
    // char filename[indexfilename_size]
    // size_t filedata_size
    // char filedata[indexfiledata_size]

    // Allocate buffers
    char scratchbuf[ALLOCATION_SIZE];
    const size_t filename_size = path.length();
    if (send(txsock, &filename_size, sizeof(size_t), 0) != sizeof(size_t)) {
        std::cerr << "Error sending filename size: " << strerror(errno) << std::endl;
        return -1;
    }
    if (send(txsock, path.data(), filename_size, 0) != (ssize_t)filename_size) {
        std::cerr << "Error sending filename: " << strerror(errno) << std::endl;
        return -1;
    }
    // Send file size
    if (!std::filesystem::exists(path)) {
        constexpr size_t file_size = 0;
        if (send(txsock, &file_size, sizeof(size_t), 0) != sizeof(size_t)) {
            std::cerr << "Error sending zero file size: " << strerror(errno) << std::endl;
            return -1;
        }
        return 0;
    }
    const size_t file_size = std::filesystem::file_size(path);
    if (send(txsock, &file_size, sizeof(size_t), 0) != sizeof(size_t)) {
        std::cerr << "Error sending file size: " << strerror(errno) << std::endl;
        return -1;
    }

    // Open file and send it
    std::ifstream file(path, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
        std::cerr << "Error opening file for sending: " << path << std::endl;
        return -1;
    }
    size_t file_bytes = 0;
    do
    {
        size_t packet_bytes = 0;
        do
        {
            // Read Data from file and send it to the other computer
            const size_t read_size = file.readsome(scratchbuf, sizeof(scratchbuf));
            if (read_size > 0)
            {
                ssize_t sent_bytes = 0;
                while (sent_bytes < (ssize_t)read_size) {
                    ssize_t result = send(txsock, scratchbuf + sent_bytes, read_size - sent_bytes, 0);
                    if (result <= 0) {
                        std::cerr << "Error sending data: " << strerror(errno) << std::endl;
                        file.close();
                        return -1;
                    }
                    sent_bytes += result;
                }
            }
            else
                break; // No more data to read
            packet_bytes += read_size;
        } while (packet_bytes < MAX_PAYLOAD_SIZE);
        file_bytes += packet_bytes;
        std::cout << "Sent " << HumanReadable{file_bytes} << " of " << HumanReadable{file_size} << " bytes." << std::endl;
    } while (file_bytes < file_size);
    // Close file
    file.close();
    return 0;
}

int TcpCommand::ReceiveFile(const std::map<std::string, std::string> &args)
{
    const auto rxsock_str = args.at("txsocket");
    const int rxsock = std::strtol(rxsock_str.c_str(), NULL, 10);

    // Packet format:
    // size_t filename_size
    // char filename[indexfilename_size]
    // size_t filedata_size
    // char filedata[indexfiledata_size]

    // Allocate buffer
    char scratchbuf[ALLOCATION_SIZE];

    
    // Receive filename size
    size_t filename_size = 0;
    recv(rxsock, &filename_size, sizeof(size_t), 0);
    recv(rxsock, scratchbuf, filename_size, 0);
    std::string filename(scratchbuf, filename_size);
    std::cout << "Receiving file: " << filename << std::endl;
    
    if ( args.find("path") != args.end() )
        filename = args.at("path");

    // Open file for writing
    std::ofstream file(filename, std::ios::binary | std::ios::out);
    
    // Receive file size
    size_t file_size = 0;
    recv(rxsock, &file_size, sizeof(size_t), 0);

    size_t file_bytes = 0;
    bool fail = false;
    while (!fail && file_bytes < file_size)
    {
        const size_t remaining_bytes = file_size - file_bytes;
        const size_t read_size = std::min(remaining_bytes, sizeof(scratchbuf));

        const ssize_t received_bytes = recv(rxsock, scratchbuf, read_size, 0);
        if (received_bytes <= 0)
            fail = true;
        else
        {
            file.write(scratchbuf, received_bytes);
            file_bytes += received_bytes;
        }
    }

    file.close();

    return fail ? -1 : 0;
}

int TcpCommand::transmit(const std::map<std::string, std::string> &args, bool calculateSize)
{
    const int txsock = std::stoi(args.at("txsocket"));

    // Get the size of the data to send
    const size_t data_size = mData.size();
    if (calculateSize)
        setCmdSize(mData.size());
    mData.seek(0, SEEK_SET);

    size_t bytes_sent = 0;
    char scratchbuf[ALLOCATION_SIZE];

    block_transmit();
    // Send the actual data in chunks
    while (bytes_sent < data_size)
    {
        const size_t remaining_bytes = data_size - bytes_sent;
        const size_t chunk_size = std::min(remaining_bytes, sizeof(scratchbuf));

        // Read data from mData into the buffer
        size_t read_bytes = mData.read(scratchbuf, chunk_size);

        // Send the chunk over the socket
        ssize_t sent_bytes = send(txsock, scratchbuf, read_bytes, 0);
        if (sent_bytes <= 0)
        {
            std::cerr << "Failed to send data chunk" << std::endl;
            if ( calculateSize )
                unblock_transmit();
            return -1;
        }

        bytes_sent += sent_bytes;
    }
    if ( calculateSize )
        unblock_transmit();

    return 0; // Success
}

TcpCommand* TcpCommand::receiveHeader(const int socket)
{
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

size_t TcpCommand::receivePayload( const int socket, const size_t maxlen )
{
    const size_t payloadSize = cmdSize() - kPayloadIndex;
    if ( cmdSize() <= kPayloadIndex )
    {
        MessageCmd::sendMessage(socket, "Command has empty payload");
        return 0;
    }

    // If maxlen is 0, we will read the entire payload
    // If maxlen is specified, we use it as the limit
    const size_t bytes_to_receive = maxlen == 0 ? payloadSize : std::min<size_t>(payloadSize, maxlen);

    // to avoid having a 12GB buffer, we always use the start of the payload buffer
    mData.seek(kPayloadIndex, SEEK_SET);

    size_t bytesReceived = 0;
    char scratchbuf[ALLOCATION_SIZE]; // Fixed-size buffer

    // Receive the command data in chunks
    while (bytesReceived < bytes_to_receive)
    {
        const size_t remainingBytes = bytes_to_receive - bytesReceived;
        const size_t chunkSize = std::min(remainingBytes, sizeof(scratchbuf));

        const ssize_t receivedBytes = recv(socket, scratchbuf, chunkSize, 0);

        if (receivedBytes <= 0)
        {
            MessageCmd::sendMessage(socket, "Failed to receive command data");
            break;
        }

        // Write the received chunk into the GrowingBuffer
        mData.write(scratchbuf, receivedBytes);
        bytesReceived += receivedBytes;
    }

    return bytesReceived;
}