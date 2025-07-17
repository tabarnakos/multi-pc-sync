// Section 1: Compilation Guards
#ifndef _DIRECTORY_INDEXER_H_
#define _DIRECTORY_INDEXER_H_

// Section 2: Includes
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "folder.pb.h"
#include "sync_command.h"

// Section 3: Defines and Macros
// (none)

// Section 4: Classes
class SyncCommand;

/**
 * Class for indexing directory contents and managing synchronization
 * between local and remote file systems
 */
class DirectoryIndexer {
public:
    /**
     * Type of index being managed
     */
    enum INDEX_TYPE : std::uint8_t {
        INDEX_TYPE_LOCAL = 0,          ///< Current local directory state
        INDEX_TYPE_LOCAL_LAST_RUN,     ///< Previous local directory state
        INDEX_TYPE_REMOTE,             ///< Current remote directory state
        INDEX_TYPE_REMOTE_LAST_RUN,    ///< Previous remote directory state
    };

    /**
     * Type of filesystem entry
     */
    enum PATH_TYPE : std::uint8_t {
        FOLDER = 0,   ///< Directory entry
        FILE,         ///< File entry
    };

    enum class FILE_TIME_COMP_RESULT : std::int16_t {
        FILE_TIME_EQUAL = 0,          ///< File times are equal
        FILE_TIME_FILE_A_OLDER = -1,  ///< File A is older than File B
        FILE_TIME_FILE_B_OLDER = 1,   ///< File A is newer than File B
        FILE_TIME_LENGTH_MISMATCH = -1000, ///< File lengths do not match
    };

    /**
     * Constructs a directory indexer for the specified path
     * @param path Path to index
     * @param topLevel Whether this is the top-level directory
     * @param type Type of index to create
     */
    DirectoryIndexer(const std::filesystem::path &path, bool topLevel = false,
                     INDEX_TYPE type = INDEX_TYPE_LOCAL);

    /**
     * Constructs a directory indexer from existing index data
     * @param path Path the index represents
     * @param folderIndex Existing index data
     * @param topLevel Whether this is the top-level directory
     */
    DirectoryIndexer(const std::filesystem::path &path, const com::fileindexer::Folder &folderIndex,
                     bool topLevel = false);

    /**
     * Destructor
     */
    ~DirectoryIndexer();

    /**
     * Gets the list of deleted files
     * @return Vector of paths to deleted files
     */
    std::vector<std::string> getDeletions(DirectoryIndexer* lastRunIndexer);


    /**
     * Prints the directory index structure
     * @param folderIndex Starting folder to print from, or nullptr for root
     * @param recursionlevel Current depth for indentation
     */
    void printIndex(com::fileindexer::Folder *folderIndex = nullptr, int recursionlevel = 0);

    /**
     * Indexes the directory structure into protobuf format and dumps to file
     * @param verbose Whether to print verbose output
     * @return 0 on success, negative on error
     */
    int indexonprotobuf(bool verbose = false);


    /**
     * Dumps the index to file
     * @param path Name of the file to dump the index to
     * @return 0 on success, negative on error
     */
    int dumpIndexToFile(const std::optional<std::filesystem::path> &path);

    /**
     * Synchronizes directory contents with a remote directory
     * @param folderIndex Current folder being synced
     * @param past Previous local state
     * @param remote Current remote state
     * @param remotePast Previous remote state
     * @param syncCommands List to store required sync operations
     * @param verbose Whether to print verbose output
     * @param isRemote Whether syncing remote to local
     */
    void sync(com::fileindexer::Folder *folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote,
              DirectoryIndexer *remotePast, SyncCommands &syncCommands, bool verbose, bool isRemote);

    /**
     * Counts entries in the index
     * @param folderIndex Starting folder to count from, or nullptr for root
     * @param recursionLevel Maximum depth to count (-1 for unlimited)
     * @return Number of entries found
     */
    size_t count(com::fileindexer::Folder *folderIndex = nullptr, int recursionLevel = 0);

    /**
     * Gets the path being indexed
     * @return Path to the indexed directory
     */
    const std::filesystem::path path();

    /**
     * Sets the path to index
     * @param path New path to index
     */
    void setPath(const std::string &path);

