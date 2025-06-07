#ifndef __MD5_WRAPPER_H__
#define __MD5_WRAPPER_H__
#include <cstring>
#include <iterator>
#include <md5.h>
#include <sstream>
#include <string>
#include <iomanip>
#include <iostream>

#define MD5_DIGEST_LENGHT_NATIVE        (MD5_DIGEST_LENGTH/sizeof(uint64_t))

class MD5Calculator 
{
public:
    MD5Calculator( const char * path, bool verbose );
    MD5Calculator( const std::string &path, bool verbose );

    virtual ~MD5Calculator() {}

    typedef struct _MD5Digest
    {
        union
        {
            uint8_t digest_bytes[MD5_DIGEST_LENGTH];
            uint64_t digest_native[MD5_DIGEST_LENGHT_NATIVE];
        };
        
        std::string to_string() {
                                    std::stringstream ss;
                                    ss << std::hex << std::setw(sizeof(uint64_t)) << std::setfill('0');
                                    int i = 0;
                                    for (uint64_t i = 0; i < MD5_DIGEST_LENGHT_NATIVE; ++i)
                                    {
                                        ss  << __bswap_64(digest_native[i]);
                                    }
                                    return ss.str();
                                }

        bool operator==( _MD5Digest& other)
        {
            return memcmp(this->digest_native, other.digest_native, MD5_DIGEST_LENGHT_NATIVE);
        }
    } MD5Digest;


    inline MD5Digest & getDigest() {return mDigest;}

private:

    MD5Digest mDigest;
};


#endif //__MD5_WRAPPER_H_
