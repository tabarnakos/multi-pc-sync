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
    TcpCommand();
    TcpCommand(GrowingBuffer &data);
    virtual ~TcpCommand() = default;

    /* Public Static Methods */
    static TcpCommand* create(GrowingBuffer& data);
    static TcpCommand* receiveHeader(const int socket);
    static void executeInDetachedThread(TcpCommand* command, const std::map<std::string, std::string>& args);
    static void setRateLimit(float rateHz);
    static void block_transmit();
    static void unblock_transmit();
    static void block_receive();
    static void unblock_receive();

    /* Public Methods */
    const char* commandName();
    size_t receivePayload(const int socket, const size_t maxlen);
    int transmit(const std::map<std::string, std::string>& args, bool calculateSize = true);
    int SendFile(const std::map<std::string, std::string>& args);
    int ReceiveFile(const std::map<std::string, std::string>& args);
    void dump(std::ostream& os);
    size_t cmdSize();
    void setCmdSize(size_t size);
    size_t bufferSize();
    cmd_id_t command();
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
    std::string readPathFromBuffer(size_t off, int whence = SEEK_SET);
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

    MessageCmd(GrowingBuffer& data) :  TcpCommand(data) {}
    MessageCmd(const std::string& message);
    virtual ~MessageCmd() override;
    virtual int execute(const std::map<std::string, std::string>& args) override;

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