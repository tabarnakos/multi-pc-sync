#include "growing_buffer.h"
#include <iostream>
#include <iomanip>

GrowingBuffer::GrowingBuffer() 
{

}
GrowingBuffer::~GrowingBuffer()
{
    for ( auto buf : mBuffers )
        free(buf);
}
    
size_t GrowingBuffer::read(void * buf, const size_t size)
{
    advise_access(size);

    size_t bytes_left = std::min(size, mSize - mPublicIndex);

    while ( bytes_left > 0 )
    {
        size_t readlen = mBufferSizes[mBufferIndex] - mIndex < bytes_left ? mBufferSizes[mBufferIndex] - mIndex : bytes_left;
        memcpy(buf, &((uint8_t *)mBuffers[mBufferIndex])[mIndex], readlen);

        move( readlen );
        bytes_left -= readlen;
        buf = (uint8_t *)buf + readlen;
    }
    return size - bytes_left;
}
    
void GrowingBuffer::write(const void * buf, const size_t size)
{
    advise_access(size);

    size_t bytes_left = size;

    while ( bytes_left > 0 )
    {
        size_t writelen = mBufferSizes[mBufferIndex] - mIndex < size ? mBufferSizes[mBufferIndex] - mIndex : size;
        memcpy((uint8_t *)(mBuffers[mBufferIndex])+mIndex, buf, writelen);

        move( writelen );
        bytes_left -= writelen;
        buf = (uint8_t *)buf + writelen;
    }
}
    
void GrowingBuffer::dumpToFile( FILE * fd, uintmax_t size )
{
    std::cout << "dump " << size << " bytes to file" << std::endl;
    std::cout << "mBufferIndex = " << mBufferIndex << std::endl;
    std::cout << "mBufferSizes[mBufferIndex] = " << mBufferSizes[mBufferIndex] << std::endl;
    std::cout << "mIndex = " << mIndex << std::endl;
    std::cout << "data = ";
    for ( int i = 0; i < 8; ++i )
    {
        printf("%02x ",*(static_cast<uint8_t*>(mBuffers[mBufferIndex]) + mIndex + i));
    }
    std::cout << std::endl;
    while ( size > 0)
    {
        auto w_size = fwrite( static_cast<uint8_t*>(mBuffers[mBufferIndex]) + mIndex, 1, (mBufferSizes[mBufferIndex]-mIndex < size) ? mBufferSizes[mBufferIndex]-mIndex : size, fd );
        mIndex = mIndex + w_size;
        if ( mIndex == mBufferSizes[mBufferIndex] )
        {
            mIndex = 0;
            if ( ++mBufferIndex == mBuffers.size() )
                mBufferIndex = 0;
        }
        size -= w_size;
    }
}
    
void GrowingBuffer::seek( int off, int whence )
{
    switch ( whence )
    {
        case SEEK_CUR:
            move(off);
            break;

        case SEEK_END:
            move(off+mSize-mPublicIndex);
            break;

        case SEEK_SET:
            move(off-mPublicIndex);
    }

}

size_t GrowingBuffer::tell()
{
    return mPublicIndex;
}

void GrowingBuffer::dump(std::ostream& os)
{
    this->seek(0, SEEK_SET);
    size_t dataSize = this->size();
    std::vector<uint8_t> buffer(dataSize);
    this->read(buffer.data(), dataSize);

    os << "Raw mData (" << dataSize << " bytes):" << std::endl;
    for (size_t i = 0; i < dataSize; ++i) {
        if (i % 16 == 0) os << std::endl << std::hex << std::setw(8) << std::setfill('0') << i << ": ";
        os << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
    }
    os << std::dec << std::endl;
}
    
void GrowingBuffer::move( int off )
{
    if ( off > 0 )
        advise_access(off);

    mPublicIndex += off;

    if (off < 0)
    {
        while ( off != 0 )
        {
            if ( mIndex >= -off )
            {
                mIndex+=off;
                off = 0;
            }
            else
            {
                off+=mIndex+1;
                mBufferIndex--;
                mIndex=mBufferSizes[mBufferIndex]-1;
            }
        }
    } else 
    {
        while ( off != 0 )
        {
            if ( mBufferSizes[mBufferIndex] - mIndex > off )
            {
                mIndex+=off;
                off = 0;
            }
            else 
            {
                off -= mBufferSizes[mBufferIndex] - mIndex;
                mIndex = 0;
                mBufferIndex++;
            }
        }
    }
}
    
void GrowingBuffer::advise_access(size_t size)
{
    if (mPublicIndex+size > mSize)
    {
        void * p = malloc(mPublicIndex-mSize+size);
        memset(p,0,mPublicIndex-mSize+size);
        mBuffers.push_back(p);
        mBufferSizes.push_back(mPublicIndex-mSize+size);
        mSize+=size;
    }
}