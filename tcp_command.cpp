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
    size_t commandSize = kPayloadIndex +
                         kSizeSize + indexfilename.length() + 
                         kSizeSize + std::filesystem::file_size(indexfilename) + 
                         kSizeSize + lastrunIndexFilename.length() + 
                         kSizeSize + (std::filesystem::exists(lastrunIndexFilename) ? std::filesystem::file_size(lastrunIndexFilename) : 0);
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
    command->transmit(args);
    delete command;

    // Now send the index files
    auto fileargs = args;
    fileargs["path"] = indexfilename;
    SendFile(fileargs);
    fileargs["path"] = lastrunIndexFilename;
    SendFile(fileargs);
    
    return 0;
}
IndexPayloadCmd::Paths IndexPayloadCmd::extractPathsFromPayload() {
    Paths paths;

    // Extract remotePath
    size_t remotePathSize;
    mData.seek(kPayloadIndex, SEEK_SET);
    mData.read(&remotePathSize, sizeof(size_t));
    paths.remote = new char[remotePathSize + 1];
    mData.read(paths.remote, remotePathSize);
    paths.remote[remotePathSize] = '\0';

    // Extract lastRunIndexPath
    size_t lastRunIndexPathSize;
    mData.read(&lastRunIndexPathSize, sizeof(size_t));
    paths.lastRunIndex = new char[lastRunIndexPathSize + 1];
    mData.read(paths.lastRunIndex, lastRunIndexPathSize);
    paths.lastRunIndex[lastRunIndexPathSize] = '\0';

    // Extract localPath
    size_t localPathSize;
    mData.read(&localPathSize, sizeof(size_t));
    paths.local = new char[localPathSize + 1];
    mData.read(paths.local, localPathSize);
    paths.local[localPathSize] = '\0';

    return paths;
}

int IndexPayloadCmd::execute(const std::map<std::string, std::string> &args)
{
    size_t commandSize = cmdSize();
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < commandSize) {
        std::cerr << "Error receiving payload for IndexPayloadCmd" << std::endl;
        return -1;
    }

    // Use the implemented function
    auto paths = extractPathsFromPayload();
    char *remotePath = paths.remote;
    char *lastRunIndexPath = paths.lastRunIndex;
    char *localPath = paths.local;

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

    DirectoryIndexer localIndexer(localPath, true, DirectoryIndexer::INDEX_TYPE_LOCAL_LAST_RUN);

    std::cout << "local index size: " << localIndexer.count() << std::endl;
    std::cout << "remote index size: " << remoteIndexer.count() << std::endl;

    std::cout << "Exporting Sync commands." << std::endl;

    SyncCommands syncCommands;
    localIndexer.sync(nullptr, nullptr, &remoteIndexer, lastRunRemoteIndexer, syncCommands, true);

    std::cout << std::endl << "Sync Commands: " << std::endl;
    for (auto &command : syncCommands)
    {
        command.print();
    }

    std::filesystem::path exportPath = std::filesystem::path(localPath) / "sync_commands.sh";
    syncCommands.exportToFile(exportPath);

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
            command.execute(args,true);
        }
    }

    if (lastRunRemoteIndexer)
    {
        delete lastRunRemoteIndexer;
        lastRunRemoteIndexer = nullptr;
    }

    delete[] remotePath;
    delete[] lastRunIndexPath;
    delete[] localPath;

    return 0;
}

int MkdirCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t commandSize = cmdSize();
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < commandSize) {
        std::cerr << "Error receiving payload for MkdirCmd" << std::endl;
        return -1;
    }

    std::string path = readPathFromBuffer(kPathSizeIndex);
    return std::filesystem::create_directory(path) ? 0 : -1;
}

int RmCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t commandSize = cmdSize();
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < commandSize) {
        std::cerr << "Error receiving payload for RmCmd" << std::endl;
        return -1;
    }

    std::string path = readPathFromBuffer(kPathSizeIndex);
    return std::filesystem::exists(path) && std::filesystem::remove(path) ? 0 : -1;
}

int RmdirCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t commandSize = cmdSize();
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < commandSize) {
        std::cerr << "Error receiving payload for RmdirCmd" << std::endl;
        return -1;
    }

    std::string path = readPathFromBuffer(kPathSizeIndex);
    return std::filesystem::remove_all(path) > 0 ? 0 : -1;
}

int FileFetchCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t commandSize = cmdSize();
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < commandSize) {
        std::cerr << "Error receiving payload for FileFetchCmd" << std::endl;
        return -1;
    }

    std::string path = readPathFromBuffer(kPathSizeIndex);
    if (std::filesystem::exists(path)) {
        auto fileargs = args;
        fileargs["path"] = path;
        SendFile(fileargs);
    }
    
    return 0;
}

int FilePushCmd::execute(const std::map<std::string,std::string> &args)
{
    size_t commandSize = cmdSize();
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < commandSize) {
        std::cerr << "Error receiving payload for FilePushCmd" << std::endl;
        return -1;
    }

    std::string path = readPathFromBuffer(kPathSizeIndex);
    auto fileargs = args;
    fileargs["path"] = path;
    
    return ReceiveFile(fileargs);
}

int RemoteLocalCopyCmd::execute(const std::map<std::string, std::string> &args)
{
    size_t commandSize = cmdSize();
    size_t bytesReceived = receivePayload(std::stoi(args.at("txsocket")), ALLOCATION_SIZE);
    if (bytesReceived < commandSize) {
        std::cerr << "Error receiving payload for RemoteLocalCopyCmd" << std::endl;
        return -1;
    }

    std::string srcPath = readPathFromBuffer(kSrcPathSizeIndex);
    std::string destPath = readPathFromBuffer(kDestPathSizeIndex);

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

void TcpCommand::SendFile(const std::map<std::string,std::string> &args)
{
    const auto path = args.at("path");
    const auto txsock_str = args.at("txsocket");
    const int txsock = std::strtol( txsock_str.c_str(), NULL, 10 );
    
    // Packet format:
    // size_t filename_size
    // char filename[indexfilename_size]
    // size_t filedata_size
    // char filedata[indexfiledata_size]

    //Allocate buffers
    char scratchbuf[ALLOCATION_SIZE];
    const size_t filename_size = path.length();
    send( txsock, &filename_size, sizeof(size_t), 0 );
    send( txsock, path.data(), filename_size, 0 );
    //Send file size
    if ( !std::filesystem::exists(path) )
    {
        constexpr size_t file_size = 0;
        send( txsock, &file_size, sizeof(size_t), 0 );
        return;
    }
    const size_t file_size = std::filesystem::file_size(path);
    send( txsock, &file_size, sizeof(size_t), 0 );

    //open file and send it
    std::ifstream file(path, std::ios::binary | std::ios::in);
    size_t file_bytes = 0;
    do
    {
        size_t packet_bytes = 0;
        do
        {
            //Read Data from file and send it so the other computer
            const size_t read_size = file.readsome(scratchbuf, sizeof(scratchbuf) );
            send( txsock, scratchbuf, read_size, 0 );
            packet_bytes += read_size;
        } while (  packet_bytes < MAX_PAYLOAD_SIZE );
        file_bytes += packet_bytes;
        std::cout << "Sent " << HumanReadable{file_bytes} << " of " << HumanReadable{file_size} << " bytes." << std::endl;
    } while ( file_bytes < file_size );
    //Close file
    file.close();
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
    const std::string filename(scratchbuf, filename_size);
    std::cout << "Receiving file: " << filename << std::endl;
    
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

int TcpCommand::transmit(const std::map<std::string, std::string> &args)
{
    const int txsock = std::stoi(args.at("txsocket"));

    // Get the size of the data to send
    const size_t data_size = mData.size();
    setCmdSize(data_size);
    mData.seek(0, SEEK_SET);

    size_t bytes_sent = 0;
    char scratchbuf[ALLOCATION_SIZE];

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
            return -1;
        }

        bytes_sent += sent_bytes;
    }

    return 0; // Success
}

TcpCommand* TcpCommand::receiveHeader(const int socket)
{
    GrowingBuffer buffer;

    // Receive the size of the command
    size_t commandSize = 0;
    if (recv(socket, &commandSize, kSizeSize, 0) <= 0)
    {
        MessageCmd::sendMessage(socket, "Failed to receive command size");
        return nullptr;
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
        MessageCmd::sendMessage(socket, "Unknown command received");
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