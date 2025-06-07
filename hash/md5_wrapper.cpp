#include "md5_wrapper.h"
#include <cstdint>
#include <cstdio>
#include <md5.h>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <iostream>
#include <fstream>  // IWYU pragma: keep

#define MAX_MD5SUM_BUFFERSIZE       (256 * 1024 * 1024)

MD5Calculator::MD5Calculator( const char * path, bool verbose )
{
    memset(mDigest.digest_native, 0, MD5_DIGEST_LENGHT_NATIVE);

    std::filesystem::path filepath( path );
    if ( ! std::filesystem::exists(filepath) )
        return;

    if ( verbose )
        std::cout <<  filepath << std::endl;

    std::error_code ec;
    uintmax_t filesize = std::filesystem::file_size(filepath, ec);

    if ( ec.value() != 0 )
    {
        std::cerr << ec.message() << '\n';
        return;
    }

    std::filebuf filebuf;

    if ( !filebuf.open(path, std::ios::binary | std::ios::in) )
    {
        std::cout << "Open file " << path << " for read failed\n";
        return;
    }
    
    MD5_CTX ctx;
    MD5Init(&ctx);
    uint8_t * buffer = new uint8_t[MAX_MD5SUM_BUFFERSIZE];
    while ( filesize )
    {
        //read the content into memory in max 256MiB chunks
        size_t buffersize = std::min<long>(filesize, MAX_MD5SUM_BUFFERSIZE);
        auto dataread = filebuf.sgetn(reinterpret_cast<char*>(buffer), buffersize);
        MD5Update(&ctx, buffer, dataread);
        
        filesize -= dataread;
    }
    delete[] buffer;
	filebuf.close();
    
    MD5Final(mDigest.digest_bytes, &ctx);

}

MD5Calculator::MD5Calculator( const std::string &path, bool verbose ) : 
MD5Calculator(path.c_str(), verbose)
{
}