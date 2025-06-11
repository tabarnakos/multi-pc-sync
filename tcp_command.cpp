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
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>

// System Includes
#include <sys/socket.h>

// Section 3: Defines and Macros
#define ALLOCATION_SIZE  (1024 * 1024)  // 1MiB
#define MAX_PAYLOAD_SIZE (64 * ALLOCATION_SIZE)  // 64MiB

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

size_t TcpCommand::receivePayload(const int socket, const size_t maxlen) {
    const size_t cmdSize = this->cmdSize();
    uint8_t *buf = new uint8_t[cmdSize];
    size_t bytesLeft = cmdSize - mData.size();

    while (bytesLeft > 0) {
        ssize_t n = recv(socket, buf, bytesLeft, 0);
        if (n <= 0) {
            delete[] buf;
            return 0;
        }
        mData.write(buf, n);
        bytesLeft -= n;
    }

    delete[] buf;
    return cmdSize;
}

int TcpCommand::transmit(const std::map<std::string, std::string>& args, bool calculateSize) {
    if (calculateSize) {
        size_t size = mData.size();
        mData.seek(kSizeIndex, SEEK_SET);
        mData.write(&size, kSizeSize);
    }

    int socket = std::stoi(args.at("rxsocket"));
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
    size_t size = cmdSize();
    cmd_id_t cmd = command();
    os << "Command size: " << size << "\n";
    os << "Command ID: " << static_cast<int>(cmd) << " (" << commandName() << ")\n";
}

size_t TcpCommand::cmdSize() {
    mData.seek(kSizeIndex, SEEK_SET);
    size_t size;
    mData.read(&size, kSizeSize);
    return size;
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
        return -1;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    uint8_t* buffer = new uint8_t[ALLOCATION_SIZE];
    size_t file_bytes = 0;
    HumanReadable total_size(file_size);
    const auto update_interval = std::max<size_t>(file_size / 100, ALLOCATION_SIZE); // Update every 1% or at least every buffer
    size_t next_update = update_interval;

    while (file_bytes < file_size) {
        size_t bytes_to_read = std::min<size_t>(ALLOCATION_SIZE, file_size - file_bytes);
        if (!file.read(reinterpret_cast<char*>(buffer), bytes_to_read)) {
            delete[] buffer;
            return -1;
        }
        mData.write(buffer, bytes_to_read);
        file_bytes += bytes_to_read;

        // Only update progress at reasonable intervals to avoid console spam
        if (file_bytes >= next_update || file_bytes == file_size) {
            std::cout << "Sent " << HumanReadable(file_bytes) << " of " << total_size 
                      << " (" << (file_bytes * 100 / file_size) << "%)\r" << std::flush;
            next_update = file_bytes + update_interval;
        }
    }
    std::cout << std::endl; // Final newline after progress updates

    delete[] buffer;
    return transmit(args);
}

int TcpCommand::ReceiveFile(const std::map<std::string, std::string>& args) {
    const std::string& path = args.at("path");
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return -1;
    }

    mData.seek(0, SEEK_SET);
    size_t total_size = mData.size();
    size_t remaining = total_size;
    uint8_t* buffer = new uint8_t[ALLOCATION_SIZE];
    HumanReadable total_size_hr(total_size);
    const auto update_interval = std::max<size_t>(total_size / 100, ALLOCATION_SIZE); // Update every 1% or at least every buffer
    size_t next_update = update_interval;
    size_t bytes_received = 0;
    
    while (remaining > 0) {
        size_t read_size = std::min(remaining, (size_t)ALLOCATION_SIZE);
        mData.read(buffer, read_size);
        if (!file.write(reinterpret_cast<char*>(buffer), read_size)) {
            delete[] buffer;
            return -1;
        }
        remaining -= read_size;
        bytes_received = total_size - remaining;

        // Only update progress at reasonable intervals to avoid console spam
        if (bytes_received >= next_update || remaining == 0) {
            std::cout << "Received " << HumanReadable(bytes_received) << " of " << total_size_hr 
                      << " (" << (bytes_received * 100 / total_size) << "%)\r" << std::flush;
            next_update = bytes_received + update_interval;
        }
    }
    std::cout << std::endl; // Final newline after progress updates
    
    delete[] buffer;
    return file.good() ? 0 : -1;
}

// Section 8: Helper Functions