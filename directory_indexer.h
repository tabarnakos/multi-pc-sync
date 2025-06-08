#ifndef _DIRECTORY_INDEXER_H_
#define _DIRECTORY_INDEXER_H_

#include <cstddef>
#include <fstream>
#include <filesystem>
#include <string>

#include "folder.pb.h"

class SyncCommand;

class DirectoryIndexer
{
public:

	enum INDEX_TYPE
	{
		INDEX_TYPE_LOCAL = 0,
		INDEX_TYPE_LOCAL_LAST_RUN,
		INDEX_TYPE_REMOTE,
		INDEX_TYPE_REMOTE_LAST_RUN,
	};

	enum PATH_TYPE
	{
		FOLDER = 0,
		FILE,
	};

	DirectoryIndexer(const std::filesystem::path &path, bool topLevel = false, INDEX_TYPE type = INDEX_TYPE_LOCAL);
    DirectoryIndexer(const std::filesystem::path &path, const com::fileindexer::Folder &folderIndex, bool topLevel = false);

	~DirectoryIndexer();

	void printIndex( com::fileindexer::Folder *folderIndex = nullptr, int recursionlevel = 0 );

	int indexonprotobuf( bool verbose = false );

	void syncFrom( DirectoryIndexer &other, std::list<SyncCommand> &syncCommands, bool verbose = false );
	void sync( com::fileindexer::Folder * folderIndex, DirectoryIndexer *past, DirectoryIndexer *remote, DirectoryIndexer* remotePast, std::list<SyncCommand> &syncCommands, bool verbose );

	size_t count( com::fileindexer::Folder *folderIndex = nullptr, int recursionLevel = 0 );

	inline const std::filesystem::path path() { return mDir.path(); }

	inline void setPath( const std::string &path ) 
	{
		const std::filesystem::directory_entry d(path); 
		mDir = d;
		mFolderIndex.set_name( mDir.path() );
	}

	static int compareFileTime(const std::string& a, const std::string& b)
	{
		if ( a.length() != b.length() )
			return -1000;
		
		for ( int i=0; i < a.length(); ++i )
		{
			auto compresult = a.at(i) - b.at(i);
			if ( compresult )
				return compresult < 0 ? -1 : 1;
		}
		return 0;
	}
	
private:

	void indexpath( const std::filesystem::path &path, bool verbose );

	com::fileindexer::File * findFileAtPath( com::fileindexer::Folder * folderIndex, const std::string & path, bool verbose );
	std::list<com::fileindexer::File *> findFileFromName( com::fileindexer::Folder * folderIndex, const std::string & filename,  bool verbose = false );
	com::fileindexer::Folder * findFolderFromName( const std::filesystem::path & filepath,  bool verbose );
	std::list<com::fileindexer::File *> findFileFromHash( com::fileindexer::Folder * folderIndex, const std::string & hash, bool stopAtFirst,  bool verbose = false );
	const std::list<std::string> __extractPathComponents( const std::filesystem::path & filepath, const bool verbose = false );
	void * extract( com::fileindexer::Folder * folderIndex, const std::string & path, const PATH_TYPE type );
	bool removePath( com::fileindexer::Folder * folderIndex, const std::string & path, const PATH_TYPE type );
	void copyTo( com::fileindexer::Folder * folderIndex, ::google::protobuf::Message *element, const std::string & path, const PATH_TYPE type );
	//constexpr std::list<std::string> DirectoryIndexer::__extractPathComponents( const com::fileindexer::File & file, const bool verbose = false );


	std::filesystem::directory_entry mDir;
	std::fstream mIndexfile;
	bool mUpdateIndexFile;
	com::fileindexer::Folder mFolderIndex;
	bool mTopLevel;
};

#endif