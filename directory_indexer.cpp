// Section 1: Main Header
#include "directory_indexer.h"

// Section 2: Includes
#include "file.pb.h"
#include "folder.pb.h"
#include "md5_wrapper.h"
#include "sync_command.h"
#include "tcp_command.h"
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <list>
#include <string>
#include <unistd.h>
#include <fcntl.h> /* Definition of AT_* constants */
#include <sys/stat.h>

// Third-Party Includes
#include "termcolor/termcolor.hpp"

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
            std::cout << termcolor::white << "Loading index from file... " << termcolor::reset;
            
            mUpdateIndexFile = false;
            mIndexfile.open( indexpath, std::ios::in );
            mFolderIndex.ParseFromIstream( &mIndexfile );
            mIndexfile.close();

            std::cout << termcolor::green << " done" << termcolor::reset << "\r\n";
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
        std::cout << termcolor::magenta << tabs << *folder.mutable_name() << termcolor::reset;
        std::cout << termcolor::cyan << "\t" << folder.permissions() << termcolor::reset;
        std::cout << termcolor::cyan << "\t" << folder.type() << termcolor::reset;
        std::cout << termcolor::cyan << "\t" << *folder.mutable_modifiedtime() << termcolor::reset;
        std::cout << termcolor::cyan << "\r\n" << termcolor::reset;
        printIndex( &folder, recursionlevel + 1 );
    }
    for ( auto file : *folderIndex->mutable_files() )
    {
        std::cout << termcolor::magenta << tabs << *file.mutable_name() << termcolor::reset;
        std::cout << termcolor::cyan << "\t" << file.permissions() << termcolor::reset;
        std::cout << termcolor::cyan << "\t" << file.type() << termcolor::reset;
        std::cout << termcolor::cyan << "\t" << *file.mutable_modifiedtime() << termcolor::reset;
        std::cout << termcolor::cyan << "\t" << *file.mutable_hash() << termcolor::reset;
        std::cout << termcolor::cyan << "\r\n" << termcolor::reset;
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
        std::cout << mDir.path() << "\r\n";
    
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
        return dumpIndexToFile({});    //default path is .folderindex in the directory being indexed

    return 0;
}

int DirectoryIndexer::dumpIndexToFile(const std::optional<std::filesystem::path> &path) {
    
    auto indexPath = path ? *path : (mDir.path() / ".folderindex");
    if (std::filesystem::exists(indexPath))
        std::filesystem::remove(indexPath);

    std::ofstream outFile( indexPath, std::ios::out );
    if (!outFile) {
        std::cout << termcolor::red << "Failed to open index file for writing: " << indexPath << termcolor::reset << "\r\n";
        std::cerr << "Error: " << strerror(errno) << "\r\n";
        outFile.close();
        return -1;
    }
    mFolderIndex.SerializeToOstream(&outFile);
    outFile.close();
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
    // rudimentary loop to ensure the file time is not in the future
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
    protobufFile.set_modifiedtime(file_time_to_string(filetime));

    // Check for path and filename length warnings
    std::string fullPath = file.path().string();
    std::string filename = file.path().filename().string();
    
    if (fullPath.length() > TcpCommand::MAX_PATH_WARNING_LENGTH) {
        std::cout << termcolor::yellow << "Warning: Path length (" << termcolor::magenta << fullPath.length() 
                  << termcolor::yellow << " characters) exceeds recommended limit (" << termcolor::magenta
                  << TcpCommand::MAX_PATH_WARNING_LENGTH << termcolor::yellow << " characters): " << fullPath 
                  << termcolor::reset << "\r\n";
    }
    
    if (filename.length() > TcpCommand::MAX_FILENAME_WARNING_LENGTH) {
        std::cout << termcolor::yellow << "Warning: Filename length (" << termcolor::magenta << filename.length() 
                  << termcolor::yellow << " characters) exceeds recommended limit (" << termcolor::magenta 
                  << TcpCommand::MAX_FILENAME_WARNING_LENGTH << termcolor::yellow << " characters): " << filename 
                  << termcolor::reset << "\r\n";
    }

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
        if (verbose)
            std::cout << "\r\n" << "Exporting sync commands from local to remote" << "\r\n";
        
        remote->sync(&mFolderIndex, remotePast, this, past, syncCommands, verbose, true);
        
        postProcessSyncCommands(syncCommands, remote);
    }
}

