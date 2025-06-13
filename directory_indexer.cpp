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
// (none)

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

            std::cout << " done" << std::endl;
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
        std::cout << std::endl;
        printIndex( &folder, recursionlevel + 1 );
    }
    for ( auto file : *folderIndex->mutable_files() )
    {
        std::cout << tabs << *file.mutable_name();
        std::cout << "\t" << file.permissions();
        std::cout << "\t" << file.type();
        std::cout << "\t" << *file.mutable_modifiedtime();
        std::cout << "\t" << *file.mutable_hash();
        std::cout << std::endl;
    }

}

int DirectoryIndexer::indexonprotobuf( bool verbose )
{
    if ( !mDir.exists() || !mDir.is_directory() )
        return -1;
    
    if ( verbose )
        std::cout << mDir.path() << std::endl;
    
    for ( auto file : std::filesystem::directory_iterator( mDir.path() ) )
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

void DirectoryIndexer::indexpath( const std::filesystem::path &path, bool verbose )
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
    } while ( filetime > indextime );

    com::fileindexer::File protobufFile;
    protobufFile.set_name( file.path()/*.filename()*/ );
    protobufFile.set_permissions( (int)permissions );
    protobufFile.set_type((::com::fileindexer::File_FileType)type);
    protobufFile.set_modifiedtime( std::format( "{0:%F}_{0:%R}.{0:%S}", filetime) );
    
    bool found = false;
    if ( type != std::filesystem::file_type::directory )
    {
        /* search for it in the file index */
        for (int i = 0; i < mFolderIndex.files_size(); i++)
        {
            auto fileInIndex = mFolderIndex.mutable_files()->Mutable(i);
            if ( fileInIndex->name() == protobufFile.name() )
            {
                found = true;
                if ( fileInIndex->permissions() != protobufFile.permissions() ||
                    fileInIndex->type() != protobufFile.type() ||
                    fileInIndex->modifiedtime() != protobufFile.modifiedtime() )
                {
                    mUpdateIndexFile = true;
                    /* recalculate the hash */
                    if ( type == std::filesystem::file_type::regular )
                    {
                        MD5Calculator hash( std::filesystem::canonical(file.path()), verbose );
                        std::string hashstring = hash.getDigest().to_string();
                        *fileInIndex->mutable_hash() = hashstring;
                    }
                    
                    /* update the file entry in the index */
                    fileInIndex->set_permissions( protobufFile.permissions() );
                    fileInIndex->set_type(protobufFile.type() );
                    *fileInIndex->mutable_modifiedtime() = protobufFile.modifiedtime();
                }
                break;
            }
        }
    } else 
    {
        /* search for it in the folder index */
        for (int i = 0; i < mFolderIndex.folders_size(); i++)
        {
            auto folderInIndex = mFolderIndex.mutable_folders()->Mutable(i);
            if ( folderInIndex->name() == protobufFile.name() )
            {
                found = true;
                /*
                if ( folderInIndex->permissions() != protobufFile.permissions() ||
                    folderInIndex->type() != static_cast<com::fileindexer::Folder::FileType>(protobufFile.type()) ||
                    folderInIndex->modifiedtime() != protobufFile.modifiedtime() )
                {*/
                    mUpdateIndexFile = true;
                    
                    /* update the folder entry in the index */
                    DirectoryIndexer indexer( file.path(), *folderInIndex, false );
                    indexer.indexonprotobuf( verbose );

                    *folderInIndex = indexer.mFolderIndex;
                    folderInIndex->set_name( protobufFile.name() );
                    folderInIndex->set_permissions( protobufFile.permissions() );
                    folderInIndex->set_type( static_cast<com::fileindexer::Folder::FileType>(protobufFile.type()) );
                    folderInIndex->set_modifiedtime( protobufFile.modifiedtime() );
                //}
                break;
            }
        }

    }

    if ( !found )
    {
        mUpdateIndexFile = true;
        /* calculate the hash and add it */
        if ( type == std::filesystem::file_type::directory )
        {
            DirectoryIndexer indexer( file.path() );
            indexer.indexonprotobuf( verbose );

            com::fileindexer::Folder folder;
            indexer.mFolderIndex.set_name( protobufFile.name() );
            indexer.mFolderIndex.set_permissions( protobufFile.permissions() );
            indexer.mFolderIndex.set_type( static_cast<com::fileindexer::Folder::FileType>(protobufFile.type()) );
            indexer.mFolderIndex.set_modifiedtime( protobufFile.modifiedtime() );
            
            *mFolderIndex.add_folders() = indexer.mFolderIndex;
        }
        else
        {
            if ( type == std::filesystem::file_type::regular )
            {
                /* regular files carry their hash */
                MD5Calculator hash( std::filesystem::canonical(file.path()), verbose );
                std::string hashstring = hash.getDigest().to_string();
                protobufFile.set_hash( hashstring );
            }
            *mFolderIndex.add_files() = protobufFile;
        }
    }
}

