#ifndef _GROWING_BUFFER_H_
#define _GROWING_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>


class GrowingBuffer 
{
public:
	GrowingBuffer();
	virtual ~GrowingBuffer();

	size_t read(void * buf, const size_t size);

	void write(const void * buf, const size_t size);

	template <class T>
	void write(const T &val)
	{
		this->write(&val, sizeof(T));
	}

	void dumpToFile( FILE * fd, uintmax_t size );

	void seek( int off, int whence );
	size_t tell();

	void move( int off );

	size_t size() const { return mSize; }

	template<class T>
	T operator[](size_t idx)
	{
		idx = idx * sizeof(T);
		T val;
		this->seek(idx,SEEK_SET);
		this->read(&val, sizeof(T));
		return val;
	}

	template <class T*>
	T* operator&(T &other)
	{
		return &other;
	}


private:
	void advise_access(size_t size);

	size_t mSize  = 0;
	std::vector<void *> mBuffers;
	std::vector<size_t> mBufferSizes;

	size_t mBufferIndex = 0;
	size_t mIndex = 0;
	size_t mPublicIndex = 0;

};

#endif //_GROWING_BUFFER_H_