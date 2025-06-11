// Section 1: Main Header
#include "growing_buffer.h"

// Section 2: Includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <vector>

// Section 3: Defines and Macros
// (none)

// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
GrowingBuffer::GrowingBuffer() {}

GrowingBuffer::~GrowingBuffer() {
    for (auto buf : mBuffers)
        free(buf);
}

// Section 6: Static Methods
// (none)

// Section 7: Public/Protected/Private Methods
size_t GrowingBuffer::read(void *buf, const size_t size) {
    if ( advise_access(size) < 0 ) {
        return 0; // Error: advise_access failed
    }
    size_t bytes_left = std::min(size, mSize - mPublicIndex);
    while (bytes_left > 0) {
        size_t readlen = std::min(mBufferSizes[mBufferIndex] - mIndex, bytes_left);
        memcpy(buf, &((uint8_t *)mBuffers[mBufferIndex])[mIndex], readlen);
        
        bytes_left -= readlen;
        buf = (uint8_t *)buf + readlen;
        if ( move(readlen) < 0 ) {
            return size - bytes_left; // Error: move failed
        }
    }
    return size - bytes_left;
}

size_t GrowingBuffer::write(const void *buf, const size_t size) {
    if ( advise_access(size) < 0 ) {
        return 0; // Error: advise_access failed
    }
    if (size == 0) return 0; // Nothing to write

    size_t bytes_left = size;
    while (bytes_left > 0) {
        size_t writelen = std::min(mBufferSizes[mBufferIndex] - mIndex, bytes_left);
        memcpy((uint8_t *)(mBuffers[mBufferIndex]) + mIndex, buf, writelen);
        bytes_left -= writelen;
        buf = (uint8_t *)buf + writelen;
        if ( move(writelen) < 0 ) {
            return size - bytes_left; // Error: move failed
        }
    }
    return size - bytes_left;
}

void GrowingBuffer::dumpToFile(FILE *fd, uintmax_t size) {
    size_t total_left = mSize - (mBufferIndex * mBufferSizes[0] + mIndex);
    if (size > total_left) size = total_left; // Clamp to available data
    std::cout << "dump " << size << " bytes to file" << std::endl;
    std::cout << "mBufferIndex = " << mBufferIndex << std::endl;
    std::cout << "mBufferSizes[mBufferIndex] = " << mBufferSizes[mBufferIndex] << std::endl;
    std::cout << "mIndex = " << mIndex << std::endl;
    std::cout << "data = ";
    for (int i = 0; i < 8 && (mIndex + i) < mBufferSizes[mBufferIndex]; ++i) {
        printf("%02x ", *(static_cast<uint8_t *>(mBuffers[mBufferIndex]) + mIndex + i));
    }
    std::cout << std::endl;
    while (size > 0) {
        size_t chunk = std::min(mBufferSizes[mBufferIndex] - mIndex, (size_t)size);
        auto w_size = fwrite(static_cast<uint8_t *>(mBuffers[mBufferIndex]) + mIndex, 1, chunk, fd);
        mIndex = mIndex + w_size;
        if (mIndex == mBufferSizes[mBufferIndex]) {
            mIndex = 0;
            if (++mBufferIndex == mBuffers.size())
                mBufferIndex = 0;
        }
        size -= w_size;
        if (w_size == 0) {
            // Error: write failed
            break;
        }
    }
}

void GrowingBuffer::dump(std::ostream &os) {
    size_t prevIndex = mPublicIndex;
    this->seek(0, SEEK_SET);
    size_t dataSize = this->size();
    std::vector<uint8_t> buffer(dataSize);
    this->read(buffer.data(), dataSize);
    os << "Raw mData (" << dataSize << " bytes):" << std::endl;
    for (size_t i = 0; i < dataSize; ++i) {
        if ( i % 8 == 0 && i != 0 )
            os << "  ";
        if (i % 16 == 0)
            os << std::endl << std::hex << std::setw(8) << std::setfill('0') << i << ": ";
        os << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
    }
    os << std::dec << std::endl;
    this->seek(prevIndex, mPublicIndex);
}

int GrowingBuffer::seek(int off, int whence) {
    switch (whence) {
        case SEEK_CUR:
            return move(off);
        case SEEK_END:
            return move(off + mSize - mPublicIndex);
        case SEEK_SET:
            return move(off - mPublicIndex);
        default:
            return -1;
    }
}

size_t GrowingBuffer::tell() {
    return mPublicIndex;
}

int GrowingBuffer::move(int off) {
    if (off > 0)
    {
        if ( advise_access(off) < 0 ) {
            return -1; // Error: advise_access failed
        }
    }
    mPublicIndex += off;
    if (off < 0) {
        while (off != 0) {
            if (mIndex >= -off) {
                mIndex += off;
                off = 0;
            } else {
                if (mBufferIndex == 0) {
                    // Prevent underflow: clamp to start of buffer
                    mIndex = 0;
                    mPublicIndex = 0;
                    // Return early or set an error code if needed
                    return -1;
                }
                off += mIndex + 1;
                if (mBufferIndex == 0) {
                    // Prevent underflow again (double check)
                    mIndex = 0;
                    mPublicIndex = 0;
                    return -1;
                }
                mBufferIndex--;
                if (mBufferIndex >= mBufferSizes.size()) {
                    // Error: underflow, clamp to start and return
                    mBufferIndex = 0;
                    mIndex = 0;
                    mPublicIndex = 0;
                    return -1;
                }
                mIndex = mBufferSizes[mBufferIndex] - 1;
            }
        }
    } else if (off > 0) {
        while (off != 0) {
            size_t remain = mBufferSizes[mBufferIndex] - mIndex;
            if (remain > off) {
                mIndex += off;
                off = 0;
            } else {
                off -= remain;
                if (mBufferIndex == mBufferSizes.size() - 1) {
                    // At the last buffer - clamp to end
                    mIndex = mBufferSizes[mBufferIndex];
                    // Don't modify mPublicIndex since it's already been updated
                    return 0;
                }
                mIndex = 0;
                mBufferIndex++;
                // This check is no longer needed since we check for last buffer above
                // but keep it as a safety net
                if (mBufferIndex >= mBufferSizes.size()) {
                    mBufferIndex = mBufferSizes.size() - 1;
                    mIndex = mBufferSizes[mBufferIndex];
                    return 0;
                }
            }
        }
    }
    return 0;
}

int GrowingBuffer::advise_access(size_t size) {
    if (mPublicIndex + size > mSize) {
        // Check for integer overflow
        if (size > (SIZE_MAX - mPublicIndex)) {
            return -1;
        }
        
        size_t new_buf_size = mPublicIndex - mSize + size;
        if (new_buf_size == 0) {
            // Error: do not allocate zero-sized buffer
            return -1;
        }

        // Protect against excessive allocation
        if (new_buf_size > (1 << 30)) { // 1GB limit
            return -1;
        }

        void *p = malloc(new_buf_size);
        if (!p) {
            // Error: allocation failed
            return -1;
        }

        memset(p, 0, new_buf_size);
        mBuffers.push_back(p);
        mBufferSizes.push_back(new_buf_size);
        if (mPublicIndex == mSize) {
            // we were at the end of the buffer, and mPublicIndex was pointing to nothing
            // reset the indexes to the same position, but now pointing to data
            mBufferIndex = mBuffers.size() - 1;
            mIndex = 0;
        }
        mSize += new_buf_size;
    }
    return 0;
}