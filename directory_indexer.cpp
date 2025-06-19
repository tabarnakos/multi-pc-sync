// Section 1: Main Header
#include "directory_indexer.h"

// Section 2: Includes
#include "file.pb.h"
#include "folder.pb.h"
#include "md5_wrapper.h"
#include "sync_command.h"
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <list>
#include <string>
#include <unistd.h>

// Section 3: Defines and Macros


// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
DirectoryIndexer::DirectoryIndexer(const std::filesystem::path &path, bool topLevel, INDEX_TYPE type ) :
    mDir( path ),
    mUpdateIndexFile( true ),
    mTopLevel( topLevel )
{
    if ( !mDir.exists() || !mDir.is_directory() )
        return;
    
    if ( topLevel )
    {
        std::filesystem::path indexpath = mDir.path();
        switch ( type )
        {
            case INDEX_TYPE_LOCAL:
                indexpath /= ".folderindex";
                break;
            case INDEX_TYPE_LOCAL_LAST_RUN:
                indexpath /= ".folderindex.last_run";
                break;
            case INDEX_TYPE_REMOTE:
                indexpath /= ".remote.folderindex";
                break;
            case INDEX_TYPE_REMOTE_LAST_RUN:
                indexpath /= ".remote.folderindex.last_run";
                break;
            default:
                break;
        }

        /* import existing index from file */
        if ( std::filesystem::exists( indexpath ) )
        {
            std::cout << "Loading index from file... ";
            
            mUpdateIndexFile = false;
            mIndexfile.open( indexpath, std::ios::in );
            std:: cout << mFolderIndex.ParseFromIstream( &mIndexfile );
            mIndexfile.close();

            std::cout << " done" << "\n\r";
        }
        
        if ( mFolderIndex.name().empty() )
            mFolderIndex.set_name( mDir.path() );
    }
}
DirectoryIndexer::DirectoryIndexer(const std::filesystem::path &path, const com::fileindexer::Folder &folderIndex, bool topLevel) :
    mDir( path ),
    mUpdateIndexFile( true ),
    mFolderIndex(folderIndex),
    mTopLevel( topLevel )
{

}

DirectoryIndexer::~DirectoryIndexer()
{ 
}

// Section 6: Static Methods
// ...existing code...

// Section 7: Public/Protected/Private Methods
void DirectoryIndexer::printIndex( com::fileindexer::Folder *folderIndex, int recursionlevel )
{
    if ( folderIndex == nullptr )
        folderIndex = &mFolderIndex;

    std::string tabs = "\t";
    for ( int i = 0; i < recursionlevel; ++i )
    {
        tabs += "\t";
    }
    for ( auto folder : *folderIndex->mutable_folders() )
    {
        std::cout << tabs << *folder.mutable_name();
        std::cout << "\t" << folder.permissions();
        std::cout << "\t" << folder.type();
        std::cout << "\t" << *folder.mutable_modifiedtime();
        std::cout << "\n\r";
        printIndex( &folder, recursionlevel + 1 );
    }
    for ( auto file : *folderIndex->mutable_files() )
    {
        std::cout << tabs << *file.mutable_name();
        std::cout << "\t" << file.permissions();
        std::cout << "\t" << file.type();
        std::cout << "\t" << *file.mutable_modifiedtime();
        std::cout << "\t" << *file.mutable_hash();
        std::cout << "\n\r";
    }

}

std::vector<std::string> DirectoryIndexer::getDeletions(DirectoryIndexer* lastRunIndexer) {
    std::vector<std::string> deletions;
    if (lastRunIndexer == nullptr) {
        // Or handle error appropriately
        return deletions;
    }

    // Start comparison from the root folders of both indexes
    // Pass an empty path initially for basePath, it will be built up during recursion
    findDeletedRecursive(this->mFolderIndex, lastRunIndexer->mFolderIndex, "", deletions);

    return deletions;
}

