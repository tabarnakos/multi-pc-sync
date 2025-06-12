// Section 1: Compilation Guards
#ifndef _GROWING_BUFFER_H_
#define _GROWING_BUFFER_H_

// Section 2: Includes
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <vector>

// Section 3: Defines and Macros
// (none)

// Section 4: Classes
/**
 * A dynamic buffer that grows as needed to accommodate data.
 * Provides read/write operations and automatic memory management.
 */
class GrowingBuffer {
public:
    /**
     * Default constructor
     * Initializes an empty growing buffer
     */
    GrowingBuffer();

    /**
     * Virtual destructor to cleanup allocated memory
     */
    virtual ~GrowingBuffer();

    /**
     * Reads data from the buffer into the provided memory location
     * @param buf Pointer to the destination buffer
     * @param size Number of bytes to read
     * @return Number of bytes actually read
     */
    size_t read(void *buf, const size_t size);

    /**
     * Writes data from the provided buffer into this growing buffer
     * @param buf Pointer to the source buffer
     * @param size Number of bytes to write
     * @return Number of bytes written
     */
    size_t write(const void *buf, const size_t size);

    /**
     * Template method to write a value of any type
     * @param val The value to write
     */
    template <class T>
    void write(const T &val) { this->write(&val, sizeof(T)); }

    /**
     * Dumps buffer contents to a file
     * @param fd File descriptor to write to
     * @param size Number of bytes to write
     */
    void dumpToFile(FILE *fd, uintmax_t size);

    /**
     * Dumps buffer contents to an output stream
     * @param os The output stream to write to
     */
    void dump(std::ostream &os);

    /**
     * Seeks to a position in the buffer
     * @param off Offset from whence
     * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
     * @return 0 on success, negative value on error
     */
    int seek(int off, int whence);

    /**
     * Gets current position in the buffer
     * @return Current position
     */
    size_t tell() const;

    /**
     * Moves the current position by the specified offset
     * @param off Number of bytes to move (positive or negative)
     * @return 0 on success, negative value on error
     */
    int move(int off);

    /**
     * Gets the total size of the buffer
     * @return Size in bytes
     */
    size_t size() const { return mSize; }

    /**
     * Array access operator to read a value of type T at the given index
     * @param idx Index to read from
     * @return Value at the specified index
     */
    template <class T>
    T operator[](size_t idx) {
        T val;
        this->seek(idx, SEEK_SET);
        this->read(&val, sizeof(T));
        this->move(-(int)sizeof(T)); // Restore original position
        return val;
    }

    /**
     * Address-of operator overload
     * @param other Reference to obtain address of
     * @return Pointer to the referenced value
     */
    template <class T*>
    T* operator&(T &other) { return &other; }

protected:
    // (none)

private:
    int advise_access(size_t size);
    size_t mSize = 0;
    std::vector<void *> mBuffers;
    std::vector<size_t> mBufferSizes;
    size_t mBufferIndex = 0;
    size_t mIndex = 0;
    size_t mPublicIndex = 0;
};

#endif // _GROWING_BUFFER_H_