size_t DirectoryIndexer::count( com::fileindexer::Folder *folderIndex, int recursionLevel)
{
    if ( !folderIndex )
        folderIndex = &mFolderIndex;

    static size_t result=folderIndex->files_size();
    for ( auto folder : *folderIndex->mutable_folders() )
        result += count( &folder, recursionLevel + 1 );
    return result;
}

void DirectoryIndexer::sync( com::fileindexer::Folder * folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote, DirectoryIndexer* remotePast, std::list<SyncCommand> &syncCommands, bool verbose, bool isRemote )
{
    if ( remote == nullptr )
        return;
    
    const bool topLevel = !folderIndex;
    const bool forcePull = (past == nullptr);
    const DirectoryIndexer * local = this;

    if ( topLevel )
        folderIndex = &remote->mFolderIndex;
    
    for ( auto &remoteFolder: *folderIndex->mutable_folders() )
    {
        auto remoteFolderPath = remoteFolder.name();
        auto localFolderPath = local->mDir.path().string() + "/" + remoteFolderPath.substr(remote->mDir.path().string().length()+1);
        if ( verbose )
            std::cout << "Entering " << remoteFolderPath << std::endl;
        
        if ( extract(nullptr, localFolderPath, FOLDER) )
        {
            if ( verbose )
                std::cout << "folder exists! " << localFolderPath << std::endl;
            /* matching local folder with identical name exists, simplest case. recurse */
            sync( &remoteFolder, past, remote, remotePast, syncCommands, verbose, isRemote );
        }
        else
        {
            if ( verbose )
                std::cout << "folder missing! " << localFolderPath << std::endl;
            /* remote folder is missing locally */

            if ( forcePull || !past->extract(nullptr, localFolderPath, FOLDER) )
            {
                /* no local history or folder never existed, create folder locally and recurse */
                syncCommands.push_back( SyncCommand( "mkdir", localFolderPath, "", isRemote ) );
                /* update the local index to reflect the new folder */
                copyTo( nullptr, &remoteFolder, localFolderPath, FOLDER );
                /* recurse */
                sync( &remoteFolder, past, remote, remotePast, syncCommands, verbose, isRemote );
            } else
            {
                /* folder was deleted, recurse first then delete it on the remote*/
                sync( &remoteFolder, past, remote, remotePast, syncCommands, verbose, isRemote );
                syncCommands.push_back( SyncCommand( "rmdir", remoteFolderPath, "", !isRemote ) );
            }
        }
    }

    for ( auto &remoteFile: *folderIndex->mutable_files() )
    {
        auto remoteFilePath = remoteFile.name();
        auto localFilePath = local->mDir.path().string() + "/" + remoteFilePath.substr(remote->mDir.path().string().length()+1);
        if ( verbose )
            std::cout << "checking " << remoteFilePath << std::endl;

        auto localFile = static_cast<com::fileindexer::File *>(extract(nullptr, localFilePath, FILE));
        if ( localFile )
        {
            if ( verbose )
                std::cout << "file exists! " << localFilePath << std::endl;

            /* matching local file with identical name exists, perform detailed checks */
            /* no matter what the modified time is, if the content is the same, we don't do anything */
            if ( remoteFile.hash() != localFile->hash() )
            {
                /* files are different, keep the younger one */
                int compResult = compareFileTime( remoteFile.modifiedtime(), localFile->modifiedtime() );
                if ( compResult == -1000 )
                {
                    std::cout << "ERROR IN COMPARING FILE TIMES, STRING OF DIFFERENT LENGTHS !!" << std::endl;
                } else if ( compResult == 0 )
                {
                    std::cout << "ERROR IN COMPARING FILE TIMES, DIFFERENT HASH BUT SAME MODIFIED TIME !!" << std::endl;
                } else if ( compResult > 0 )
                {
                    /* remote file is younger */
                    syncCommands.push_back( SyncCommand("rm", localFilePath, "", isRemote ) );
                    syncCommands.push_back( SyncCommand(isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote ) );
                    localFile->set_hash( remoteFile.hash() );
                } else
                {
                    /* local file is younger */
                    syncCommands.push_back( SyncCommand("rm", remoteFilePath, "", !isRemote ) );
                    syncCommands.push_back( SyncCommand(isRemote ? "fetch" : "push", localFilePath, remoteFilePath, !isRemote ) );
                    remoteFile.set_hash( localFile->hash() );
                }
            }
        } else
        {
            if ( verbose )
                std::cout << "file missing! " << localFilePath << std::endl;
            /* no matching local file, check file history to determine correct action */
            if ( forcePull )
            {
                /* no local history, try to find a local copy */
                auto fileList = findFileFromHash( nullptr, remoteFile.hash(), true, verbose );
                if ( fileList.empty() )
                {
                    /* no local copy found, copy from remote instead */
                    syncCommands.push_back( SyncCommand( isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote ) );
                } else
                {
                    /* we have a local copy, save some bandwidth and copy locally */
                    syncCommands.push_back( SyncCommand( "cp", (*fileList.cbegin())->name(), localFilePath, isRemote ) );
                }
                /* update the local index to reflect the new file */
                copyTo( nullptr, &remoteFile, localFilePath, FILE );
            } else
            {
                /* we have a local history */
                auto localPastFile = static_cast<com::fileindexer::File *>(past->extract(nullptr, localFilePath, FILE));

                if ( localPastFile )
                {
                    /* file was deleted locally, remove it from the remote */
                    syncCommands.push_back( SyncCommand( "rm", remoteFilePath, "", !isRemote ) );
                } else
                {
                    /* file was never there, try to find a local copy */
                    auto fileList = findFileFromHash( nullptr, remoteFile.hash(), true, verbose );
                    if ( fileList.empty() )
                    {
                        /* no local copy found, copy from remote instead */
                        syncCommands.push_back( SyncCommand( isRemote ? "push" : "fetch", remoteFilePath, localFilePath, !isRemote ) );
                    } else
                    {
                        /* we have a local copy, save some bandwidth and copy locally */
                        syncCommands.push_back( SyncCommand( "cp", (*fileList.cbegin())->name(), localFilePath, isRemote ) );
                    }
                    /* update the local index to reflect the new file */
                    copyTo( nullptr, &remoteFile, localFilePath, FILE );
                }
            }
        }
    }

    /* finished outputting sync commands from remote to local */
    /* remove paths to be deleted from the index */
    if ( topLevel )
    {

        for ( auto it = syncCommands.begin(); it != syncCommands.end(); ++it )
        {
            if ( it->isRemoval() )
            {
                PATH_TYPE type;
                if ( it->path1().ends_with("/\"") ) 
                    type = FOLDER;
                else
                    type = FILE;
                    
                if ( !removePath( nullptr, it->path1(), type ) )
                {
                    if ( !remote->removePath( nullptr, it->path1(), type ) )
                    {
                        std::cout << "ERROR: PATH " << it->path1() << " NOT FOUND IN EITHER INDEXES" << std::endl;
                    }
                }
/* may not be necessary */
#if 0
                if ( type == FILE )
                {
                    /* this is a file removal. In the case of a rename, the file might be used as a 
                       source for a local copy. Need to re-order the list to ensure the copy happens
                       before the removal */
                    auto prevIt = --it;
                    auto rmIt = ++it;
                    auto nextIt = ++it;
                    for ( auto it2 = nextIt; it2 != syncCommands.end(); ++it2 )
                    {
                        if ( it2->source() == *rmPath )
                        {
                            while ( it2->source() == *rmPath )
                                ++it2;
                            syncCommands.splice( it2, syncCommands, rmIt );
                        }
                    }
                    it = --nextIt;
                }
#endif
            }
        }

        /* lastly, call the sync function in reverse, to sync from local to remote */
        if ( verbose )
            std::cout << std::endl << "Exporting sync commands from local to remote" << std::endl;
        
        remote->sync( &mFolderIndex, remotePast, this, past, syncCommands, verbose, true );
    }
}