int DirectoryIndexer::indexonprotobuf( bool verbose )
{
    if ( !mDir.exists() || !mDir.is_directory() )
        return -1;
    
    if ( verbose )
        std::cout << mDir.path() << "\n\r";
    
    for ( const auto& file : std::filesystem::directory_iterator( mDir.path() ) )
    {
        if ( file.path().filename() == ".folderindex" || file.path().filename() == ".remote.folderindex" ||
             file.path().filename() == ".folderindex.last_run" || file.path().filename() == ".remote.folderindex.last_run" ||
             file.path().filename() == "sync_commands.sh" )
            continue;

        indexpath( file.path(), verbose );
    }

    /* check for file deletion */
    // Move all filtered elements to the end of the list.
    int keep = 0;  // number to keep
    for (int i = 0; i < mFolderIndex.files_size(); i++)
    {
        if ( std::filesystem::exists( (mDir.path() / mFolderIndex.files()[i].name()) ) )
        {
            if (keep < i) 
            {
                mFolderIndex.mutable_files()->SwapElements(i, keep);
            }
            ++keep;
        } else
            mUpdateIndexFile = true;
    }
    // Remove the filtered elements.
    mFolderIndex.mutable_files()->DeleteSubrange(keep, mFolderIndex.files_size() - keep);

    /* check for folder deletion */
    // Move all filtered elements to the end of the list.
    keep = 0;  // number to keep
    for (int i = 0; i < mFolderIndex.folders_size(); i++)
    {
        if ( std::filesystem::exists( (mDir.path() / mFolderIndex.folders()[i].name()) ) )
        {
            if (keep < i) 
            {
                mFolderIndex.mutable_folders()->SwapElements(i, keep);
            }
            ++keep;
        } else
            mUpdateIndexFile = true;
    }
    // Remove the filtered elements.
    mFolderIndex.mutable_folders()->DeleteSubrange(keep, mFolderIndex.folders_size() - keep);

    /* output to file */
    if ( mUpdateIndexFile && mTopLevel )
    {
        std::filesystem::path indexpath = mDir.path();
        indexpath /= ".folderindex";

        mIndexfile.open( indexpath, std::ios::out );
        mFolderIndex.SerializeToOstream( &mIndexfile );
        mIndexfile.close();
    }

    return 0;
}

void DirectoryIndexer::updateFileEntry(const std::filesystem::directory_entry& file, com::fileindexer::File& protobufFile, bool verbose, bool& found) {
    for (int i = 0; i < mFolderIndex.files_size(); i++) {
        auto *fileInIndex = mFolderIndex.mutable_files()->Mutable(i);
        if (fileInIndex->name() == protobufFile.name()) {
            found = true;
            if (fileInIndex->permissions() != protobufFile.permissions() ||
                fileInIndex->type() != protobufFile.type() ||
                fileInIndex->modifiedtime() != protobufFile.modifiedtime()) {
                mUpdateIndexFile = true;
                if (file.status().type() == std::filesystem::file_type::regular) {
                    MD5Calculator hash(std::filesystem::canonical(file.path()), verbose);
                    std::string hashstring = hash.getDigest().to_string();
                    *fileInIndex->mutable_hash() = hashstring;
                }
                fileInIndex->set_permissions(protobufFile.permissions());
                fileInIndex->set_type(protobufFile.type());
                *fileInIndex->mutable_modifiedtime() = protobufFile.modifiedtime();
            }
            break;
        }
    }
}

void DirectoryIndexer::updateFolderEntry(const std::filesystem::directory_entry& file, com::fileindexer::File& protobufFile, bool verbose, bool& found) {
    for (int i = 0; i < mFolderIndex.folders_size(); i++) {
        auto *folderInIndex = mFolderIndex.mutable_folders()->Mutable(i);
        if (folderInIndex->name() == protobufFile.name()) {
            found = true;
            mUpdateIndexFile = true;
            DirectoryIndexer indexer(file.path(), *folderInIndex, false);
            indexer.indexonprotobuf(verbose);
            *folderInIndex = indexer.mFolderIndex;
            folderInIndex->set_name(protobufFile.name());
            folderInIndex->set_permissions(protobufFile.permissions());
            folderInIndex->set_type(static_cast<com::fileindexer::Folder::FileType>(protobufFile.type()));
            folderInIndex->set_modifiedtime(protobufFile.modifiedtime());
            break;
        }
    }
}

