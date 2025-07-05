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
#include <print>
#include <vector>

// Third-Party Includes
#include "termcolor/termcolor.hpp"

// Section 3: Defines and Macros
constexpr int DUMP_PREVIEW_BYTES = 8;
constexpr int DUMP_LINE_BYTES = 16;
constexpr size_t ONE_GIGABYTE = 1ULL << 30; // 1 GiB

// Section 4: Static Variables
// (none)

// Section 5: Constructors and Destructors
GrowingBuffer::GrowingBuffer() = default;

GrowingBuffer::~GrowingBuffer() {
    for (auto *buf : mBuffers)
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

void GrowingBuffer::dumpToFile(FILE *file, uintmax_t size) {
    size_t total_left = mSize - (mBufferIndex * mBufferSizes[0] + mIndex);
    size = std::min(size, total_left); // Clamp to available data
    std::cout << termcolor::cyan << "dump " << size << " bytes to file" << "\r\n" << termcolor::reset;
    std::cout << termcolor::cyan << "\tmBufferIndex = " << mBufferIndex << "\r\n" << termcolor::reset;
    std::cout << termcolor::cyan << "\tmBufferSizes[mBufferIndex] = " << mBufferSizes[mBufferIndex] << "\r\n" << termcolor::reset;
    std::cout << termcolor::cyan << "\tmIndex = " << mIndex << "\r\n" << termcolor::reset;
    std::cout << termcolor::cyan << "\tdata = ";
    for (int i = 0; i < DUMP_PREVIEW_BYTES && (mIndex + i) < mBufferSizes[mBufferIndex]; ++i) {
        std::print("{:02x} ", *(static_cast<uint8_t *>(mBuffers[mBufferIndex]) + mIndex + i));
    }
    std::cout << "\r\n" << termcolor::reset;
    while (size > 0) {
        size_t chunk = std::min(mBufferSizes[mBufferIndex] - mIndex, (size_t)size);
        auto w_size = fwrite(static_cast<uint8_t *>(mBuffers[mBufferIndex]) + mIndex, 1, chunk, file);
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

void GrowingBuffer::dump(std::ostream &outputStream) {
    size_t prevIndex = mPublicIndex;
    this->seek(0, SEEK_SET);
    size_t dataSize = this->size();
    std::vector<uint8_t> buffer(dataSize);
    this->read(buffer.data(), dataSize);
    outputStream << "Raw mData (" << dataSize << " bytes):" << "\r\n";
    for (size_t i = 0; i < dataSize; ++i) {
        if ( i % DUMP_PREVIEW_BYTES == 0 && i != 0 )
            outputStream << "  ";
        if (i % DUMP_LINE_BYTES == 0)
            outputStream << "\r\n" << std::hex << std::setw(DUMP_PREVIEW_BYTES) << std::setfill('0') << i << ": ";
        outputStream << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
    }
    outputStream << std::dec << "\r\n";
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

size_t GrowingBuffer::tell() const{
    return mPublicIndex;
}

int GrowingBuffer::moveBackward(int& off) {
    while (off != 0) {
        if ((ssize_t)mIndex >= -off) {
            // Can move within current buffer
            mIndex += off;
            off = 0;
        } else {
            // Need to move to previous buffer
            if (mBufferIndex == 0) {
                // Already at first buffer, clamp to start
                mIndex = 0;
                mPublicIndex = 0;
                return -1;
            }
            
            // Move to previous buffer
            off += mIndex + 1;
            mBufferIndex--;
            
            if (mBufferIndex >= mBufferSizes.size()) {
                // Safety check for buffer index underflow
                mBufferIndex = 0;
                mIndex = 0;
                mPublicIndex = 0;
                return -1;
            }
            
            // Position at end of previous buffer
            mIndex = mBufferSizes[mBufferIndex] - 1;
        }
    }
    return 0;
}

int GrowingBuffer::moveForward(int& off) {
    while (off != 0) {
        // Calculate remaining bytes in current buffer
        ssize_t remain = mBufferSizes[mBufferIndex] - mIndex;
        
        if (remain > off) {
            // Can move within current buffer
            mIndex += off;
            off = 0;
        } else {
            // Need to move to next buffer
            off -= remain;
            
            if (mBufferIndex == mBufferSizes.size() - 1) {
                // Already at last buffer, clamp to end
                mIndex = mBufferSizes[mBufferIndex];
                return 0;
            }
            
            // Move to next buffer
            mIndex = 0;
            mBufferIndex++;
            
            // Safety check for buffer index overflow
            if (mBufferIndex >= mBufferSizes.size()) {
                mBufferIndex = mBufferSizes.size() - 1;
                mIndex = mBufferSizes[mBufferIndex];
                return 0;
            }
        }
    }
    return 0;
}

int GrowingBuffer::move(int off) {
    // Handle growth for forward movement
    if (off > 0 && advise_access(off) < 0) {
        return -1; // Error: advise_access failed
    }
    
    // Update public index
    mPublicIndex += off;
    
    // Delegate to specialized functions based on direction
    if (off < 0) {
        return moveBackward(off);
    }
    
    if (off > 0) {
        return moveForward(off);
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
        if (new_buf_size > ONE_GIGABYTE) { // 1GB limit
            return -1;
        }

        void *ptr = malloc(new_buf_size);
        if (ptr == nullptr) {
            // Error: allocation failed
            return -1;
        }

        memset(ptr, 0, new_buf_size);
        mBuffers.push_back(ptr);
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