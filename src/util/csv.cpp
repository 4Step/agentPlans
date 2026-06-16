#include "util/csv.h"

#include "util/gz_io.h"

namespace ap {

void split_csv(std::string_view line, std::vector<std::string_view>& out) {
    out.clear();
    size_t i = 0, n = line.size();
    while (i <= n) {
        if (i < n && line[i] == '"') {
            // Quoted field: scan to the closing quote (no embedded-quote escapes
            // expected in these numeric/lookup tables).
            size_t start = ++i;
            while (i < n && line[i] != '"') ++i;
            out.emplace_back(line.substr(start, i - start));
            // advance past closing quote and the following comma (if any)
            if (i < n) ++i;            // closing quote
            if (i < n && line[i] == ',') ++i;
            if (i >= n) break;
        } else {
            size_t start = i;
            while (i < n && line[i] != ',') ++i;
            out.emplace_back(line.substr(start, i - start));
            if (i >= n) break;
            ++i;  // skip comma
            if (i == n) { out.emplace_back(line.substr(n, 0)); break; }  // trailing comma
        }
    }
}

void CsvHeader::parse(std::string_view header_line) {
    names.clear();
    idx.clear();
    std::vector<std::string_view> f;
    split_csv(trim_cr(header_line), f);
    for (int c = 0; c < static_cast<int>(f.size()); ++c) {
        std::string name(trim_cr(f[c]));
        names.push_back(name);
        idx[name] = c;
    }
}

CsvTable load_csv(const std::string& path) {
    GzLineReader rd(path);
    if (!rd.good()) throw std::runtime_error("cannot open " + path);
    CsvTable t;
    std::string_view line;
    if (!rd.next_line(line)) throw std::runtime_error(path + ": empty");
    t.header.parse(line);
    int ncol = static_cast<int>(t.header.names.size());
    std::vector<std::string_view> f;
    while (rd.next_line(line)) {
        if (line.empty()) continue;
        split_csv(line, f);
        std::vector<std::string> row;
        row.reserve(ncol);
        for (auto& sv : f) row.emplace_back(trim_cr(sv));
        row.resize(ncol);  // pad short rows (trailing empties)
        t.rows.push_back(std::move(row));
    }
    return t;
}

} // namespace ap
