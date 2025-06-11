/******************************************************************************
 * TCP Command Header
 ******************************************************************************/

/* Section 1: Compilation Guards */
#ifndef _TCP_COMMAND_H_
#define _TCP_COMMAND_H_

/* Section 2: Includes */
// C Standard Library
#include <cstdio>

// C++ Standard Library
#include <chrono>
#include <map>
#include <mutex>
#include <string>

// Project Includes
#include "growing_buffer.h"

/* Section 3: Defines and Macros */
#define INDEX_AFTER(prevIdx,prevIdxSiz)    ((prevIdx)+(prevIdxSiz))

/* Section 4: Classes */
class TcpCommand {
public:
    /* Public Types */
    typedef enum {
        CMD_ID_INDEX_FOLDER = 0,
        CMD_ID_INDEX_PAYLOAD,
        CMD_ID_MKDIR_REQUEST,
        CMD_ID_RM_REQUEST,
        CMD_ID_FETCH_FILE_REQUEST,
        CMD_ID_PUSH_FILE,
        CMD_ID_REMOTE_LOCAL_COPY,
        CMD_ID_MESSAGE,
        CMD_ID_RMDIR_REQUEST,
        CMD_ID_SYNC_COMPLETE,
        CMD_ID_SYNC_DONE,
    } cmd_id_t;

    /* Public Static Constants */
    static constexpr size_t kSizeIndex = 0;
    static constexpr size_t kSizeSize = sizeof(size_t);
    static constexpr size_t kCmdIndex = INDEX_AFTER(kSizeIndex, kSizeSize);
    static constexpr size_t kCmdSize = sizeof(cmd_id_t);
    static constexpr size_t kPayloadIndex = INDEX_AFTER(kCmdIndex, kCmdSize);

    /* Constructors/Destructors */
    /**
     * Default constructor
     * Initializes an empty TCP command
     */
    TcpCommand();

    /**
     * Constructs a TCP command from existing buffer data
     * @param data The buffer containing command data to initialize from
     */
    TcpCommand(GrowingBuffer &data);

    /**
     * Virtual destructor to ensure proper cleanup of derived classes
     */
    virtual ~TcpCommand() = default;

    /* Public Static Methods */
    /**
     * Creates a TcpCommand instance from the provided buffer data
     * @param data The buffer containing command data to create from
     * @return A new TcpCommand instance of the appropriate derived type, or nullptr if invalid
     */
    static TcpCommand* create(GrowingBuffer& data);

    /**
     * Receives a command header from the socket
     * @param socket The socket file descriptor to receive from
     * @return A new TcpCommand instance created from the received header, or nullptr on error
     */
    static TcpCommand* receiveHeader(const int socket);

    /**
     * Executes a command in a detached thread
     * @param command The command to execute
     * @param args Map of arguments for command execution
     */
    static void executeInDetachedThread(TcpCommand* command, const std::map<std::string, std::string>& args);

    /**
     * Sets the rate limit for transmissions
     * @param rateHz The rate limit in Hz (transmissions per second)
     */
    static void setRateLimit(float rateHz);

    /**
     * Blocks transmission by acquiring the send mutex
     */
    static void block_transmit();

    /**
     * Unblocks transmission by releasing the send mutex
     */
    static void unblock_transmit();

    /**
     * Blocks receiving by acquiring the receive mutex
     */
    static void block_receive();

    /**
     * Unblocks receiving by releasing the receive mutex
     */
    static void unblock_receive();

    /* Public Methods */
    /**
     * Gets the string representation of the command type
     * @return A string containing the command name
     */
    const char* commandName();

    /**
     * Receives payload data from a socket
     * @param socket The socket file descriptor to receive from
     * @param maxlen The maximum length of data to receive (0 for unlimited)
     * @return Number of bytes received
     */
    size_t receivePayload(const int socket, const size_t maxlen);

    /**
     * Transmits the command over a socket
     * @param args Map of arguments including "txsocket" for the target socket
     * @param calculateSize Whether to calculate and update the command size before transmission
     * @return 0 on success, negative value on error
     */
    int transmit(const std::map<std::string, std::string>& args, bool calculateSize = true);

    /**
     * Sends a file over the network
     * @param args Map of arguments including "path" for the file path and "txsocket" for the target socket
     * @return 0 on success, negative value on error
     */
    int SendFile(const std::map<std::string, std::string>& args);

    /**
     * Receives a file from the network
     * @param args Map of arguments including "path" for the destination path and "txsocket" for the source socket
     * @return 0 on success, negative value on error
     */
    int ReceiveFile(const std::map<std::string, std::string>& args);