    /**
     * Removes a file/folder from the index
     * @param folderIndex Folder index to remove from
     * @param path Path of the file to remove
     * @param type Type of path (FILE or FOLDER)
     * @return true if removed successfully, false otherwise
     */
    bool removePath(com::fileindexer::Folder *folderIndex, const std::string &path, PATH_TYPE type);

    /**
     * Converts a modified time string to a timespec structure
     * @param modifiedTimeString The modified time string
     * @param timespec Pointer to the timespec structure to fill
     * @return 0 on success, negative on error
     */
    static int make_timespec(const std::string &modifiedTimeString, struct timespec *timespec);

    static std::string file_time_to_string(std::filesystem::file_time_type fileTime);

protected:
    // (none)

private:
    void indexpath(const std::filesystem::path &path, bool verbose);
    com::fileindexer::File *findFileAtPath(com::fileindexer::Folder *folderIndex, const std::string &path, bool verbose);
    std::list<com::fileindexer::File *> findFileFromName(com::fileindexer::Folder *folderIndex,
                                                         const std::string &filename, bool verbose = false);
    com::fileindexer::Folder *findFolderFromName(const std::filesystem::path &filepath, bool verbose);
    std::list<com::fileindexer::File *> findFileFromHash(com::fileindexer::Folder *folderIndex,
                                                         const std::string &hash, bool stopAtFirst,
                                                         bool verbose = false);
    static std::list<std::string> __extractPathComponents(const std::filesystem::path &filepath,
                                                         bool verbose = false);
    void *extract(com::fileindexer::Folder *folderIndex, const std::string &path, PATH_TYPE type);
    void copyTo(com::fileindexer::Folder *folderIndex, ::google::protobuf::Message *element,
                const std::string &path, PATH_TYPE type);
	static FILE_TIME_COMP_RESULT compareFileTime(const std::string& timeA, const std::string& timeB);

    void findDeletedRecursive(const com::fileindexer::Folder& currentFolder, const com::fileindexer::Folder& lastRunFolder, const std::filesystem::path& basePath, std::vector<std::string>& deletions);
    
    static bool isPathInFolder(const std::filesystem::path& pathToCheck, const com::fileindexer::Folder& folder);
    static void* extractFile(com::fileindexer::Folder* folderIndex, const std::string& path);
    static void* extractFolder(com::fileindexer::Folder* folderIndex, const std::string& path);
    void* extractRecursive(com::fileindexer::Folder* folderIndex, const std::string& path, PATH_TYPE type);
    
    void addNewEntry(const std::filesystem::directory_entry& file, com::fileindexer::File& protobufFile, bool verbose, std::filesystem::file_type type);
    void updateFileEntry(const std::filesystem::directory_entry& file, com::fileindexer::File& protobufFile, bool verbose, bool& found);
    void updateFolderEntry(const std::filesystem::directory_entry& file, com::fileindexer::File& protobufFile, bool verbose, bool& found);
    void syncFolders(com::fileindexer::Folder *folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote, DirectoryIndexer *remotePast, SyncCommands &syncCommands, bool verbose, bool isRemote, const DirectoryIndexer *local, bool forcePull);
    void syncFiles(com::fileindexer::Folder *folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote, DirectoryIndexer *remotePast, SyncCommands &syncCommands, bool verbose, bool isRemote, const DirectoryIndexer *local, bool forcePull);
    void postProcessSyncCommands(SyncCommands &syncCommands, DirectoryIndexer *remote);
    void handleFileMissing(com::fileindexer::File& remoteFile, const std::string& remoteFilePath, const std::string& localFilePath, DirectoryIndexer* past, SyncCommands &syncCommands, bool isRemote, bool forcePull, bool verbose);
    static void handleFileConflict(com::fileindexer::File* remoteFile, com::fileindexer::File* localFile, const std::string& remoteFilePath, const std::string& localFilePath, SyncCommands &syncCommands, bool isRemote);
    static void handleFileExists(com::fileindexer::File& remoteFile, com::fileindexer::File* localFile, const std::string& remoteFilePath, const std::string& localFilePath, SyncCommands &syncCommands, bool isRemote);
    static void checkPathLengthWarnings(const std::string& path, const std::string& operation);


    std::filesystem::directory_entry mDir;
    std::fstream mIndexfile;
    bool mUpdateIndexFile;
    com::fileindexer::Folder mFolderIndex;
    bool mTopLevel;
};

#endif // _DIRECTORY_INDEXER_H_