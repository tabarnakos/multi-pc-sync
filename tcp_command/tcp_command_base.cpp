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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// Third-Party Includes
#include "termcolor/termcolor.hpp"

// Project Includes
#include "program_options.h"

// System Includes
#include <sys/socket.h>

// Project Includes
#include "human_readable.h"

// Section 3: Defines and Macros
constexpr suseconds_t TCP_COMMAND_HEADER_TIMEOUT_USEC = 10000; // 10ms
constexpr int PERCENTAGE_FACTOR = 100;

// Section 4: Static Variables
std::binary_semaphore TcpCommand::TCPSendSemaphore{1};
std::binary_semaphore TcpCommand::TCPReceiveSemaphore{1};
std::chrono::steady_clock::time_point TcpCommand::lastTransmitTime = std::chrono::steady_clock::now();
float TcpCommand::transmitRateLimit = 0.0F;
uint64_t TcpCommand::configurable_max_file_size = DEFAULT_MAX_FILE_SIZE_BYTES; // Initialize with default value

// Section 5: Constructors/Destructors
TcpCommand::TcpCommand() = default;

TcpCommand::TcpCommand(GrowingBuffer &data) {
    const size_t size = data.size();
    auto *buf = new uint8_t[size];
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
        case CMD_ID_REMOTE_SYMLINK:
            return new RemoteSymlinkCmd(data);
        case CMD_ID_REMOTE_MOVE:
            return new RemoteMoveCmd(data);
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
        timeout.tv_usec = TCP_COMMAND_HEADER_TIMEOUT_USEC; // 10ms
        
        if (received == 0) {
            block_receive();
        }
        ret = select(socket + 1, &readfds, nullptr, nullptr, &timeout);
        if (ret > 0 && FD_ISSET(socket, &readfds)) {
            ssize_t num = recv(socket, sizePtr + received, bytesLeft-received, 0);
            if (num <= 0) {
                // client disconnected or error
                unblock_receive();
                return nullptr;
            }
            received += num;
            bytesLeft -= num;
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
    if (command == nullptr)
    {
        std::cout << termcolor::red << "Received unknown command ID: " << cmd << "\r\n" << termcolor::reset;
        return nullptr;
    }
    std::cout << termcolor::green << "Received command " << command->commandName() << " of size " << commandSize << "\r\n" << termcolor::reset;
    
    return command;
}

// Section 7: Public/Protected/Private Methods

size_t TcpCommand::receivePayload(const int socket, const size_t maxlen) {
    const size_t cmdSize = this->cmdSize();
    const size_t bufSize = maxlen > 0 ? std::min<size_t>(maxlen, ALLOCATION_SIZE) : ALLOCATION_SIZE;
    auto* buffer = new uint8_t[bufSize];
    size_t totalReceived = 0;
    const size_t targetSize = maxlen > 0 ? std::min<size_t>(maxlen, cmdSize - mData.size()) : cmdSize - mData.size();

    //std::cout << "DEBUG: receivePayload starting with"
    //          << "\n  Target size: " << targetSize << " bytes"
    //          << "\n  Command size: " << cmdSize << " bytes"
    //          << "\n  Current buffer size: " << mData.size() << " bytes" << "\r\n";

    if (mData.seek(mData.size(), SEEK_SET) < 0) {
        std::cerr << termcolor::red << "Error seeking to end of buffer" << "\r\n" << termcolor::reset;
        delete[] buffer;
        return 0;
    }

    while (totalReceived < targetSize) {
        size_t remainingBytes = targetSize - totalReceived;
        size_t bytesToReceive = std::min<size_t>(remainingBytes, bufSize);
        
        ssize_t num = recv(socket, buffer, bytesToReceive, 0);
        if (num <= 0) {
            if (num == 0) {
                std::cerr << termcolor::red << "Connection closed by peer after receiving " << totalReceived << " bytes" << "\r\n" << termcolor::reset;
            } else {
                std::cerr << termcolor::red << "Receive error after " << totalReceived << " bytes: " << strerror(errno) << "\r\n" << termcolor::reset;
            }
            delete[] buffer;
            return totalReceived;
        }

        if (mData.write(buffer, num) != num) {
            std::cerr << termcolor::red << "Error writing " << num << " bytes to buffer" << "\r\n" << termcolor::reset;
            delete[] buffer;
            return totalReceived;
        }

        totalReceived += num;
        //std::cout << "DEBUG: receivePayload received " << n << " bytes (total: " << totalReceived 
        //          << "/" << targetSize << ")" << "\r\n";
    }

    delete[] buffer;
    return totalReceived;
}

int TcpCommand::transmit(const std::map<std::string, std::string>& args, bool calculateSize) {
    std::cout << termcolor::cyan << "DEBUG: Transmitting command " << commandName() << " with size " << mData.size() << "\r\n" << termcolor::reset;
    if (calculateSize) {
        size_t size = mData.size();
        mData.seek(kSizeIndex, SEEK_SET);
        mData.write(&size, kSizeSize);
    }

    int socket = std::stoi(args.at("txsocket"));
    auto* buffer = new uint8_t[ALLOCATION_SIZE];
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

    std::cout << termcolor::cyan << "Transmitted " << mData.size() << " bytes" << "\r\n" << termcolor::reset;

    delete[] buffer;
    return 0;
}

int TcpCommand::SendFile(const std::map<std::string, std::string>& args) {
    const std::string& path = args.at("path");
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << termcolor::red << "Failed to open file for reading: " << path << " - " << strerror(errno) << "\r\n" << termcolor::reset;
        return -1;
    }
    //std::cout << "DEBUG: Sending file: " << path << "\r\n";
    int socket = std::stoi(args.at("txsocket"));
    //std::cout << "DEBUG: Sending file header..." << "\r\n";
    size_t path_size = path.size();
    
    size_t sent_bytes = sendChunk(socket, &path_size, sizeof(size_t));
    if (sent_bytes < sizeof(size_t)) {
        std::cerr << termcolor::red << "Failed to send path size" << "\r\n" << termcolor::reset;
        return -1;
    }
    //std::cout << "DEBUG: Path size sent: " << path_size << " bytes" << "\r\n";
    sent_bytes = sendChunk(socket, path.data(), path_size);
    if (sent_bytes < path_size) {
        std::cerr << termcolor::red << "Failed to send file path" << "\r\n" << termcolor::reset;
        return -1;
    }
    //std::cout << "DEBUG: File path sent: " << path << "\r\n";
    // Get the file size
    std::streamsize file_size = std::filesystem::file_size(path); //file.tellg();
    //std::cout << "DEBUG: File size is " << file_size << " bytes" << "\r\n";
    if (file_size < 0 || static_cast<uint64_t>(file_size) > getMaxFileSize()) {
        std::cerr << termcolor::red << "Invalid file size: " << file_size << " bytes (max allowed: " << getMaxFileSize() << ")" << "\r\n" << termcolor::reset;
        return -1;
    }
    // Send the file size
    auto file_size_net = static_cast<size_t>(file_size);
    //std::cout << "DEBUG: Sending file size: " << file_size_net << " bytes" << "\r\n";
    sent_bytes = sendChunk(socket, &file_size_net, sizeof(size_t));
    if (sent_bytes < sizeof(size_t)) {
        std::cerr << termcolor::red << "Failed to send file size" << "\r\n" << termcolor::reset;
        return -1;
    }
    
    // Now send the file contents in chunks
    auto* buffer = new uint8_t[ALLOCATION_SIZE];
    size_t total_bytes_sent = 0;

    while (total_bytes_sent < file_size) {
        size_t bytes_to_read = std::min<size_t>(ALLOCATION_SIZE, file_size - total_bytes_sent);
        
        // Read chunk from file
        if (!file.read(reinterpret_cast<char*>(buffer), bytes_to_read)) {
            std::cerr << termcolor::red << "Failed to read from file after " << HumanReadable(total_bytes_sent) << " bytes" << "\r\n" << termcolor::reset;
            delete[] buffer;
            return -1;
        }

        size_t chunk_sent = sendChunk(socket, buffer, bytes_to_read);
        if (chunk_sent < bytes_to_read) {
            std::cerr << termcolor::red << "Failed to send file chunk after " << HumanReadable(total_bytes_sent) << " bytes" << "\r\n" << termcolor::reset;
            delete[] buffer;
            return -1;
        }
        total_bytes_sent += chunk_sent;
        //std::cout << "DEBUG: Sent chunk of " << chunk_sent 
        //          << " bytes (total sent: " << HumanReadable(total_bytes_sent) 
        //          << "/" << HumanReadable(file_size) << ")" << "\r\n";
        // Force flush output to ensure logs appear in real-time
        std::cout << termcolor::cyan << "Progress: " << HumanReadable(total_bytes_sent) << " of " << HumanReadable(file_size) 
                  << " (" << (total_bytes_sent * PERCENTAGE_FACTOR / file_size) << "%)" << termcolor::reset << "\r\n";
    }

    //std::cout << "DEBUG: File send complete. Total bytes sent: " << HumanReadable(total_bytes_sent) 
    //          << " of " << HumanReadable(file_size) << " expected" << "\r\n";

    delete[] buffer;
    file.close();
    //std::cout << "DEBUG: File " << path << " sent successfully." << "\r\n";
    return 0;
}

