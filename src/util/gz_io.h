#pragma once
// gzip read/write for agentPlans, built on vendored miniz (raw deflate +
// hand-rolled gzip member framing). The reader is adapted from AgentFlow/Hydra
// so agentPlans and Hydra speak the same compressed trip-list format.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace ap {

// Streams lines out of a gzip (or plain) text file.
class GzLineReader {
public:
    explicit GzLineReader(const std::string& path);
    ~GzLineReader();
    GzLineReader(const GzLineReader&) = delete;
    GzLineReader& operator=(const GzLineReader&) = delete;

    bool good() const { return file_ != nullptr; }
    bool next_line(std::string_view& line);

private:
    bool decompress_more();
    bool refill_input();
    bool read_input_byte(uint8_t& b);
    bool parse_gzip_member_header();
    bool skip_member_trailer();

    FILE* file_ = nullptr;
    bool gzip_ = false, input_eof_ = false, decode_done_ = false;
    void* strm_ = nullptr;
    std::vector<uint8_t> in_buf_;
    size_t in_pos_ = 0, in_len_ = 0;
    std::string text_;
    size_t text_pos_ = 0;
};

// Writes a gzip file. Appends bytes via write()/line(); call finish() (or let
// the destructor do it) to flush the deflate stream and the gzip trailer.
class GzWriter {
public:
    explicit GzWriter(const std::string& path, int level = 6);
    ~GzWriter();
    GzWriter(const GzWriter&) = delete;
    GzWriter& operator=(const GzWriter&) = delete;

    bool good() const { return file_ != nullptr; }
    void write(std::string_view bytes);
    void line(std::string_view s) { write(s); write("\n"); }
    void finish();

private:
    void deflate_block(const uint8_t* data, size_t len, bool finish);

    FILE* file_ = nullptr;
    void* strm_ = nullptr;          // mz_stream*
    unsigned long crc_ = 0;         // gzip CRC32 of uncompressed data
    uint32_t isize_ = 0;            // uncompressed size mod 2^32
    bool finished_ = false;
    std::vector<uint8_t> out_buf_;
};

} // namespace ap
