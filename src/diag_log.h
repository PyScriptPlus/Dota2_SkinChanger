#pragma once

#include <cstddef>
#include <string>
#include <vector>


class DiagLog {
public:
    static void add(const std::string& line);
    static std::vector<std::string> snapshot(size_t maxLines = 400);
    static void clear();
    static size_t count();
};
