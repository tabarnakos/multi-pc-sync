#ifndef _TCP_COMMAND_H_
#define _TCP_COMMAND_H_

#include "growing_buffer.h"
#include <map>
#include <string>
#include <thread>

#define INDEX_AFTER(prevIdx,prevIdxSiz)    ((prevIdx)+(prevIdxSiz))

class TcpCommand
{
public:
    TcpCommand() : mData() {} // Default constructor initializes mData
    TcpCommand(GrowingBuffer &data);

    typedef enum
    {
        CMD_ID_INDEX_FOLDER = 0,
        CMD_ID_INDEX_PAYLOAD,
        CMD_ID_MKDIR_REQUEST,
        CMD_ID_RM_REQUEST,
        CMD_ID_FETCH_FILE_REQUEST,
        CMD_ID_PUSH_FILE,
        CMD_ID_REMOTE_LOCAL_COPY,
        CMD_ID_MESSAGE,
        CMD_ID_RMDIR_REQUEST, // Add new command ID
    } cmd_id_t;

    constexpr const char * commandName()
    {
        if (command() == CMD_ID_INDEX_FOLDER)
            return "IndexFolderCmd";
        else if (command() == CMD_ID_INDEX_PAYLOAD)
            return "IndexPayloadCmd";
        else if (command() == CMD_ID_MKDIR_REQUEST)
            return "MkdirCmd";
        else if (command() == CMD_ID_RM_REQUEST)
            return "RmCmd";
        else if (command() == CMD_ID_FETCH_FILE_REQUEST)
            return "FileFetchCmd";
        else if (command() == CMD_ID_PUSH_FILE)
            return "FilePushCmd";
        else if (command() == CMD_ID_REMOTE_LOCAL_COPY)
            return "RemoteLocalCopyCmd";
        else if (command() == CMD_ID_MESSAGE)
            return "Message";
        else if (command() == CMD_ID_RMDIR_REQUEST)
            return "RmdirCmd";
        else
            return "UnknownCommand";
    }

    static TcpCommand * create( GrowingBuffer & data );
    static TcpCommand* receiveHeader( const int socket );
    size_t receivePayload( const int socket, const size_t maxlen );
    int transmit(const std::map<std::string, std::string> &args, bool calculateSize = true);
    void SendFile(const std::map<std::string,std::string> &args);
    void dump(std::ostream& os);

    size_t cmdSize();
    void setCmdSize(size_t size);
    size_t bufferSize();
    cmd_id_t command();
    virtual int execute(const std::map<std::string,std::string> &args) = 0;
    static void executeInDetachedThread(TcpCommand *command, const std::map<std::string, std::string> &args) {
        std::thread([command, args]() {
            command->execute(args);
            delete command;
        }).detach();
    }

    static constexpr size_t kSizeIndex = 0;
    static constexpr size_t kSizeSize = sizeof(size_t);
    static constexpr size_t kCmdIndex = INDEX_AFTER(kSizeIndex, kSizeSize);
    static constexpr size_t kCmdSize = sizeof(cmd_id_t);
    static constexpr size_t kPayloadIndex = INDEX_AFTER(kCmdIndex, kCmdSize);

    virtual ~TcpCommand() = default;

    static void block_transmit() { TCPSendMutex.lock();}
    static void unblock_transmit() { TCPSendMutex.unlock();}
    static void block_receive() { TCPReceiveMutex.lock();}
    static void unblock_receive() { TCPReceiveMutex.unlock();}

protected:
    static std::mutex TCPSendMutex;
    static std::mutex TCPReceiveMutex;
    GrowingBuffer mData;

    // Add new helper function
    std::string readPathFromBuffer(size_t pathSizeIndex) {
        mData.seek(pathSizeIndex, SEEK_SET);
        size_t size;
        mData.read(&size, kSizeSize);
        std::vector<char> path(size + 1);
        mData.read(path.data(), size);
        path[size] = '\0';
        return std::string(path.data());
    }

    int ReceiveFile(const std::map<std::string,std::string> &args);
    std::string extractStringFromPayload();
};

class IndexFolderCmd : public TcpCommand
{
public:
    IndexFolderCmd( GrowingBuffer & data ) : TcpCommand(data) {}
    virtual int execute(const std::map<std::string,std::string> &args);
};
class IndexPayloadCmd : public TcpCommand
{
public:
    IndexPayloadCmd( GrowingBuffer & data ) : TcpCommand(data) {}
    virtual int execute(const std::map<std::string,std::string> &args);
};
class MkdirCmd : public TcpCommand
{
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    MkdirCmd( GrowingBuffer & data ) : TcpCommand(data) {}
    virtual int execute(const std::map<std::string,std::string> &args);
};
class RmCmd : public TcpCommand
{
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    RmCmd( GrowingBuffer & data ) : TcpCommand(data) {}
    virtual int execute(const std::map<std::string,std::string> &args);
};
class FileFetchCmd : public TcpCommand
{
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    FileFetchCmd( GrowingBuffer & data ) : TcpCommand(data) {}
    virtual int execute(const std::map<std::string,std::string> &args);
};
class FilePushCmd : public TcpCommand
{
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    FilePushCmd( GrowingBuffer & data ) : TcpCommand(data) {}
    virtual int execute(const std::map<std::string,std::string> &args);
};
class RemoteLocalCopyCmd : public TcpCommand
{
public:
    static constexpr size_t kSrcPathSizeIndex = kPayloadIndex;
    static constexpr size_t kSrcPathSizeSize = sizeof(size_t);
    static constexpr size_t kSrcPathIndex = INDEX_AFTER(kSrcPathSizeIndex, kSrcPathSizeSize);
    static constexpr size_t kDestPathSizeIndex = INDEX_AFTER(kSrcPathIndex, 0); // Adjust dynamically
    static constexpr size_t kDestPathSizeSize = sizeof(size_t);
    static constexpr size_t kDestPathIndex = INDEX_AFTER(kDestPathSizeIndex, kDestPathSizeSize);

    RemoteLocalCopyCmd(GrowingBuffer &data) : TcpCommand(data) {}
    virtual int execute(const std::map<std::string, std::string> &args);
};

class MessageCmd : public TcpCommand
{
public:
    static constexpr size_t kErrorMessageSizeIndex = kPayloadIndex;
    static constexpr size_t kErrorMessageSizeSize = sizeof(size_t);
    static constexpr size_t kErrorMessageIndex = INDEX_AFTER(kErrorMessageSizeIndex, kErrorMessageSizeSize);

    MessageCmd(GrowingBuffer &data) : TcpCommand(data) {}
    MessageCmd(const std::string &message);
    virtual int execute(const std::map<std::string, std::string> &args);

    static void sendMessage(const int socket, const std::string &message);
};

class RmdirCmd : public TcpCommand
{
public:
    static constexpr size_t kPathSizeIndex = kPayloadIndex;
    static constexpr size_t kPathSizeSize = sizeof(size_t);
    static constexpr size_t kPathIndex = INDEX_AFTER(kPathSizeIndex, kPathSizeSize);

    RmdirCmd(GrowingBuffer & data ) : TcpCommand(data) {}
    virtual int execute(const std::map<std::string,std::string> &args);
};

#endif  //_TCP_COMMAND_H_