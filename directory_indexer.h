// Section 1: Compilation Guards
#ifndef _DIRECTORY_INDEXER_H_
#define _DIRECTORY_INDEXER_H_

// Section 2: Includes
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "folder.pb.h"

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
    enum INDEX_TYPE {
        INDEX_TYPE_LOCAL = 0,          ///< Current local directory state
        INDEX_TYPE_LOCAL_LAST_RUN,     ///< Previous local directory state
        INDEX_TYPE_REMOTE,             ///< Current remote directory state
        INDEX_TYPE_REMOTE_LAST_RUN,    ///< Previous remote directory state
    };

    /**
     * Type of filesystem entry
     */
    enum PATH_TYPE {
        FOLDER = 0,   ///< Directory entry
        FILE,         ///< File entry
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
     * Prints the directory index structure
     * @param folderIndex Starting folder to print from, or nullptr for root
     * @param recursionlevel Current depth for indentation
     */
    void printIndex(com::fileindexer::Folder *folderIndex = nullptr, int recursionlevel = 0);

    /**
     * Indexes the directory structure into protobuf format
     * @param verbose Whether to print verbose output
     * @return 0 on success, negative on error
     */
    int indexonprotobuf(bool verbose = false);

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
              DirectoryIndexer *remotePast, std::list<SyncCommand> &syncCommands, bool verbose, bool isRemote);

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
    const std::list<std::string> __extractPathComponents(const std::filesystem::path &filepath,
                                                        const bool verbose = false);
    void *extract(com::fileindexer::Folder *folderIndex, const std::string &path, const PATH_TYPE type);
    bool removePath(com::fileindexer::Folder *folderIndex, const std::string &path, const PATH_TYPE type);
    void copyTo(com::fileindexer::Folder *folderIndex, ::google::protobuf::Message *element,
                const std::string &path, const PATH_TYPE type);
	static int compareFileTime(const std::string& a, const std::string& b);

    std::filesystem::directory_entry mDir;
    std::fstream mIndexfile;
    bool mUpdateIndexFile;
    com::fileindexer::Folder mFolderIndex;
    bool mTopLevel;
};

#endif // _DIRECTORY_INDEXER_H_