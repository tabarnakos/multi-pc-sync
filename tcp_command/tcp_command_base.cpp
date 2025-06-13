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
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

// System Includes
#include <sys/socket.h>

// Project Includes
#include "human_readable.h"

// Section 3: Defines and Macros

// Section 4: Static Variables
std::binary_semaphore TcpCommand::TCPSendSemaphore{1};
std::binary_semaphore TcpCommand::TCPReceiveSemaphore{1};
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

// Section 6: Static Methods

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

int TcpCommand::ReceiveFile(const std::map<std::string, std::string>& args) {
    std::cout << "DEBUG: Starting ReceiveFile..." << std::endl;
    
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

    if (file_size)
    {
        const std::string& path = args.at("path");
        std::ofstream file(path, std::ios::binary);

        if (!file) {
            std::cerr << "Failed to open file for writing: " << path << " - " << strerror(errno) << std::endl;
            return -1;
        }

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
    }

    return 0;
}

// Section 8: Helper Methods

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