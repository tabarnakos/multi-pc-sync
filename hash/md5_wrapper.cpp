// *****************************************************************************
// MD5 Wrapper Implementation
// *****************************************************************************

// Section 1: Includes
// C++ Standard Library
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>

// Project Includes
#include "md5_wrapper.h"
#include <md5.h>

// Section 2: Defines and Macros
#define MAX_MD5SUM_BUFFERSIZE (256 * 1024 * 1024)

// Section 3: MD5Calculator Implementation
MD5Calculator::MD5Calculator(const char *path, bool verbose)
{
    memset(mDigest.digest_native, 0, MD5_DIGEST_LENGHT_NATIVE*sizeof(uint64_t));

    std::filesystem::path filepath( path );
    if ( ! std::filesystem::exists(filepath) )
        return;

    if ( verbose )
        std::cout <<  filepath << "\n\r";

    std::error_code ec;
    uintmax_t filesize = std::filesystem::file_size(filepath, ec);

    if ( ec.value() != 0 )
    {
        std::cerr << ec.message() << "\n\r";
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

std::string MD5Calculator::MD5Digest::to_string() {
    std::stringstream ss;
    ss << std::hex << std::setw(sizeof(uint64_t)) << std::setfill('0');
    for (uint64_t i = 0; i < MD5_DIGEST_LENGHT_NATIVE; ++i)
    {
        ss << __bswap_64(digest_native[i]);
    }
    return ss.str();
}

bool MD5Calculator::MD5Digest::operator==(MD5Calculator::MD5Digest& other)
{
    return memcmp(this->digest_native, other.digest_native, MD5_DIGEST_LENGHT_NATIVE) == 0;
}