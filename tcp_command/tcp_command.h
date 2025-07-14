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
#include <memory>
#include <string>
#include <semaphore>

// Project Includes
#include "growing_buffer.h"

/* Section 3: Defines and Macros */
#define INDEX_AFTER(prevIdx,prevIdxSiz)    ((prevIdx)+(prevIdxSiz))


//forward declarations
class DirectoryIndexer;

/* Section 4: Classes */
class TcpCommand {
public:
    /* Public Types */
    using cmd_id_t = enum : std::uint8_t {
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
        CMD_ID_REMOTE_SYMLINK,
        CMD_ID_REMOTE_MOVE,
    };

    /* Public Static Constants */
    static constexpr size_t kSizeIndex = 0;
    static constexpr size_t kSizeSize = sizeof(size_t);
    static constexpr size_t kCmdIndex = INDEX_AFTER(kSizeIndex, kSizeSize);
    static constexpr size_t kCmdSize = sizeof(cmd_id_t);
    static constexpr size_t kPayloadIndex = INDEX_AFTER(kCmdIndex, kCmdSize);

    static constexpr size_t ALLOCATION_SIZE = 128 * 1024;  // 128KiB
    static constexpr size_t MAX_TCP_PAYLOAD_SIZE = 1440;//1440 bytes
    static constexpr size_t MAX_PATH_LENGTH = 4095;  // 4095 characters, see Readme for details
    static constexpr size_t MAX_FILENAME_LENGTH = 255;  // 255 characters, see Readme for details
    static constexpr size_t MAX_PATH_WARNING_LENGTH = MAX_PATH_LENGTH - MAX_FILENAME_LENGTH;  // 255 characters margin
    static constexpr size_t MAX_FILENAME_WARNING_LENGTH = MAX_FILENAME_LENGTH - 50;  // 50 characters margin

    /* Static Configuration */
    static uint64_t configurable_max_file_size; // Actual max file size from configuration

    /* Static Configuration Methods */
    static void setMaxFileSize(uint64_t max_size) { configurable_max_file_size = max_size; }
    static uint64_t getMaxFileSize() { return configurable_max_file_size; }

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
     * Creates a TcpCommand instance using the provided arguments
     * @param cmd The command ID to create
     * @param args Map of arguments to create the command from
     * @return A new TcpCommand instance of the appropriate derived type, or nullptr if invalid
     */
    static TcpCommand* create(cmd_id_t cmd, std::map<std::string, std::string>& args);

    /**
     * Receives a command header from the socket
     * @param socket The socket file descriptor to receive from
     * @return A new TcpCommand instance created from the received header, or nullptr on error
     */
    static TcpCommand* receiveHeader(int socket);

    /**
     * Executes a command in a detached thread
     * @param command The command to execute
     * @param args Map of arguments for command execution
     */
    static void executeInDetachedThread(TcpCommand* command, std::map<std::string, std::string>& args);

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
    size_t receivePayload(int socket, size_t maxlen);

    /**
     * Transmits the command over a socket
     * @param args Map of arguments including "txsocket" for the target socket
     * @param calculateSize Whether to calculate and update the command size before transmission
     * @return 0 on success, negative value on error
     */
    int transmit(const std::map<std::string, std::string>& args, bool calculateSize = true);

    /**
     * Sends a chunk of data over the network
     * @param socket The socket file descriptor to send to
     * @param buffer Pointer to the data buffer to send
     * @param len Number of bytes to send
     * @return Number of bytes actually sent
     */
    static size_t sendChunk(int socket, const void* buffer, size_t len);

    /**
     * Sends a file over the network
     * @param args Map of arguments including "path" for the file path and "txsocket" for the target socket
     * @return 0 on success, negative value on error
     */
    static int SendFile(const std::map<std::string, std::string>& args);

    /**
     * Receives a chunk of data from a socket
     * @param socket The socket file descriptor to receive from
     * @param buffer Pointer to the buffer to store received data
     * @param len Number of bytes to receive
     * @return Number of bytes actually received
     */
    static ssize_t ReceiveChunk(int socket, void* buffer, size_t len);

    /**
     * Receives a file from the network
     * @param args Map of arguments including "path" for the destination path and "txsocket" for the source socket
     * @return 0 on success, negative value on error
     */
    static int ReceiveFile(const std::map<std::string, std::string>& args);

    /**
     * Dumps debug information about the command to an output stream
     * @param os The output stream to write to
     */
    void dump(std::ostream& outStream);

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
    virtual int execute(std::map<std::string, std::string>& args) = 0;

