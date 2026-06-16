#include "util/gz_io.h"

#include <cstring>

#include "miniz.h"

namespace ap {

namespace {
constexpr size_t IN_CHUNK = 1u << 20;   // 1 MB compressed input
constexpr size_t OUT_CHUNK = 4u << 20;  // 4 MB decode/encode step
}

// ---------------------------------------------------------------- reader
GzLineReader::GzLineReader(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) return;
    in_buf_.resize(IN_CHUNK);
    uint8_t magic[2] = {0, 0};
    size_t got = std::fread(magic, 1, 2, file_);
    std::fseek(file_, 0, SEEK_SET);
    gzip_ = (got == 2 && magic[0] == 0x1f && magic[1] == 0x8b);
    if (gzip_) {
        if (!parse_gzip_member_header()) { std::fclose(file_); file_ = nullptr; return; }
        auto* s = new mz_stream();
        std::memset(s, 0, sizeof(mz_stream));
        if (mz_inflateInit2(s, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            delete s; std::fclose(file_); file_ = nullptr; return;
        }
        strm_ = s;
    }
}

GzLineReader::~GzLineReader() {
    if (strm_) {
        mz_inflateEnd(static_cast<mz_stream*>(strm_));
        delete static_cast<mz_stream*>(strm_);
    }
    if (file_) std::fclose(file_);
}

bool GzLineReader::refill_input() {
    if (input_eof_) return false;
    in_len_ = std::fread(in_buf_.data(), 1, in_buf_.size(), file_);
    in_pos_ = 0;
    if (in_len_ == 0) { input_eof_ = true; return false; }
    return true;
}

bool GzLineReader::read_input_byte(uint8_t& b) {
    if (in_pos_ >= in_len_ && !refill_input()) return false;
    b = in_buf_[in_pos_++];
    return true;
}

bool GzLineReader::parse_gzip_member_header() {
    uint8_t h[10];
    for (auto& byte : h)
        if (!read_input_byte(byte)) return false;
    if (h[0] != 0x1f || h[1] != 0x8b || h[2] != 8) return false;
    uint8_t flg = h[3], b = 0;
    if (flg & 0x04) {
        uint8_t l0, l1;
        if (!read_input_byte(l0) || !read_input_byte(l1)) return false;
        size_t xlen = l0 | (static_cast<size_t>(l1) << 8);
        for (size_t i = 0; i < xlen; ++i)
            if (!read_input_byte(b)) return false;
    }
    if (flg & 0x08) { do { if (!read_input_byte(b)) return false; } while (b); }
    if (flg & 0x10) { do { if (!read_input_byte(b)) return false; } while (b); }
    if (flg & 0x02) { if (!read_input_byte(b) || !read_input_byte(b)) return false; }
    return true;
}

bool GzLineReader::skip_member_trailer() {
    uint8_t b;
    for (int i = 0; i < 8; ++i)
        if (!read_input_byte(b)) return false;
    if (in_pos_ >= in_len_ && !refill_input()) return false;
    if (!parse_gzip_member_header()) return false;
    mz_inflateEnd(static_cast<mz_stream*>(strm_));
    std::memset(strm_, 0, sizeof(mz_stream));
    return mz_inflateInit2(static_cast<mz_stream*>(strm_), -MZ_DEFAULT_WINDOW_BITS) == MZ_OK;
}

bool GzLineReader::decompress_more() {
    if (decode_done_) return false;
    if (!gzip_) {
        size_t off = text_.size();
        text_.resize(off + OUT_CHUNK);
        size_t got = std::fread(&text_[off], 1, OUT_CHUNK, file_);
        text_.resize(off + got);
        if (got == 0) { decode_done_ = true; return false; }
        return true;
    }
    auto* s = static_cast<mz_stream*>(strm_);
    size_t off = text_.size();
    text_.resize(off + OUT_CHUNK);
    size_t produced = 0;
    while (produced < OUT_CHUNK) {
        if (in_pos_ >= in_len_ && !refill_input()) { decode_done_ = true; break; }
        s->next_in = in_buf_.data() + in_pos_;
        s->avail_in = static_cast<unsigned>(in_len_ - in_pos_);
        s->next_out = reinterpret_cast<unsigned char*>(&text_[off + produced]);
        s->avail_out = static_cast<unsigned>(OUT_CHUNK - produced);
        int rc = mz_inflate(s, MZ_SYNC_FLUSH);
        size_t consumed = (in_len_ - in_pos_) - s->avail_in;
        in_pos_ += consumed;
        produced = OUT_CHUNK - s->avail_out;
        if (rc == MZ_STREAM_END) {
            if (!skip_member_trailer()) { decode_done_ = true; break; }
            continue;
        }
        if (rc != MZ_OK) { decode_done_ = true; break; }
        if (produced == OUT_CHUNK) break;
    }
    text_.resize(off + produced);
    return produced > 0;
}

bool GzLineReader::next_line(std::string_view& line) {
    while (true) {
        size_t nl = text_.find('\n', text_pos_);
        if (nl != std::string::npos) {
            size_t len = nl - text_pos_;
            if (len > 0 && text_[text_pos_ + len - 1] == '\r') --len;
            line = std::string_view(text_.data() + text_pos_, len);
            text_pos_ = nl + 1;
            return true;
        }
        text_.erase(0, text_pos_);
        text_pos_ = 0;
        if (!decompress_more()) {
            if (!text_.empty()) {
                size_t len = text_.size();
                if (text_[len - 1] == '\r') --len;
                line = std::string_view(text_.data(), len);
                text_pos_ = text_.size();
                return true;
            }
            return false;
        }
    }
}

// ---------------------------------------------------------------- writer
GzWriter::GzWriter(const std::string& path, int level) {
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return;
    out_buf_.resize(OUT_CHUNK);
    crc_ = mz_crc32(MZ_CRC32_INIT, nullptr, 0);
    // 10-byte gzip header: magic, deflate, no flags, mtime=0, xfl=0, OS=255.
    const uint8_t hdr[10] = {0x1f, 0x8b, 0x08, 0x00, 0, 0, 0, 0, 0, 0xff};
    std::fwrite(hdr, 1, 10, file_);
    auto* s = new mz_stream();
    std::memset(s, 0, sizeof(mz_stream));
    if (mz_deflateInit2(s, level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9,
                        MZ_DEFAULT_STRATEGY) != MZ_OK) {
        delete s; std::fclose(file_); file_ = nullptr; return;
    }
    strm_ = s;
}

GzWriter::~GzWriter() {
    finish();
    if (strm_) {
        mz_deflateEnd(static_cast<mz_stream*>(strm_));
        delete static_cast<mz_stream*>(strm_);
        strm_ = nullptr;
    }
    if (file_) { std::fclose(file_); file_ = nullptr; }
}

void GzWriter::deflate_block(const uint8_t* data, size_t len, bool finish) {
    auto* s = static_cast<mz_stream*>(strm_);
    s->next_in = data;
    s->avail_in = static_cast<unsigned>(len);
    int flush = finish ? MZ_FINISH : MZ_NO_FLUSH;
    do {
        s->next_out = out_buf_.data();
        s->avail_out = static_cast<unsigned>(out_buf_.size());
        int rc = mz_deflate(s, flush);
        size_t have = out_buf_.size() - s->avail_out;
        if (have) std::fwrite(out_buf_.data(), 1, have, file_);
        if (rc == MZ_STREAM_END) break;
        if (rc != MZ_OK && rc != MZ_BUF_ERROR) break;
    } while (s->avail_in > 0 || finish);
}

void GzWriter::write(std::string_view bytes) {
    if (!file_ || finished_ || bytes.empty()) return;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
    crc_ = mz_crc32(crc_, p, bytes.size());
    isize_ += static_cast<uint32_t>(bytes.size());
    deflate_block(p, bytes.size(), false);
}

void GzWriter::finish() {
    if (!file_ || finished_) return;
    finished_ = true;
    deflate_block(nullptr, 0, true);
    // gzip trailer: CRC32 then ISIZE, both little-endian.
    uint8_t tr[8];
    uint32_t c = static_cast<uint32_t>(crc_);
    tr[0] = c & 0xff; tr[1] = (c >> 8) & 0xff; tr[2] = (c >> 16) & 0xff; tr[3] = (c >> 24) & 0xff;
    tr[4] = isize_ & 0xff; tr[5] = (isize_ >> 8) & 0xff;
    tr[6] = (isize_ >> 16) & 0xff; tr[7] = (isize_ >> 24) & 0xff;
    std::fwrite(tr, 1, 8, file_);
    std::fflush(file_);
}

} // namespace ap