// Section 8: Helper Functions
com::fileindexer::File * DirectoryIndexer::findFileAtPath( com::fileindexer::Folder * folderIndex, const std::string & path, bool verbose )
{
    if ( !folderIndex )
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

                    auto file = findFileAtPath( &folder, temp_path, verbose );
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
    if ( !folderIndex )
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
    if ( !folderIndex )
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

const std::list<std::string> DirectoryIndexer::__extractPathComponents( const std::filesystem::path & filepath, const bool verbose )
{
    std::filesystem::path pathcopy = filepath;
    std::list<std::string> pathComponents;
    do
    {
        pathComponents.push_front( pathcopy.filename() );
        if ( verbose )
            std::cout << pathcopy.filename() << std::endl;
        pathcopy = pathcopy.parent_path();
    } while ( pathcopy != "/" );
    return pathComponents;
}

void * DirectoryIndexer::extract( com::fileindexer::Folder * folderIndex, const std::string & path, const PATH_TYPE type )
{
    if ( !folderIndex )
        folderIndex = &mFolderIndex;

    if ( !path.starts_with( folderIndex->name() ) )
        return nullptr;
    else if ( path == folderIndex->name() )
        return folderIndex;

    if ( path.find_last_of('/') == folderIndex->name().length() )
    {
        /* we found the supposedly correct path */
        switch (type)
        {
            case FILE:
                for ( auto &file : *folderIndex->mutable_files() )
                {
                    if ( file.name() == path )
                        return &file;
                }
                break;
            case FOLDER:
                for ( auto &folder : *folderIndex->mutable_folders() )
                {
                    if ( folder.name() == path )
                        return &folder;
                }
        }
    } else
    {
        for ( auto &folder : *folderIndex->mutable_folders() )
        {
            if ( path.starts_with( folder.name() + "/" ) )
                return extract( &folder, path, type );
        }
    }

    return nullptr;
}

bool DirectoryIndexer::removePath( com::fileindexer::Folder * folderIndex, const std::string & path, const PATH_TYPE type )
{
    if ( !folderIndex )
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
        else if ( path.starts_with( folder->name() ) )
            return removePath( &*folder, path, type );
    }

    return false;
}

