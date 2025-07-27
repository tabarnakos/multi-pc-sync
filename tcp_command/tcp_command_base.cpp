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
#include <fcntl.h> /* Definition of AT_* constants */
#include <sys/stat.h>

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
#include "directory_indexer.h"

// Section 3: Defines and Macros
constexpr suseconds_t TCP_COMMAND_HEADER_TIMEOUT_USEC = 10000; // 10ms
constexpr int PERCENTAGE_FACTOR = 100;
constexpr double NANOSECONDS_PER_SECOND = 1000000000.0; // 1 billion nanoseconds in a second
constexpr unsigned long FILE_TRANSFER_UPDATE_INTERVAL_MS = 200L; // 200ms = 5 Hz

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
        case CMD_ID_SYSTEM_CALL:
            return new SystemCallCmd(data);
        case CMD_ID_TOUCH:
            return new TouchCmd(data);
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
    
    std::array<uint8_t, MD5_DIGEST_LENGTH> receivedhash;
    if (recv(socket, receivedhash.data(), MD5_DIGEST_LENGTH, 0) <= 0)
    {
        MessageCmd::sendMessage(socket, "Failed to receive command hash");
        return nullptr;
    }
    buffer.write(receivedhash);

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
        if (num < 0) {
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

    // Send the file's modified time
    std::filesystem::file_time_type modTime = std::filesystem::last_write_time(path);
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

    //std::cout << "DEBUG: File path sent: " << path << "\r\n";
    // Get the file size
    std::streamsize file_size = std::filesystem::file_size(path); //file.tellg();
    //std::cout << "DEBUG: File size is " << file_size << " bytes" << "\r\n";
    if (file_size < 0 || static_cast<uint64_t>(file_size) > getMaxFileSize()) {
        std::cerr << termcolor::red << "Invalid file size: " << HumanReadable(file_size) << " (max allowed: " << HumanReadable(getMaxFileSize()) << ")" << "\r\n" << termcolor::reset;
        file_size = 0; // Set to 0 to send no content
    }
    // Send the file size
    auto file_size_net = static_cast<size_t>(file_size);
    //std::cout << "DEBUG: Sending file size: " << file_size_net << " bytes" << "\r\n";
    sent_bytes = sendChunk(socket, &file_size_net, sizeof(size_t));
    if (sent_bytes < sizeof(size_t)) {
        std::cerr << termcolor::red << "Failed to send file size" << "\r\n" << termcolor::reset;
        return -1;
    }
    
    // Now send the file contents in chunks with optimal buffering
    auto* buffer = new uint8_t[ALLOCATION_SIZE + MAX_TCP_PAYLOAD_SIZE]; // Extra space for buffering
    size_t total_bytes_sent = 0;
    size_t buffer_offset = 0; // Tracks unsent data from previous read
    size_t total_bytes_read = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;

    while (total_bytes_read < file_size) {
        // Calculate how much to read - always try to read ALLOCATION_SIZE worth of new data
        size_t bytes_to_read = std::min<size_t>(ALLOCATION_SIZE, file_size - total_bytes_read);
        
        // Read chunk from file into buffer after any existing buffered data
        if (!file.read(reinterpret_cast<char*>(buffer + buffer_offset), bytes_to_read)) {
            std::cerr << termcolor::red << "Failed to read from file after " << HumanReadable(total_bytes_read) << " bytes" << "\r\n" << termcolor::reset;
            delete[] buffer;
            return -1;
        }
        total_bytes_read += bytes_to_read;
        
        // Total data available to send (buffered + newly read)
        size_t total_available = buffer_offset + bytes_to_read;
        size_t bytes_sent_from_buffer = 0;
        
        // Send complete MAX_TCP_PAYLOAD_SIZE packets
        while (bytes_sent_from_buffer + MAX_TCP_PAYLOAD_SIZE <= total_available) {
            size_t chunk_sent = sendChunk(socket, buffer + bytes_sent_from_buffer, MAX_TCP_PAYLOAD_SIZE);
            if (chunk_sent < MAX_TCP_PAYLOAD_SIZE) {
                std::cerr << termcolor::red << "Failed to send file chunk after " << HumanReadable(total_bytes_sent + bytes_sent_from_buffer) << " bytes" << "\r\n" << termcolor::reset;
                delete[] buffer;
                return -1;
            }
            bytes_sent_from_buffer += chunk_sent;
        }
        
        // Handle remaining data
        size_t remaining_data = total_available - bytes_sent_from_buffer;
        
        if (total_bytes_read >= file_size) {
            // This is the last read - send any remaining data
            if (remaining_data > 0) {
                size_t chunk_sent = sendChunk(socket, buffer + bytes_sent_from_buffer, remaining_data);
                if (chunk_sent < remaining_data) {
                    std::cerr << termcolor::red << "Failed to send final file chunk after " << HumanReadable(total_bytes_sent + bytes_sent_from_buffer) << " bytes" << "\r\n" << termcolor::reset;
                    delete[] buffer;
                    return -1;
                }
                bytes_sent_from_buffer += chunk_sent;
            }
            buffer_offset = 0;
        } else {
            // Move remaining incomplete packet data to beginning of buffer for next iteration
            if (remaining_data > 0) {
                std::memmove(buffer, buffer + bytes_sent_from_buffer, remaining_data);
            }
            buffer_offset = remaining_data;
        }
        
        total_bytes_sent += bytes_sent_from_buffer;
        //std::cout << "DEBUG: Sent " << bytes_sent_from_buffer 
        //          << " bytes, buffered " << buffer_offset << " for next iteration"
        //          << " (total sent: " << HumanReadable(total_bytes_sent) 
        //          << "/" << HumanReadable(file_size) << ")" << "\r\n";
        // Force flush output to ensure logs appear in real-time
        if ( std::chrono::steady_clock::now() - last_report_time > std::chrono::milliseconds(FILE_TRANSFER_UPDATE_INTERVAL_MS) ) {
            last_report_time = std::chrono::steady_clock::now();
            std::cout << termcolor::cyan << "Progress: " << HumanReadable(total_bytes_sent) << " of " << HumanReadable(file_size) 
                  << " (" << (total_bytes_sent * PERCENTAGE_FACTOR / file_size) << "%)" << termcolor::reset << "\r\n";
        }
    }

    //auto duration = std::chrono::steady_clock::now() - start_time + std::chrono::nanoseconds(1); // Ensure non-zero duration
    //std::cout << termcolor::yellow << "ALLOCATION_SIZE = " << HumanReadable(ALLOCATION_SIZE) << termcolor::reset << "\r\n";
    //std::cout << termcolor::yellow << "Average send rate: " << termcolor::bright_magenta
    //          << HumanReadable(total_bytes_sent / (std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() / NANOSECONDS_PER_SECOND)) 
    //          << "ps" << termcolor::reset << "\r\n";

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

    // Receive the file's modified time
    size_t modTimeSize;
    received_bytes = ReceiveChunk(socket, &modTimeSize, kSizeSize);
    if (received_bytes < kSizeSize) {
        std::cerr << termcolor::red << "Failed to receive modified time size" << "\r\n" << termcolor::reset;
        return -1;
    }
    std::string modTimeStr(modTimeSize, '\0');
    received_bytes = ReceiveChunk(socket, modTimeStr.data(), modTimeSize);
    if (received_bytes < modTimeSize) {
        std::cerr << termcolor::red << "Failed to receive modified time" << "\r\n" << termcolor::reset;
        return -1;
    }
    
    size_t file_size;
    received_bytes = ReceiveChunk(socket, &file_size, kSizeSize);
    if (received_bytes < kSizeSize) {
        std::cerr << termcolor::red << "Failed to receive file size" << "\r\n" << termcolor::reset;
        return -1;
    }
    if (file_size > getMaxFileSize()) {
        std::cerr << termcolor::red << "File size exceeds maximum allowed size: " << HumanReadable(file_size) << " > " << HumanReadable(getMaxFileSize()) << "\r\n" << termcolor::reset;
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

        auto start_time = std::chrono::steady_clock::now();
        auto last_report_time = start_time;
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
            
            if ( std::chrono::steady_clock::now() - last_report_time > std::chrono::milliseconds(FILE_TRANSFER_UPDATE_INTERVAL_MS) ) {
                last_report_time = std::chrono::steady_clock::now();
                std::cout << termcolor::cyan << "Progress: " << HumanReadable(received_bytes) << " of " << HumanReadable(file_size)
                      << " (" << (received_bytes * 100. / file_size) << "%)" << termcolor::reset << "\r\n";
            }
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

    // Set the file's modified time
    std::array<struct timespec, 2> timeSpecsArray{ timespec{.tv_sec = 0, .tv_nsec = UTIME_OMIT}, 
                                                   timespec{.tv_sec = 0, .tv_nsec = 0} };
    DirectoryIndexer::make_timespec(modTimeStr, &timeSpecsArray[1]);
    utimensat(0, args.at("path").c_str(), timeSpecsArray.data(), 0);

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

std::array<uint8_t, MD5_DIGEST_LENGTH> TcpCommand::getCommandHash()
{
    mData.seek(kCmdHashIndex, SEEK_SET);
    std::array<uint8_t, MD5_DIGEST_LENGTH> hash;
    if (mData.read(hash.data(), kCmdHashSize) != kCmdHashSize)
    {
        std::cerr << termcolor::red << "Error reading command hash from buffer" << "\r\n" << termcolor::reset;
        return {};
    }
    return hash;
}