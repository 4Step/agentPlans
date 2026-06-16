#pragma once
// Lightweight CSV reading helpers for agentPlans.
// All lookup inputs are plain CSV (xlsx must be pre-converted to CSV).
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ap {

// Trim a trailing '\r' (Windows CRLF written by R/fwrite on some hosts).
inline std::string_view trim_cr(std::string_view s) {
    if (!s.empty() && s.back() == '\r') s.remove_suffix(1);
    return s;
}

// Split one CSV line into fields. Handles simple double-quoted fields
// (no embedded newlines). Reuses `out` to avoid allocations.
void split_csv(std::string_view line, std::vector<std::string_view>& out);

inline long long to_ll(std::string_view s) {
    s = trim_cr(s);
    if (s.empty()) return 0;
    return std::strtoll(std::string(s).c_str(), nullptr, 10);
}

inline double to_double(std::string_view s) {
    s = trim_cr(s);
    if (s.empty()) return 0.0;
    return std::strtod(std::string(s).c_str(), nullptr);
}

// Column-name -> index map for a header row.
struct CsvHeader {
    std::vector<std::string> names;
    std::unordered_map<std::string, int> idx;
    void parse(std::string_view header_line);
    int col(const std::string& name) const {
        auto it = idx.find(name);
        return it == idx.end() ? -1 : it->second;
    }
    int require(const std::string& name, const std::string& file) const {
        int c = col(name);
        if (c < 0) throw std::runtime_error(file + ": missing column '" + name + "'");
        return c;
    }
};

// Reads an entire (plain or gz) CSV into memory as a header + rows-of-strings.
// Convenient for the modest lookup tables (shares, ToD, TAZ->DMA, skims).
struct CsvTable {
    CsvHeader header;
    std::vector<std::vector<std::string>> rows;  // [row][col]

    int col(const std::string& n) const { return header.col(n); }
    int require(const std::string& n, const std::string& f) const { return header.require(n, f); }
    size_t size() const { return rows.size(); }

    const std::string& at(size_t r, int c) const { return rows[r][c]; }
    double num(size_t r, int c) const { return c < 0 ? 0.0 : to_double(rows[r][c]); }
    long long ll(size_t r, int c) const { return c < 0 ? 0 : to_ll(rows[r][c]); }
};

// Load a CSV (auto-detects .gz by magic bytes) fully into a CsvTable.
CsvTable load_csv(const std::string& path);

} // namespace ap