void DirectoryIndexer::addNewEntry(const std::filesystem::directory_entry& file, com::fileindexer::File& protobufFile, bool verbose, std::filesystem::file_type type) {
    mUpdateIndexFile = true;
    if (type == std::filesystem::file_type::directory) {
        DirectoryIndexer indexer(file.path());
        indexer.indexonprotobuf(verbose);
        indexer.mFolderIndex.set_name(protobufFile.name());
        indexer.mFolderIndex.set_permissions(protobufFile.permissions());
        indexer.mFolderIndex.set_type(static_cast<com::fileindexer::Folder::FileType>(protobufFile.type()));
        indexer.mFolderIndex.set_modifiedtime(protobufFile.modifiedtime());
        *mFolderIndex.add_folders() = indexer.mFolderIndex;
    } else {
        if (type == std::filesystem::file_type::regular) {
            MD5Calculator hash(std::filesystem::canonical(file.path()), verbose);
            std::string hashstring = hash.getDigest().to_string();
            protobufFile.set_hash(hashstring);
        }
        *mFolderIndex.add_files() = protobufFile;
    }
}

void DirectoryIndexer::indexpath(const std::filesystem::path &path, bool verbose)
{
    std::filesystem::directory_entry file(path);

    std::filesystem::file_time_type indextime;
    std::filesystem::file_time_type filetime;
    std::filesystem::perms permissions;
    std::filesystem::file_type type;
    do
    {
        indextime = std::filesystem::__file_clock::now();
        std::filesystem::file_status status = file.status();
        permissions = status.permissions();
        type = status.type();
        filetime = file.last_write_time();
    } while (filetime > indextime);

    com::fileindexer::File protobufFile;
    protobufFile.set_name(file.path());
    protobufFile.set_permissions((int)permissions);
    protobufFile.set_type((::com::fileindexer::File_FileType)type);
    protobufFile.set_modifiedtime(std::format("{0:%F}_{0:%R}.{0:%S}", filetime));

    bool found = false;
    if (type != std::filesystem::file_type::directory) {
        updateFileEntry(file, protobufFile, verbose, found);
    } else {
        updateFolderEntry(file, protobufFile, verbose, found);
    }

    if (!found) {
        addNewEntry(file, protobufFile, verbose, type);
    }
}

size_t DirectoryIndexer::count( com::fileindexer::Folder *folderIndex, int recursionLevel)
{
    if ( folderIndex == nullptr )
        folderIndex = &mFolderIndex;

    static size_t result=folderIndex->files_size();
    for ( auto folder : *folderIndex->mutable_folders() )
        result += count( &folder, recursionLevel + 1 );
    return result;
}

void DirectoryIndexer::sync( com::fileindexer::Folder * folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote, DirectoryIndexer* remotePast, SyncCommands &syncCommands, bool verbose, bool isRemote )
{
    if (remote == nullptr)
        return;

    const bool topLevel = folderIndex == nullptr;
    const bool forcePull = (past == nullptr);
    const DirectoryIndexer *local = this;

    if (topLevel)
        folderIndex = &remote->mFolderIndex;

    syncFolders(folderIndex, past, remote, remotePast, syncCommands, verbose, isRemote, local, forcePull);
    syncFiles(folderIndex, past, remote, remotePast, syncCommands, verbose, isRemote, local, forcePull);

    if (topLevel)
    {
        postProcessSyncCommands(syncCommands, remote);
        if (verbose)
            std::cout << "\n\r" << "Exporting sync commands from local to remote" << "\n\r";
        remote->sync(&mFolderIndex, remotePast, this, past, syncCommands, verbose, true);
    }
}

void DirectoryIndexer::syncFolders(com::fileindexer::Folder *folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote, DirectoryIndexer *remotePast, SyncCommands &syncCommands, bool verbose, bool isRemote, const DirectoryIndexer *local, bool forcePull)
{
    for (auto &remoteFolder : *folderIndex->mutable_folders())
    {
        auto remoteFolderPath = remoteFolder.name();
        auto localFolderPath = local->mDir.path().string() + "/" + remoteFolderPath.substr(remote->mDir.path().string().length() + 1);
        if (verbose)
            std::cout << "Entering " << remoteFolderPath << "\n\r";

        if (nullptr != extract(nullptr, localFolderPath, FOLDER))
        {
            if (verbose)
                std::cout << "folder exists! " << localFolderPath << "\n\r";
            sync(&remoteFolder, past, remote, remotePast, syncCommands, verbose, isRemote);
        }
        else
        {
            if (verbose)
                std::cout << "folder missing! " << localFolderPath << "\n\r";

            if (forcePull || (past->extract(nullptr, localFolderPath, FOLDER) == nullptr))
            {
                syncCommands.emplace_back("mkdir", localFolderPath, "", isRemote);
                copyTo(nullptr, &remoteFolder, localFolderPath, FOLDER);
                sync(&remoteFolder, past, remote, remotePast, syncCommands, verbose, isRemote);
            }
            else
            {
                sync(&remoteFolder, past, remote, remotePast, syncCommands, verbose, isRemote);
                syncCommands.emplace_back("rmdir", remoteFolderPath, "", !isRemote);
            }
        }
    }
}