    /**
     * Gets the local directory indexer instance
     * @return A shared pointer to the local DirectoryIndexer
     */
    std::shared_ptr<DirectoryIndexer> getLocalIndexer();

protected:
    static std::binary_semaphore TCPSendSemaphore;
    static std::binary_semaphore TCPReceiveSemaphore;
    static std::chrono::steady_clock::time_point lastTransmitTime;
    static float transmitRateLimit;
    GrowingBuffer mData;

    /**
     * Reads a string from the buffer at the specified offset
     * @param off Offset in the buffer to start reading from
     * @param whence Starting position for the offset (SEEK_SET, SEEK_CUR, or SEEK_END)
     * @return The string read from the buffer
     */
    std::string extractStringFromPayload(size_t off, int whence = SEEK_SET);

    static std::vector<std::string> parseDeletionLogFromBuffer(GrowingBuffer& buffer, size_t& offset, int whence = SEEK_SET);
    /**
     * Appends a deletion log to the buffer
     * @param buffer The buffer to append the deletion log to
     * @param deletions Vector of paths to delete
     */
    static void appendDeletionLogToBuffer(GrowingBuffer& buffer, const std::vector<std::string>& deletions);
};

/* Derived Command Classes */
class IndexFolderCmd : public TcpCommand {
    friend class TcpCommand; // Allow TcpCommand to access private members
public:
    IndexFolderCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~IndexFolderCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
private:
    std::shared_ptr<DirectoryIndexer> localIndexer;
};
class IndexPayloadCmd : public TcpCommand {
public:
    IndexPayloadCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~IndexPayloadCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
};
class MkdirCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    MkdirCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~MkdirCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
};
class RmCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    RmCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~RmCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
};
class FileFetchCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    FileFetchCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~FileFetchCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
};
class FilePushCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    FilePushCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~FilePushCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
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
    int execute(std::map<std::string, std::string>& args) override;
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
    int execute(std::map<std::string, std::string>& args) override;

    /**
     * Static helper to send a message over a socket
     * @param socket The socket file descriptor to send to
     * @param message The message text to send
     */
    static void sendMessage(int socket, const std::string& message);
};

class RmdirCmd : public TcpCommand {
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    RmdirCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~RmdirCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
};

class SyncCompleteCmd : public TcpCommand {
public:
    SyncCompleteCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~SyncCompleteCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
};
class SyncDoneCmd : public TcpCommand {
public:
    SyncDoneCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    virtual ~SyncDoneCmd() override;
    int execute(std::map<std::string, std::string>& args) override;
};
// New derived command classes
class RemoteSymlinkCmd : public TcpCommand {
public:
    static constexpr size_t kSrcPathSizeIndex = kPayloadIndex;
    static constexpr size_t kSrcPathSizeSize = sizeof(size_t);
    static constexpr size_t kSrcPathIndex = INDEX_AFTER(kSrcPathSizeIndex, kSrcPathSizeSize);
    static constexpr size_t kDestPathSizeIndex = INDEX_AFTER(kSrcPathIndex, 0); // Adjust dynamically
    static constexpr size_t kDestPathSizeSize = sizeof(size_t);
    static constexpr size_t kDestPathIndex = INDEX_AFTER(kDestPathSizeIndex, kDestPathSizeSize);

    RemoteSymlinkCmd(GrowingBuffer& data) : TcpCommand(data) {}
    virtual ~RemoteSymlinkCmd() override {}
    int execute(std::map<std::string, std::string>& args) override;
};

class RemoteMoveCmd : public TcpCommand {
public:
    static constexpr size_t kSrcPathSizeIndex = kPayloadIndex;
    static constexpr size_t kSrcPathSizeSize = sizeof(size_t);
    static constexpr size_t kSrcPathIndex = INDEX_AFTER(kSrcPathSizeIndex, kSrcPathSizeSize);
    static constexpr size_t kDestPathSizeIndex = INDEX_AFTER(kSrcPathIndex, 0); // Adjust dynamically
    static constexpr size_t kDestPathSizeSize = sizeof(size_t);
    static constexpr size_t kDestPathIndex = INDEX_AFTER(kDestPathSizeIndex, kDestPathSizeSize);

    RemoteMoveCmd(GrowingBuffer& data) : TcpCommand(data) {}
    virtual ~RemoteMoveCmd() override {}
    int execute(std::map<std::string, std::string>& args) override;
};

#endif  //_TCP_COMMAND_H_