#include "diag_log.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>

namespace {

std::mutex            g_mtx;
std::deque<std::string> g_lines;
const size_t          kMax = 800;

const std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

} // namespace

void DiagLog::add(const std::string& line) {
    if (line.empty()) return;

    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - g_start).count();

    char pre[32];
    snprintf(pre, sizeof(pre), "%8.1f  ", ms);

    std::lock_guard<std::mutex> lk(g_mtx);
    g_lines.push_back(std::string(pre) + line);
    while (g_lines.size() > kMax) g_lines.pop_front();
}

std::vector<std::string> DiagLog::snapshot(size_t maxLines) {
    std::lock_guard<std::mutex> lk(g_mtx);

    const size_t n = g_lines.size();
    const size_t start = (n > maxLines) ? n - maxLines : 0;

    std::vector<std::string> out;
    out.reserve(n - start);
    for (size_t i = start; i < n; ++i) out.push_back(g_lines[i]);
    return out;
}

void DiagLog::clear() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_lines.clear();
}

size_t DiagLog::count() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_lines.size();
}