void DirectoryIndexer::handleFileExists(com::fileindexer::File& remoteFile, com::fileindexer::File* localFile, const std::string& remoteFilePath, const std::string& localFilePath, SyncCommands &syncCommands, bool isRemote)
{
    if (remoteFile.hash() != localFile->hash())
    {
        FILE_TIME_COMP_RESULT compResult = compareFileTime(remoteFile.modifiedtime(), localFile->modifiedtime());
        if (compResult == FILE_TIME_COMP_RESULT::FILE_TIME_LENGTH_MISMATCH)
        {
            std::cout << "ERROR IN COMPARING FILE TIMES, STRING OF DIFFERENT LENGTHS !!" << "\n\r";
        }
        else if (compResult == FILE_TIME_COMP_RESULT::FILE_TIME_EQUAL)
        {
            std::cout << "ERROR IN COMPARING FILE TIMES, DIFFERENT HASH BUT SAME MODIFIED TIME !!" << "\n\r";
        }
        else if (compResult == FILE_TIME_COMP_RESULT::FILE_TIME_FILE_B_OLDER)
        {
            /* remote file is younger */
            syncCommands.emplace_back("rm", localFilePath, "", isRemote );
            syncCommands.emplace_back(isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote );
            localFile->set_hash(remoteFile.hash());
        }
        else
        {
            /* local file is younger */
            syncCommands.emplace_back("rm", remoteFilePath, "", !isRemote );
            syncCommands.emplace_back(isRemote ? "fetch" : "push", localFilePath, remoteFilePath, !isRemote );
            remoteFile.set_hash(localFile->hash());
        }
    }
}

void DirectoryIndexer::handleFileMissing(com::fileindexer::File& remoteFile, const std::string& remoteFilePath, const std::string& localFilePath, DirectoryIndexer* past, SyncCommands &syncCommands, bool isRemote, bool forcePull, bool verbose)
{
    if (forcePull)
    {
        auto fileList = findFileFromHash(nullptr, remoteFile.hash(), true, verbose);
        if (fileList.empty())
        {
            syncCommands.emplace_back(isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote);
        }
        else
        {
            syncCommands.emplace_back("cp", (*fileList.cbegin())->name(), localFilePath, isRemote);
        }
        copyTo(nullptr, &remoteFile, localFilePath, FILE);
    }
    else
    {
        auto *localPastFile = static_cast<com::fileindexer::File *>(past->extract(nullptr, localFilePath, FILE));
        if (localPastFile != nullptr)
        {
            // We have a past version of the file, but no current version
            // This is a file deleting case
            // Remove it from the remote
            syncCommands.emplace_back("rm", remoteFilePath, "", !isRemote);     //TODO: Confirm if isRemote is inverted here
        }
        else
        {
            auto fileList = findFileFromHash(nullptr, remoteFile.hash(), true, verbose);
            if (fileList.empty())
            {
                syncCommands.emplace_back(isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote);
            }
            else
            {
                syncCommands.emplace_back("cp", (*fileList.cbegin())->name(), localFilePath, isRemote);
            }
            copyTo(nullptr, &remoteFile, localFilePath, FILE);
        }
    }
}

void DirectoryIndexer::syncFiles(com::fileindexer::Folder *folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote, DirectoryIndexer *remotePast, SyncCommands &syncCommands, bool verbose, bool isRemote, const DirectoryIndexer *local, bool forcePull)
{
    for (auto &remoteFile : *folderIndex->mutable_files())
    {
        auto remoteFilePath = remoteFile.name();
        auto localFilePath = local->mDir.path().string() + "/" + remoteFilePath.substr(remote->mDir.path().string().length() + 1);
        if (verbose)
            std::cout << "checking " << remoteFilePath << "\n\r";

        auto *localFile = static_cast<com::fileindexer::File *>(extract(nullptr, localFilePath, FILE));
        if (localFile != nullptr)
        {
            if (verbose)
                std::cout << "file exists! " << localFilePath << "\n\r";
            handleFileExists(remoteFile, localFile, remoteFilePath, localFilePath, syncCommands, isRemote);
        }
        else
        {
            if (verbose)
                std::cout << "file missing! " << localFilePath << "\n\r";
            handleFileMissing(remoteFile, remoteFilePath, localFilePath, past, syncCommands, isRemote, forcePull, verbose);
        }
    }
}

