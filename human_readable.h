// *****************************************************************************
// Human Readable Utility
// *****************************************************************************

#ifndef _HUMAN_READABLE_H_
#define _HUMAN_READABLE_H_

// Section 1: Includes
// C++ Standard Library
#include <iostream>

// Section 2: Class Definition
// Utility class to format byte sizes in human-readable format
class HumanReadable {
public:
    size_t size;

    explicit HumanReadable(size_t bytes) : size(bytes) {}

    friend std::ostream& operator<<(std::ostream& os, const HumanReadable& hr) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        size_t unitIndex = 0;
        double size = hr.size;

        while (size >= 1024 && unitIndex < 4) {
            size /= 1024;
            unitIndex++;
        }

        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unitIndex]);
        return os << buffer;
    }
};

#endif // _HUMAN_READABLE_H_
