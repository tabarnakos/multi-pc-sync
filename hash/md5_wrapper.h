// *****************************************************************************
// MD5 Wrapper Header
// *****************************************************************************

#ifndef __MD5_WRAPPER_H__
#define __MD5_WRAPPER_H__

// Section 1: Includes
// C++ Standard Library
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

// Project Includes
#include <md5.h>

// Section 2: Defines and Macros
#define MD5_DIGEST_LENGHT_NATIVE (MD5_DIGEST_LENGTH)

// Section 3: Class Definition
class MD5Calculator 
{
public:
    MD5Calculator(const char *path, bool verbose);
    MD5Calculator(const std::string &path, bool verbose);
    virtual ~MD5Calculator() {}

    using MD5Digest = struct _MD5Digest
    {
        union
        {
            uint8_t digest_bytes[MD5_DIGEST_LENGTH];
            uint64_t digest_native[MD5_DIGEST_LENGHT_NATIVE];
        };
        constexpr std::string to_string() {
            std::stringstream md5Stream;
            md5Stream << std::hex << std::setfill('0') << std::setw(2*sizeof(uint64_t));
            for (auto i = 0; i < 2; ++i)
            {
                const uint64_t word = digest_native[i];
                md5Stream << __bswap_64(word);
            }
            return md5Stream.str();
        }
        bool operator==(_MD5Digest& other);
    };

    MD5Digest &getDigest() { return mDigest; }

private:
    MD5Digest mDigest;
};

#endif // __MD5_WRAPPER_H__