void DirectoryIndexer::copyTo( com::fileindexer::Folder * folderIndex, ::google::protobuf::Message *element, const std::string & path, const PATH_TYPE type )
{
    std::string insertPath = path.substr( 0, path.find_last_of( '/' ) );

    auto subFolder = static_cast<com::fileindexer::Folder *>(extract( folderIndex, insertPath, FOLDER ));

    if ( nullptr == subFolder )
    {
        if ( folderIndex == nullptr )
            folderIndex = &mFolderIndex;
        /* error out */
        std::cout << "Error: Couldn't locate " << insertPath << " inside " << folderIndex->name() << std::endl;
        std::cout << "Maybe this info can help:" << std::endl;
        std::cout << "    path = " << path << std::endl;
        std::cout << "    type = " << ((type == FOLDER) ? "FOLDER" : "FILE") << std::endl;
        exit(1);
    }

    if ( type == FOLDER )
    {
        const auto folderToCopy = dynamic_cast<com::fileindexer::Folder*>(element);
        com::fileindexer::Folder newFolder;
        newFolder.set_name( path );
        newFolder.set_permissions( folderToCopy->permissions() );
        newFolder.set_type( folderToCopy->type() );
        newFolder.set_modifiedtime( folderToCopy->modifiedtime() );
        *subFolder->add_folders() = newFolder;
    } else // ( type == FILE )
    {
        const auto fileToCopy = dynamic_cast<com::fileindexer::File*>(element);
        com::fileindexer::File newFile;
        newFile.set_name( path );
        newFile.set_permissions( fileToCopy->permissions() );
        newFile.set_type( fileToCopy->type() );
        newFile.set_modifiedtime( fileToCopy->modifiedtime() );
        newFile.set_hash( fileToCopy->hash() );
        *subFolder->add_files() = newFile;
    }
}

int DirectoryIndexer::compareFileTime(const std::string& a, const std::string& b)
{
    if ( a.length() != b.length() )
        return -1000;
    
    for ( size_t i=0; i < a.length(); ++i )
    {
        auto compresult = a.at(i) - b.at(i);
        if ( compresult )
            return compresult < 0 ? -1 : 1;
    }
    return 0;
}

void DirectoryIndexer::setPath( const std::string &path ) 
{
    const std::filesystem::directory_entry d(path); 
    mDir = d;
    mFolderIndex.set_name( mDir.path() );
}