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
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>

// System Includes
#include <sys/socket.h>

// Project Includes
#include "directory_indexer.h"
#include "human_readable.h"
#include "sync_command.h"

// Section 3: Defines and Macros

// Section 4: Static Variables

// Section 5: Constructors/Destructors

// Section 6: Static Methods

void TcpCommand::executeInDetachedThread(TcpCommand* command, const std::map<std::string, std::string>& args) {
    std::thread([command, args]() {
        command->execute(args);
        delete command;
    }).detach();
}

void TcpCommand::block_transmit() {
    TCPSendSemaphore.acquire();
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
        default: return "UNKNOWN";
    }
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
std::string TcpCommand::extractStringFromPayload(size_t off, int whence) {
    mData.seek(off, whence);
    size_t pathSize;
    mData.read(&pathSize, sizeof(size_t));
    
    std::string path(pathSize, '\0');
    mData.read(&path[0], pathSize);
    return path;
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
        chunk_sent += n;
        //std::cout << "DEBUG: Sent chunk of " << n
        //          << " (chunk progress: " << HumanReadable(chunk_sent) << "/" << HumanReadable(len) << ")" << std::endl;
    }
    return chunk_sent;
}

ssize_t TcpCommand::ReceiveChunk(const int socket, void* buffer, size_t len)
{
    size_t chunk_received = 0;

    while (chunk_received < len) {
        ssize_t n = recv(socket, static_cast<uint8_t*>(buffer) + chunk_received, len - chunk_received, 0);
        if (n <= 0) {
            if (n == 0) {
                std::cerr << "Connection closed by peer after receiving "
                            << chunk_received << " bytes" << std::endl;
                return -1;
            } else {
                std::cerr << "Receive error at " << chunk_received 
                            << " bytes: " << strerror(errno) << std::endl;
            }
            return -1;
        }

        chunk_received += n;
        //std::cout << "DEBUG: Received chunk of " << HumanReadable(n) 
        //            << "(chunk progress: " << HumanReadable(chunk_received) 
        //            << "/" << HumanReadable(len) << std::endl;
    }

    return chunk_received;
}