void DirectoryIndexer::syncFolders(com::fileindexer::Folder *folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote, DirectoryIndexer *remotePast, SyncCommands &syncCommands, bool verbose, bool isRemote, const DirectoryIndexer *local, bool forcePull)
{
    for (auto &remoteFolder : *folderIndex->mutable_folders())
    {
        auto remoteFolderPath = remoteFolder.name();
        auto localFolderPath = local->mDir.path().string() + "/" + remoteFolderPath.substr(remote->mDir.path().string().length() + 1);
        if (verbose)
            std::cout << termcolor::cyan << "Entering " << remoteFolderPath << termcolor::reset << "\r\n";

        if (nullptr != extract(nullptr, localFolderPath, FOLDER))
        {
            if (verbose)
                std::cout << termcolor::cyan << "folder exists! " << localFolderPath << termcolor::reset << "\r\n";
            sync(&remoteFolder, past, remote, remotePast, syncCommands, verbose, isRemote);
        }
        else
        {
            if (verbose)
                std::cout << termcolor::cyan << "folder missing! " << localFolderPath << termcolor::reset << "\r\n";

            if (forcePull || (past->extract(nullptr, localFolderPath, FOLDER) == nullptr))
            {
                checkPathLengthWarnings(localFolderPath, "mkdir");
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

void DirectoryIndexer::handleFileConflict(com::fileindexer::File* remoteFile, com::fileindexer::File* localFile, 
                                       const std::string& remoteFilePath, const std::string& localFilePath, 
                                       SyncCommands &syncCommands, bool isRemote)
{
    // Both files exist but have different hashes
    // New approach: Each side keeps their version (renamed) and also gets the other side's version,
    // then creates a symlink from the original path to their own version
    
    // Extract the filename without the path for creating the new filenames
    const std::string baseFileName = std::filesystem::path(remoteFilePath).filename().string();
    const std::string serverBasePath = std::filesystem::path(remoteFilePath).parent_path().string();
    const std::string clientBasePath = std::filesystem::path(localFilePath).parent_path().string();

    // Generate unique filenames by adding device identifiers
    // Using .client and .server as suffixes
    const std::string localServerFilename = clientBasePath + "/" + baseFileName + ".server";
    const std::string localClientFilename = clientBasePath + "/" + baseFileName + ".client";

    std::cout << termcolor::red << "CONFLICT: File content differs between " << localFilePath << " and " << remoteFilePath << "\r\n";
    std::cout << termcolor::yellow << "  Each side will keep their version and receive the other side's version\r\n";
    std::cout << "  Client modified time: " << localFile->modifiedtime() << "\r\n";
    std::cout << "  Server modified time: " << remoteFile->modifiedtime() << "\r\n" << termcolor::reset;
    
    // Step 1: Send copy of file to the other computer
    const std::string targetFilename = isRemote ? localClientFilename : localServerFilename;
    checkPathLengthWarnings(targetFilename, "conflict resolution fetch/push");
    syncCommands.emplace_back(isRemote ? "push" : "fetch", remoteFilePath, targetFilename, isRemote);
    
    // Step 2: Rename both original files to include their origin identifier
    const std::string renameTarget = isRemote ? localServerFilename : localClientFilename;
    checkPathLengthWarnings(renameTarget, "conflict resolution move");
    syncCommands.emplace_back("mv", localFilePath, renameTarget, isRemote);

    // Step 3: Create symlinks from the original locations to their respective local copies
    syncCommands.emplace_back("symlink", isRemote ? localServerFilename : localClientFilename, localFilePath, isRemote);
    
    // No need to update hashes as we're preserving both versions
}

void DirectoryIndexer::handleFileExists(com::fileindexer::File& remoteFile, com::fileindexer::File* localFile, const std::string& remoteFilePath, const std::string& localFilePath, SyncCommands &syncCommands, bool isRemote)
{
    const FILE_TIME_COMP_RESULT timeComparisonResult = compareFileTime(remoteFile.modifiedtime(), localFile->modifiedtime());
    const bool isContentIdentical = (remoteFile.hash() == localFile->hash());
    const bool isPermissionsIdentical = (remoteFile.permissions() == localFile->permissions());

    if (timeComparisonResult == FILE_TIME_COMP_RESULT::FILE_TIME_LENGTH_MISMATCH)
    {
        std::cout << termcolor::red << "ERROR IN COMPARING FILE TIMES, STRING OF DIFFERENT LENGTHS !!" << termcolor::reset << "\r\n";
        return; // Exit early to avoid further processing
    }


    if (!isContentIdentical)
    {
        if (timeComparisonResult == FILE_TIME_COMP_RESULT::FILE_TIME_EQUAL)
        {
            std::cout << termcolor::red << "ERROR IN COMPARING FILE TIMES, DIFFERENT HASH BUT SAME MODIFIED TIME !!" << termcolor::reset << "\r\n";
            return; // Exit early to avoid further processing
        }
        if (timeComparisonResult == FILE_TIME_COMP_RESULT::FILE_TIME_FILE_B_OLDER)
        {
            /* remote file is younger */
            /* this is a file replace, no need to erase the old file */
            //syncCommands.emplace_back("rm", localFilePath, "", isRemote );
            syncCommands.emplace_back(isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote );
            localFile->set_hash(remoteFile.hash());
            localFile->set_modifiedtime(remoteFile.modifiedtime());
        }
        else
        {
            /* local file is younger */
            /* this is a file replace, no need to erase the old file */
            //syncCommands.emplace_back("rm", remoteFilePath, "", !isRemote );
            syncCommands.emplace_back(isRemote ? "fetch" : "push", localFilePath, remoteFilePath, !isRemote );
            remoteFile.set_hash(localFile->hash());
            remoteFile.set_modifiedtime(localFile->modifiedtime());
        }
    }
    if ( !isPermissionsIdentical )
    {
        // The files have the same content but different permissions
        std::cout << termcolor::yellow << "Permissions differ for " << remoteFilePath << " and " << localFilePath << termcolor::reset << "\r\n";

        // Update the permissions of the remote file to match the local file
        if (timeComparisonResult == FILE_TIME_COMP_RESULT::FILE_TIME_EQUAL)
        {
            std::cout << termcolor::red << "ERROR IN COMPARING FILE TIMES, DIFFERENT PERMISSIONS BUT SAME MODIFIED TIME !!" << termcolor::reset << "\r\n";
            return; // Exit early to avoid further processing
        }
        if (timeComparisonResult == FILE_TIME_COMP_RESULT::FILE_TIME_FILE_B_OLDER)
        {
            /* remote file is younger */
            std::ostringstream oss;
            oss << std::oct << remoteFile.permissions();
            syncCommands.emplace_back("chmod", oss.str(), localFilePath, isRemote);
            localFile->set_permissions(remoteFile.permissions());
        }
        else
        {
            /* local file is younger */
            std::ostringstream oss;
            oss << std::oct << localFile->permissions();
            syncCommands.emplace_back( "chmod", oss.str(), remoteFilePath, !isRemote );
            remoteFile.set_permissions(localFile->permissions());
        }
    }
    if ( timeComparisonResult != FILE_TIME_COMP_RESULT::FILE_TIME_EQUAL && isContentIdentical && isPermissionsIdentical)
    {
        std::cout << termcolor::cyan << "Files are identical in content and permissions, but differ in" << termcolor::magenta << " modified time" << termcolor::reset << "\r\n";
        // The files are identical in content and permissions, but may differ in modified time
        if (timeComparisonResult == FILE_TIME_COMP_RESULT::FILE_TIME_FILE_B_OLDER)
        {
            /* remote file is younger */
            
            if (isRemote)
                syncCommands.emplace_back("touch", localFilePath, remoteFile.modifiedtime(), isRemote);
            else
            {
                struct timespec remoteModifiedTimeSpec[2];
                
                remoteModifiedTimeSpec[0].tv_sec = 0;
                remoteModifiedTimeSpec[0].tv_nsec = UTIME_OMIT;
                make_timespec(remoteFile.modifiedtime(), &remoteModifiedTimeSpec[1]);
                utimensat(0, localFilePath.c_str(), remoteModifiedTimeSpec, 0);
            }

            localFile->set_modifiedtime(remoteFile.modifiedtime());
        }
        else
        {
            /* local file is younger */
            if (!isRemote)
                syncCommands.emplace_back("touch", remoteFilePath, localFile->modifiedtime(), !isRemote);
            else
            {
                struct timespec localModifiedTimeSpec[2];

                localModifiedTimeSpec[0].tv_sec = 0;
                localModifiedTimeSpec[0].tv_nsec = UTIME_OMIT;
                make_timespec(localFile->modifiedtime(), &localModifiedTimeSpec[1]);
                utimensat(0, remoteFilePath.c_str(), localModifiedTimeSpec, 0);
            }

            remoteFile.set_modifiedtime(localFile->modifiedtime());
        }

        std::cout << termcolor::green << "Files are identical: " << remoteFilePath << " and " << localFilePath << termcolor::reset << "\r\n";

        //TODO: Compare the file permissions and modified time, if they differ, update the remote file
    }
}

void DirectoryIndexer::handleFileMissing(com::fileindexer::File& remoteFile, const std::string& remoteFilePath, const std::string& localFilePath, DirectoryIndexer* past, SyncCommands &syncCommands, bool isRemote, bool forcePull, bool verbose)
{
    if (forcePull)
    {
        auto fileList = findFileFromHash(nullptr, remoteFile.hash(), true, verbose);
        if (fileList.empty())
        {
            checkPathLengthWarnings(localFilePath, "fetch/push missing file");
            syncCommands.emplace_back(isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote);
        }
        else
        {
            checkPathLengthWarnings(localFilePath, "copy missing file");
            syncCommands.emplace_back("cp", (*fileList.cbegin())->name(), localFilePath, isRemote);
        }
        copyTo(nullptr, &remoteFile, localFilePath, FILE);
    }
    else
    {
        auto *localPastFile = static_cast<com::fileindexer::File *>(past->extract(nullptr, localFilePath, FILE));
        if (localPastFile != nullptr)
        {
            // We have a past version of the file, but no current version on the local side
            // Check if the remote file was modified compared to the past version
            if (remoteFile.hash() != localPastFile->hash())
            {
                // The file was modified on the remote side, so it should be preserved
                // Fetch the modified version to the local side
                auto fileList = findFileFromHash(nullptr, remoteFile.hash(), true, verbose);
                if (fileList.empty())
                {
                    checkPathLengthWarnings(localFilePath, "fetch/push modified file");
                    syncCommands.emplace_back(isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote);
                }
                else
                {
                    checkPathLengthWarnings(localFilePath, "copy modified file");
                    syncCommands.emplace_back("cp", (*fileList.cbegin())->name(), localFilePath, isRemote);
                }
                copyTo(nullptr, &remoteFile, localFilePath, FILE);
            }
            else
            {
                // The file wasn't modified, this is a genuine deletion case
                // Remove it from the remote
                syncCommands.emplace_back("rm", remoteFilePath, "", !isRemote);
            }
        }
        else
        {
            auto fileList = findFileFromHash(nullptr, remoteFile.hash(), true, verbose);
            if (fileList.empty())
            {
                checkPathLengthWarnings(localFilePath, "fetch/push new file");
                syncCommands.emplace_back(isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote);
            }
            else
            {
                checkPathLengthWarnings(localFilePath, "copy new file");
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
            std::cout << termcolor::cyan << "checking " << remoteFilePath << termcolor::reset << "\r\n";

        auto *localFile = static_cast<com::fileindexer::File *>(extract(nullptr, localFilePath, FILE));
        if (localFile != nullptr)
        {
            if (verbose)
                std::cout << termcolor::cyan << "file exists! " << localFilePath << termcolor::reset << "\r\n";

            auto *localPastFile = past == nullptr ? nullptr : static_cast<com::fileindexer::File *>(past->extract(nullptr, localFilePath, FILE));
            auto *remotePastFile = remotePast == nullptr ? nullptr : static_cast<com::fileindexer::File *>(remotePast->extract(nullptr, remoteFilePath, FILE));
            bool isLocalPastFile = (localPastFile != nullptr);
            bool isRemotePastFile = (remotePastFile != nullptr);

            if (remoteFile.hash() != localFile->hash())
            {
                std::cout << termcolor::magenta << "Conflict detected between " << localFilePath << " and " << remoteFilePath << termcolor::reset << "\r\n";
                
                if ((!isRemotePastFile && !isLocalPastFile) || ( !(isRemotePastFile && isLocalPastFile)) || (remotePastFile->hash() != localPastFile->hash()) )
                {
                    // Neither has a past version, This is a file creation conflict case
                    std::cout << termcolor::magenta << "Conflict detected between " << localFilePath << " and " << remoteFilePath << termcolor::reset << "\r\n";
                    handleFileConflict(&remoteFile, localFile, remoteFilePath, localFilePath, syncCommands, isRemote);
                } else if ( (isRemotePastFile && isLocalPastFile) && (remoteFile.hash() != localFile->hash()) )
                {
                    if ( remotePastFile->hash() == localPastFile->hash() )
                    {
                        const std::string previousHash = remotePastFile->hash();

                        if ( (previousHash == remoteFile.hash()) || (previousHash == localFile->hash()) )
                        {
                            std::cout << termcolor::white << "File was modified by one side, sync newer copy" << termcolor::reset << "\r\n";
                            handleFileExists(remoteFile, localFile, remoteFilePath, localFilePath, syncCommands, isRemote);
                        } else
                        {
                            // Both have a past version, this is a file modification conflict case
                            std::cout << termcolor::magenta << "Conflict detected between " << localFilePath << " and " << remoteFilePath << termcolor::reset << "\r\n";
                            handleFileConflict(&remoteFile, localFile, remoteFilePath, localFilePath, syncCommands, isRemote);
                        }
                    }
                } else
                {
                    // One of them has a past version, this is an out-of-sync error
                    std::cout << termcolor::red << "Out-of-sync error detected between " << localFilePath << " and " << remoteFilePath << termcolor::reset << "\r\n";
                }
            }
            else
            {
                handleFileExists(remoteFile, localFile, remoteFilePath, localFilePath, syncCommands, isRemote);
            }
        }
        else
        {
            if (verbose)
                std::cout << termcolor::white << "file missing! " << localFilePath << termcolor::reset << "\r\n";
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

            // Extract the clean path without quotes
            std::string cleanPath = it->path1();
            if (cleanPath.front() == '"' && cleanPath.back() == '"')
            {
                cleanPath = cleanPath.substr(1, cleanPath.length() - 2);
            }
            
            // Check if the path exists in local or remote index before trying to remove
            void* localPath = extract(nullptr, cleanPath, type);
            void* remotePath = remote->extract(nullptr, cleanPath, type);
            
            if (localPath != nullptr && remotePath != nullptr)
            {
                // File exists in both indexes, remove from both
                removePath(nullptr, cleanPath, type);
                remote->removePath(nullptr, cleanPath, type);
            }
            else if (localPath != nullptr)
            {
                // File exists only in local index, remove from local
                removePath(nullptr, cleanPath, type);
            }
            else if (remotePath != nullptr)
            {
                // File exists only in remote index, remove from remote
                remote->removePath(nullptr, cleanPath, type);
            }
            else
            {
                std::cout << termcolor::yellow << "ERROR: PATH " << it->path1() << " NOT FOUND IN EITHER INDEXES. Ignore if you moved the file." << termcolor::reset << "\r\n";
            }
        }
    }
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
            deletions.push_back(filePath.string());
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
            deletions.push_back(folderPath.string());
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
            findDeletedRecursive(*currentMatchingSubFolder, lastRunSubFolder, "", deletions);
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
            std::cout << termcolor::white << pathcopy.filename() << termcolor::reset << "\r\n";
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
        std::cout << termcolor::red << "Error: Couldn't locate " << insertPath << " inside " << folderIndex->name() << termcolor::reset << "\r\n";
        std::cout << termcolor::cyan << "Maybe this info can help:" << termcolor::reset << "\r\n";
        std::cout << termcolor::cyan << "    path = " << path << termcolor::reset << "\r\n";
        std::cout << termcolor::cyan << "    type = " << ((type == FOLDER) ? "FOLDER" : "FILE") << termcolor::reset << "\r\n";
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

void DirectoryIndexer::checkPathLengthWarnings(const std::string& path, const std::string& operation) {
    std::string filename = std::filesystem::path(path).filename().string();
    
    if (path.length() > TcpCommand::MAX_PATH_WARNING_LENGTH) {
        std::cout << termcolor::yellow << "Warning: " << operation << " path length (" << termcolor::magenta 
                  << path.length() << termcolor::yellow << " characters) exceeds recommended limit (" 
                  << termcolor::magenta << TcpCommand::MAX_PATH_WARNING_LENGTH << termcolor::yellow 
                  << " characters): " << path << termcolor::reset << "\r\n";
    }
    
    if (filename.length() > TcpCommand::MAX_FILENAME_WARNING_LENGTH) {
        std::cout << termcolor::yellow << "Warning: " << operation << " filename length (" << termcolor::magenta 
                  << filename.length() << termcolor::yellow << " characters) exceeds recommended limit (" 
                  << termcolor::magenta << TcpCommand::MAX_FILENAME_WARNING_LENGTH << termcolor::yellow 
                  << " characters): " << filename << termcolor::reset << "\r\n";
    }
}

void DirectoryIndexer::setPath( const std::string &path ) 
{
    const std::filesystem::directory_entry dirEntry(path); 
    mDir = dirEntry;
    mFolderIndex.set_name( mDir.path() );
}

// Example input: "2025-07-14_12:48.08.212691030"
int DirectoryIndexer::make_timespec(const std::string &modifiedTimeString, struct timespec *timespec) {
    if (timespec == nullptr) 
        return -1;
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    long nsec = 0;

    if (sscanf(modifiedTimeString.c_str(), "%d-%d-%d_%d:%d.%d.%ld",
               &year, &month, &day, &hour, &min, &sec, &nsec) == 7) {
        struct tm tm_time = {};
        tm_time.tm_year = year - 1900;
        tm_time.tm_mon = month - 1;
        tm_time.tm_mday = day;
        tm_time.tm_hour = hour;
        tm_time.tm_min = min;
        tm_time.tm_sec = sec;
        timespec->tv_sec = timegm(&tm_time);
        timespec->tv_nsec = nsec;
        return 0;
    }
    
    // handle parse error
    timespec->tv_sec = 0;
    timespec->tv_nsec = 0;
    return -1;
}

std::string DirectoryIndexer::file_time_to_string(std::filesystem::file_time_type fileTime)
{
    return std::format("{0:%F}_{0:%R}.{0:%S}", fileTime);
}