    /**
     * Dumps debug information about the command to an output stream
     * @param os The output stream to write to
     */
    void dump(std::ostream& os);

    /**
     * Gets the total size of the command data
     * @return Size of the command in bytes
     */
    size_t cmdSize();

    /**
     * Sets the total size of the command data
     * @param size New size in bytes
     */
    void setCmdSize(size_t size);

    /**
     * Gets the current size of the internal buffer
     * @return Size of the buffer in bytes
     */
    size_t bufferSize();

    /**
     * Gets the command type identifier
     * @return Command type enumeration value
     */
    cmd_id_t command();

    /**
     * Executes the command with the given arguments
     * @param args Map of arguments for command execution
     * @return 0 on success, negative value on error, 1 for completion signals
     */
    virtual int execute(const std::map<std::string, std::string>& args) = 0;

protected:
    /* Protected Static Members */
    static std::mutex TCPSendMutex;
    static std::mutex TCPReceiveMutex;
    static std::chrono::steady_clock::time_point lastTransmitTime;
    static float transmitRateLimit;

    /* Protected Members */
    GrowingBuffer mData;

    /* Protected Methods */
    /**
     * Reads a path string from the buffer at the specified offset
     * @param off Offset in the buffer to start reading from
     * @param whence Starting position for the offset (SEEK_SET, SEEK_CUR, or SEEK_END)
     * @return The path string read from the buffer
     */
    std::string readPathFromBuffer(size_t off, int whence = SEEK_SET);

    /**
     * Extracts a string from the current payload position
     * First reads the string size, then the string data
     * @return The extracted string
     */
    std::string extractStringFromPayload();
};

/* Derived Command Classes */
class IndexFolderCmd : public TcpCommand {
public:
    IndexFolderCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~IndexFolderCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};
class IndexPayloadCmd : public TcpCommand {
public:
    IndexPayloadCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~IndexPayloadCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};
class MkdirCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    MkdirCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~MkdirCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};
class RmCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    RmCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~RmCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};
class FileFetchCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    FileFetchCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~FileFetchCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};
class FilePushCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    FilePushCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~FilePushCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};
class RemoteLocalCopyCmd : public TcpCommand {
public:
    static constexpr size_t kSrcPathSizeIndex = kPayloadIndex;
    static constexpr size_t kSrcPathSizeSize = sizeof(size_t);
    static constexpr size_t kSrcPathIndex = INDEX_AFTER(kSrcPathSizeIndex, kSrcPathSizeSize);
    static constexpr size_t kDestPathSizeIndex = INDEX_AFTER(kSrcPathIndex, 0); // Adjust dynamically
    static constexpr size_t kDestPathSizeSize = sizeof(size_t);
    static constexpr size_t kDestPathIndex = INDEX_AFTER(kDestPathSizeIndex, kDestPathSizeSize);

    RemoteLocalCopyCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~RemoteLocalCopyCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};

class MessageCmd : public TcpCommand {
public:
    static constexpr size_t kErrorMessageSizeIndex = kPayloadIndex;
    static constexpr size_t kErrorMessageSizeSize = sizeof(size_t);
    static constexpr size_t kErrorMessageIndex = INDEX_AFTER(kErrorMessageSizeIndex, kErrorMessageSizeSize);

    /**
     * Constructs a message command from existing buffer data
     * @param data The buffer containing the message data
     */
    MessageCmd(GrowingBuffer& data) :  TcpCommand(data) {}

    /**
     * Constructs a new message command with the specified message
     * @param message The message text to send
     */
    MessageCmd(const std::string& message);

    /**
     * Virtual destructor for cleanup
     */
    virtual ~MessageCmd() override;

    /**
     * Executes the message command, displaying the message
     * @param args Map containing "ip" for the sender's IP address and "txsocket" for the socket
     * @return 0 on success, negative value on error
     */
    virtual int execute(const std::map<std::string, std::string>& args) override;

    /**
     * Static helper to send a message over a socket
     * @param socket The socket file descriptor to send to
     * @param message The message text to send
     */
    static void sendMessage(const int socket, const std::string& message);
};

class RmdirCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    RmdirCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~RmdirCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};

class SyncCompleteCmd : public TcpCommand {
public:
    SyncCompleteCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~SyncCompleteCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};
class SyncDoneCmd : public TcpCommand {
public:
    SyncDoneCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~SyncDoneCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;
};

#endif  //_TCP_COMMAND_H_