void DirectoryIndexer::postProcessSyncCommands(SyncCommands &syncCommands, DirectoryIndexer *remote)
{
    for (auto it = syncCommands.begin(); it != syncCommands.end(); ++it)
    {
        if (it->isRemoval())
        {
            PATH_TYPE type;
            if (it->path1().ends_with("/\""))
                type = FOLDER;
            else
                type = FILE;

            if (!removePath(nullptr, it->path1(), type))
            {
                if (!remote->removePath(nullptr, it->path1(), type))
                {
                    std::cout << "ERROR: PATH " << it->path1() << " NOT FOUND IN EITHER INDEXES" << "\n\r";
                }
            }
        }
    }
    syncCommands.sortCommands();
}


// Section 8: Helper Functions
bool DirectoryIndexer::isPathInFolder(const std::filesystem::path& pathToCheck, const com::fileindexer::Folder& folder) {
    // This helper might need adjustment if used elsewhere, or can be simplified if only for getDeletions context
    for (const auto& file : folder.files()) {
        if (file.name() == pathToCheck.filename().string()) {
            return true;
        }
    }
    return std::ranges::any_of(folder.folders(), [&](const auto& subFolder) {
        return subFolder.name() == pathToCheck.filename().string();
    });
}

void DirectoryIndexer::findDeletedRecursive(const com::fileindexer::Folder& currentFolder, const com::fileindexer::Folder& lastRunFolder, const std::filesystem::path& basePath, std::vector<std::string>& deletions) {
    // Check for deleted files in the lastRunFolder
    for (const auto& lastRunFile : lastRunFolder.files()) {
        std::filesystem::path filePath = basePath / lastRunFile.name();
        bool foundInCurrent = false;
        for (const auto& currentFile : currentFolder.files()) {
            if (currentFile.name() == lastRunFile.name()) {
                foundInCurrent = true;
                break;
            }
        }
        if (!foundInCurrent) {
            // Construct the full path relative to the DirectoryIndexer's root and convert to string
            deletions.push_back((this->mDir.path() / filePath).string());
        }
    }

    // Check for deleted subfolders in the lastRunFolder
    for (const auto& lastRunSubFolder : lastRunFolder.folders()) {
        std::filesystem::path folderPath = basePath / lastRunSubFolder.name();
        bool foundInCurrent = false;
        const com::fileindexer::Folder* currentMatchingSubFolder = nullptr;
        for (const auto& currentSubFolder : currentFolder.folders()) {
            if (currentSubFolder.name() == lastRunSubFolder.name()) {
                foundInCurrent = true;
                currentMatchingSubFolder = &currentSubFolder;
                break;
            }
        }
        if (!foundInCurrent) {
            // If the whole folder is deleted, add its path (as string)
            deletions.push_back((this->mDir.path() / folderPath).string());
            // To list all contents of the deleted folder, you would iterate through lastRunSubFolder files/folders
            // and add their paths as strings to the deletions vector, relative to folderPath.
            // For example:
            // for (const auto& deletedSubFile : lastRunSubFolder.files()) {
            //     deletions.push_back((this->mDir.path() / folderPath / deletedSubFile.name()).string());
            // }
            // ... and recursively for sub-folders within the deleted folder.
            // The current implementation just marks the top-level deleted folder.
        } else if (currentMatchingSubFolder != nullptr) {
            // If folder exists, recurse into it
            findDeletedRecursive(*currentMatchingSubFolder, lastRunSubFolder, folderPath, deletions);
        }
    }
}