int TcpCommand::ReceiveFile(const std::map<std::string, std::string>& args) {
    //std::cout << "DEBUG: Starting ReceiveFile..." << "\r\n";
    
    int socket = std::stoi(args.at("txsocket"));

    // Now receive the file data in chunks
    size_t path_size;
    ssize_t received_bytes = ReceiveChunk(socket, &path_size, kSizeSize);
    if (received_bytes < kSizeSize) {
        std::cerr << termcolor::red << "Failed to receive path size" << "\r\n" << termcolor::reset;
        return -1;
    }
    if (path_size > MAX_PATH_LENGTH) {
        std::cerr << termcolor::red << "Path size exceeds maximum allowed size: " << path_size << " > " << MAX_PATH_LENGTH << "\r\n" << termcolor::reset;
        return -1;
    }
    std::string received_path(path_size, '\0');
    received_bytes = ReceiveChunk(socket, received_path.data(), path_size);
    if (received_bytes < path_size) {
        std::cerr << termcolor::red << "Failed to receive file path" << "\r\n" << termcolor::reset;
        return -1;
    }
    //std::cout << "DEBUG: Received file path: " << received_path << "\r\n";
    
    size_t file_size;
    received_bytes = ReceiveChunk(socket, &file_size, kSizeSize);
    if (received_bytes < kSizeSize) {
        std::cerr << termcolor::red << "Failed to receive file size" << "\r\n" << termcolor::reset;
        return -1;
    }
    if (file_size > getMaxFileSize()) {
        std::cerr << termcolor::red << "File size exceeds maximum allowed size: " << file_size << " > " << getMaxFileSize() << "\r\n" << termcolor::reset;
        return -1;
    }
    //std::cout << "DEBUG: Expected file size: " << HumanReadable(file_size) << "\r\n";

    if (file_size != 0)
    {
        const std::string& path = args.at("path");
        std::ofstream file(path, std::ios::binary);

        if (!file) {
            std::cerr << termcolor::red << "Failed to open file for writing: " << path << " - " << strerror(errno) << "\r\n" << termcolor::reset;
            return -1;
        }

        auto* buffer = new uint8_t[ALLOCATION_SIZE];
        received_bytes = 0;

        while (received_bytes < file_size) {
            size_t bytes_to_read = std::min<size_t>(ALLOCATION_SIZE, file_size - received_bytes);

            ssize_t chunk_received = ReceiveChunk(socket, buffer, bytes_to_read);
            if (chunk_received < 0) {
                std::cerr << termcolor::red << "Error receiving file chunk after " << HumanReadable(received_bytes) << "\r\n" << termcolor::reset;
                delete[] buffer;
                file.flush();
                return -1;
            }
            if (chunk_received == 0) {
                std::cerr << termcolor::yellow << "No more data received, connection may have been closed prematurely" << "\r\n" << termcolor::reset;
                delete[] buffer;
                file.flush();
                return -1;
            }
            
            // Write the received chunk to file
            if (!file.write(reinterpret_cast<char*>(buffer), chunk_received)) {
                std::cerr << termcolor::red << "Failed to write to file at " << HumanReadable(received_bytes) << " bytes" << "\r\n" << termcolor::reset;
                delete[] buffer;
                file.flush();
                return -1;
            }
            received_bytes += chunk_received;
            
            // Force flush output to ensure logs appear in real-time
            std::cout << termcolor::cyan << "Progress: " << HumanReadable(received_bytes) << " of " << HumanReadable(file_size)
                      << " (" << (received_bytes * 100. / file_size) << "%)" << termcolor::reset << "\r\n";
        }
        //std::cout << "DEBUG: File receive complete. Wrote: " << HumanReadable(received_bytes)
        //        << " of " << HumanReadable(file_size) << " to disk" << "\r\n";  
        file.close();   // Will automatically flush the file buffer
        delete[] buffer;
    }
    else
    {
        // If file size is 0, just create an empty file
        const std::string touch_command = std::string("touch ") + args.at("path");
        system(touch_command.c_str());
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
                std::cerr << termcolor::red << "Error: Missing required 'path1' argument for path-based command" << "\r\n" << termcolor::reset;
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
                std::cerr << termcolor::red << "Error: Missing required 'path1' or 'path2' argument for REMOTE_LOCAL_COPY command" << "\r\n" << termcolor::reset;
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
                std::cerr << termcolor::red << "Error: Missing required 'path1' argument for MESSAGE command" << "\r\n" << termcolor::reset;
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
            std::cerr << termcolor::red << "Error: Unknown command type: " << cmd << "\r\n" << termcolor::reset;
            return nullptr;
    }

    if (command == nullptr) {
        std::cerr << termcolor::red << "Error: Failed to create command object" << "\r\n" << termcolor::reset;
        return nullptr;
    }

    // Update the command size in the buffer after payload is written
    buffer.seek(kSizeIndex, SEEK_SET);
    buffer.write(&commandSize, kSizeSize);

    return command;
}

std::shared_ptr<DirectoryIndexer> TcpCommand::getLocalIndexer() {
    auto *command = dynamic_cast<IndexFolderCmd*>(this);
    return (command != nullptr) ? command->localIndexer : nullptr;
}