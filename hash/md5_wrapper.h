// *****************************************************************************
// MD5 Wrapper Header
// *****************************************************************************

#ifndef __MD5_WRAPPER_H__
#define __MD5_WRAPPER_H__

// Section 1: Includes
// C++ Standard Library
#include <cstring>
#include <string>

// Project Includes
#include <md5.h>

// Section 2: Defines and Macros
#define MD5_DIGEST_LENGHT_NATIVE (MD5_DIGEST_LENGTH/sizeof(uint64_t))

// Section 3: Class Definition
class MD5Calculator 
{
public:
    MD5Calculator(const char *path, bool verbose);
    MD5Calculator(const std::string &path, bool verbose);
    virtual ~MD5Calculator() {}

    typedef struct _MD5Digest
    {
        union
        {
            uint8_t digest_bytes[MD5_DIGEST_LENGTH];
            uint64_t digest_native[MD5_DIGEST_LENGHT_NATIVE];
        };
        std::string to_string();
        bool operator==(_MD5Digest& other);
    } MD5Digest;

    inline MD5Digest &getDigest() { return mDigest; }

private:
    MD5Digest mDigest;
};

#endif // __MD5_WRAPPER_H__