com::fileindexer::File * DirectoryIndexer::findFileAtPath( com::fileindexer::Folder * folderIndex, const std::string & path, bool verbose )
{
    if ( folderIndex == nullptr )
        folderIndex = &mFolderIndex;

    auto pathComponents = __extractPathComponents( path, verbose );

    for ( auto it = pathComponents.begin(); it != pathComponents.end(); ++it )
    {
        if ( (++it)-- == pathComponents.end() )
        {
            for ( auto &file : *folderIndex->mutable_files() )
            {
                if (file.name() == *it)
                    return &file;
            }
        } else 
        {
            for ( auto &folder : *folderIndex->mutable_folders() )
            {
                if ( folder.name() == *it )
                {
                    auto pos = path.find_first_of( *it, 0 );
                    std::string temp_path = path.substr( pos + it->length()+1 );

                    auto *file = findFileAtPath( &folder, temp_path, verbose );
                    if ( file != nullptr )
                        return file;
                }
            }
        }
    }
    return nullptr;
}

std::list<com::fileindexer::File *> DirectoryIndexer::findFileFromName( com::fileindexer::Folder * folderIndex, const std::string & filename,  bool verbose )
{
    if ( folderIndex == nullptr )
        folderIndex = &mFolderIndex;
    
    std::list<com::fileindexer::File *> files_list;

    for ( auto &folder : *folderIndex->mutable_folders() )
    {
        auto files = findFileFromName( &folder, filename, verbose );
        if ( !files.empty() )
            files_list.insert(files_list.end(), files.begin(), files.end());
    }
    for ( auto &file : *folderIndex->mutable_files() )
    {
        if ( std::filesystem::path(file.name()).filename() == filename )
            files_list.push_back(&file);
    }
    return files_list;
}

com::fileindexer::Folder * DirectoryIndexer::findFolderFromName( const std::filesystem::path & filepath,  bool verbose )
{
    auto pathComponents = __extractPathComponents( filepath, verbose );

    com::fileindexer::Folder * localFolderIndex = &this->mFolderIndex;
    for ( auto it = pathComponents.begin(); it != pathComponents.end(); ++it )
    {
        for ( auto &folder : *localFolderIndex->mutable_folders() )
        {
            if ( folder.name() == *it )
            {
                return &folder;
            }
        }
    }
    return nullptr;
}

std::list<com::fileindexer::File *> DirectoryIndexer::findFileFromHash( com::fileindexer::Folder * folderIndex, const std::string & hash, bool stopAtFirst, bool verbose)
{
    if ( folderIndex == nullptr )
        folderIndex = &mFolderIndex;

    std::list<com::fileindexer::File *> files_list;

    for ( auto &folder : *folderIndex->mutable_folders() )
    {
        auto files = findFileFromHash( &folder, hash, stopAtFirst, verbose );
        if ( !files.empty() )
            files_list.insert(files_list.end(), files.begin(), files.end());
    }
    for ( auto &file : *folderIndex->mutable_files() )
    {
        if ( file.hash() == hash )
        {
            files_list.push_back(&file);
            if ( stopAtFirst )
                return files_list;
        }
    }
    return files_list;
}

std::list<std::string> DirectoryIndexer::__extractPathComponents( const std::filesystem::path & filepath, const bool verbose )
{
    std::filesystem::path pathcopy = filepath;
    std::list<std::string> pathComponents;
    do
    {
        pathComponents.push_front( pathcopy.filename() );
        if ( verbose )
            std::cout << pathcopy.filename() << "\n\r";
        pathcopy = pathcopy.parent_path();
    } while ( pathcopy != "/" );
    return pathComponents;
}

void* DirectoryIndexer::extractFile(com::fileindexer::Folder* folderIndex, const std::string& path) {
    for (auto& file : *folderIndex->mutable_files()) {
        if (file.name() == path)
            return &file;
    }
    return nullptr;
}

void* DirectoryIndexer::extractFolder(com::fileindexer::Folder* folderIndex, const std::string& path) {
    for (auto& folder : *folderIndex->mutable_folders()) {
        if (folder.name() == path)
            return &folder;
    }
    return nullptr;
}

void* DirectoryIndexer::extractRecursive(com::fileindexer::Folder* folderIndex, const std::string& path, const PATH_TYPE type) {
    for (auto& folder : *folderIndex->mutable_folders()) {
        if (path.starts_with(folder.name() + "/"))
            return extract(&folder, path, type);
    }
    return nullptr;
}

