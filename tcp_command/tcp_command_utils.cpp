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
#include <iostream>
#include <semaphore>
#include <string>
#include <thread>

// Third-Party Includes
#include "termcolor/termcolor.hpp"

// System Includes
#include <sys/socket.h>

// Project Includes
#include "human_readable.h"

// Section 3: Defines and Macros
constexpr float MICROSECONDS_PER_SECOND = 1000000.0F;

// Section 4: Static Variables

// Section 5: Constructors/Destructors

// Section 6: Static Methods

void TcpCommand::executeInDetachedThread(TcpCommand* command, std::map<std::string, std::string>& args) {
    std::thread([command, args]() mutable {
        command->execute(args);
        delete command;
    }).detach();
}

void TcpCommand::block_transmit() {
    TCPSendSemaphore.acquire();
    if (transmitRateLimit > 0) {
        auto now = std::chrono::steady_clock::now();
        auto minInterval = std::chrono::microseconds(static_cast<int64_t>(MICROSECONDS_PER_SECOND / transmitRateLimit));
        auto elapsed = now - lastTransmitTime;
        if (elapsed < minInterval) {
            std::this_thread::sleep_for(minInterval - elapsed);
        }
        lastTransmitTime = std::chrono::steady_clock::now();
    }
}

void TcpCommand::unblock_transmit() {
    TCPSendSemaphore.release();
    std::this_thread::sleep_for(std::chrono::nanoseconds(1));
}

void TcpCommand::block_receive() {
    TCPReceiveSemaphore.acquire();
}

void TcpCommand::unblock_receive() {
    TCPReceiveSemaphore.release();
    std::this_thread::sleep_for(std::chrono::nanoseconds(1));
}

void TcpCommand::setRateLimit(float rateHz) {
    transmitRateLimit = rateHz;
}

// Section 7: Public/Protected/Private Methods

// Section 8: Helper Functions

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
        case CMD_ID_REMOTE_SYMLINK: return "REMOTE_SYMLINK";
        case CMD_ID_REMOTE_MOVE: return "REMOTE_MOVE";
        default: return "UNKNOWN";
    }
}

void TcpCommand::dump(std::ostream& outStream) {
    mData.dump(outStream);
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
std::string TcpCommand::extractStringFromPayload(size_t off, int whence) {
    mData.seek(off, whence);
    size_t pathSize;
    mData.read(&pathSize, sizeof(size_t));
    
    std::string path(pathSize, '\0');
    mData.read(path.data(), pathSize);
    return path;
}

size_t TcpCommand::sendChunk(const int socket, const void* buffer, size_t len)
{
    size_t chunk_sent = 0;
    const auto* buf = static_cast<const uint8_t*>(buffer);
    while (chunk_sent < len) {
        auto packet_bytes = std::min<size_t>(len - chunk_sent, MAX_TCP_PAYLOAD_SIZE);
        ssize_t num = send(socket, buf + chunk_sent, packet_bytes, 0);
        if (num <= 0) {
            if (num == 0) {
                std::cerr << termcolor::red << "Connection closed by peer after sending "
                          << termcolor::magenta << HumanReadable(chunk_sent) << termcolor::reset << "\r\n";
            } else {
                std::cerr << termcolor::red << "Send error at " << termcolor::magenta << HumanReadable(chunk_sent) << termcolor::reset
                          << ": " << strerror(errno) << "\r\n";
            }
            return -1;
        }
        chunk_sent += num;
        //std::cout << "DEBUG: Sent chunk of " << n
        //          << " (chunk progress: " << HumanReadable(chunk_sent) << "/" << HumanReadable(len) << ")" << "\r\n";
    }
    return chunk_sent;
}

ssize_t TcpCommand::ReceiveChunk(const int socket, void* buffer, size_t len)
{
    size_t chunk_received = 0;

    while (chunk_received < len) {
        ssize_t num = recv(socket, static_cast<uint8_t*>(buffer) + chunk_received, len - chunk_received, 0);
        if (num < 0) {
            if (num == 0) {
                std::cerr << termcolor::red << "Connection closed by peer after receiving " << termcolor::magenta
                            << chunk_received << " bytes" << termcolor::reset << "\r\n";
                return -1;
            }
            std::cerr << termcolor::red << "Receive error at " << termcolor::magenta << chunk_received
                            << " bytes: " << strerror(errno) << termcolor::reset << "\r\n";
            return -1;
        }

        chunk_received += num;
        //std::cout << "DEBUG: Received chunk of " << HumanReadable(n) 
        //            << "(chunk progress: " << HumanReadable(chunk_received) 
        //            << "/" << HumanReadable(len) << "\r\n";
    }

    return chunk_received;
}

void TcpCommand::appendDeletionLogToBuffer(GrowingBuffer& buffer, const std::vector<std::string>& deletions) {
    size_t num_deletions = deletions.size();
    buffer.write(&num_deletions, sizeof(size_t));
    for (const auto& path : deletions) {
        size_t len = path.size();
        buffer.write(&len, sizeof(size_t));
        buffer.write(path.data(), len);
    }
}

std::vector<std::string> TcpCommand::parseDeletionLogFromBuffer(GrowingBuffer& buffer, size_t& offset, int whence) {
    std::vector<std::string> deletions;
    size_t num_deletions = 0;
    buffer.seek(offset, whence);
    buffer.read(&num_deletions, sizeof(size_t));
    offset += sizeof(size_t);
    for (size_t i = 0; i < num_deletions; ++i) {
        size_t len = 0;
        buffer.read(&len, sizeof(size_t));
        offset += sizeof(size_t);
        std::string path(len, '\0');
        buffer.read(path.data(), len);
        offset += len;
        deletions.push_back(path);
    }
    return deletions;
}
