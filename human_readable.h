// *****************************************************************************
// Human Readable Utility
// *****************************************************************************

#ifndef _HUMAN_READABLE_H_
#define _HUMAN_READABLE_H_

// Section 1: Includes
// C++ Standard Library
#include <cstdint>
#include <iostream>
#include <sstream>
#include <iomanip>

constexpr size_t ONE_KILOBYTE = 1 << 10; // 1024 bytes

// Section 2: Class Definition
// Utility class to format byte sizes in human-readable format
class HumanReadable {
public:
    uintmax_t size;

    explicit HumanReadable(uintmax_t bytes) : size(bytes) {}

    friend std::ostream& operator<<(std::ostream& outputStream, const HumanReadable& humanReadable) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        size_t unitIndex = 0;
        double size = humanReadable.size;

        while (size >= ONE_KILOBYTE && unitIndex < 4) {
            size /= ONE_KILOBYTE;
            unitIndex++;
        }
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(unitIndex == 0 ? 0 : 2) << size << " " << units[unitIndex];
        return outputStream << oss.str();
    }
};

#endif // _HUMAN_READABLE_H_