void * DirectoryIndexer::extract( com::fileindexer::Folder * folderIndex, const std::string & path, const PATH_TYPE type )
{
    if ( folderIndex == nullptr )
        folderIndex = &mFolderIndex;

    if ( !path.starts_with( folderIndex->name() ) )
        return nullptr;
    if ( path == folderIndex->name() )
        return folderIndex;

    if ( path.find_last_of('/') == folderIndex->name().length() )
    {
        switch (type)
        {
            case FILE:
                return extractFile(folderIndex, path);
            case FOLDER:
                return extractFolder(folderIndex, path);
        }
    }
    else
    {
        return extractRecursive(folderIndex, path, type);
    }

    return nullptr;
}

bool DirectoryIndexer::removePath( com::fileindexer::Folder * folderIndex, const std::string & path, const PATH_TYPE type )
{
    if ( folderIndex == nullptr )
        folderIndex = &mFolderIndex;

    if ( !path.starts_with( folderIndex->name() ) )
        return false;

    if ( type == FILE && path.find_last_of('/') == folderIndex->name().length() )
    {
        for ( auto file = folderIndex->mutable_files()->begin(); file != folderIndex->mutable_files()->end(); ++file )
        {
            if ( file->name() == path )
            {
                /* file found, remove from index */
                folderIndex->mutable_files()->erase( file );
                return true;
            }
        }
        return false;
    }
    for ( auto folder = folderIndex->mutable_folders()->begin(); folder != folderIndex->mutable_folders()->end(); ++folder )
    {
        if ( type == FOLDER && folder->name() == path )
        {
            /* folder found, remove from index */
            folderIndex->mutable_folders()->erase( folder );
            return true;
        }
        if ( path.starts_with( folder->name() ) )
            return removePath( &*folder, path, type );
    }

    return false;
}

void DirectoryIndexer::copyTo( com::fileindexer::Folder * folderIndex, ::google::protobuf::Message *element, const std::string & path, const PATH_TYPE type )
{
    std::string insertPath = path.substr( 0, path.find_last_of( '/' ) );

    auto *subFolder = static_cast<com::fileindexer::Folder *>(extract( folderIndex, insertPath, FOLDER ));

    if ( nullptr == subFolder )
    {
        if ( folderIndex == nullptr )
            folderIndex = &mFolderIndex;
        /* error out */
        std::cout << "Error: Couldn't locate " << insertPath << " inside " << folderIndex->name() << "\n\r";
        std::cout << "Maybe this info can help:" << "\n\r";
        std::cout << "    path = " << path << "\n\r";
        std::cout << "    type = " << ((type == FOLDER) ? "FOLDER" : "FILE") << "\n\r";
        exit(1);
    }

    if ( type == FOLDER )
    {
        auto *const folderToCopy = dynamic_cast<com::fileindexer::Folder*>(element);
        com::fileindexer::Folder newFolder;
        newFolder.set_name( path );
        newFolder.set_permissions( folderToCopy->permissions() );
        newFolder.set_type( folderToCopy->type() );
        newFolder.set_modifiedtime( folderToCopy->modifiedtime() );
        *subFolder->add_folders() = newFolder;
    } else // ( type == FILE )
    {
        auto *const fileToCopy = dynamic_cast<com::fileindexer::File*>(element);
        com::fileindexer::File newFile;
        newFile.set_name( path );
        newFile.set_permissions( fileToCopy->permissions() );
        newFile.set_type( fileToCopy->type() );
        newFile.set_modifiedtime( fileToCopy->modifiedtime() );
        newFile.set_hash( fileToCopy->hash() );
        *subFolder->add_files() = newFile;
    }
}

DirectoryIndexer::FILE_TIME_COMP_RESULT DirectoryIndexer::compareFileTime(const std::string& timeA, const std::string& timeB)
{
    if (timeA.length() != timeB.length())
        return FILE_TIME_COMP_RESULT::FILE_TIME_LENGTH_MISMATCH;

    for (size_t i = 0; i < timeA.length(); ++i) {
        int diff = static_cast<unsigned char>(timeA[i]) - static_cast<unsigned char>(timeB[i]);
        if (diff < 0)
            return FILE_TIME_COMP_RESULT::FILE_TIME_FILE_A_OLDER;
        if (diff > 0)
            return FILE_TIME_COMP_RESULT::FILE_TIME_FILE_B_OLDER;
    }
    return FILE_TIME_COMP_RESULT::FILE_TIME_EQUAL;
}

void DirectoryIndexer::setPath( const std::string &path ) 
{
    const std::filesystem::directory_entry dirEntry(path); 
    mDir = dirEntry;
    mFolderIndex.set_name( mDir.path() );
}