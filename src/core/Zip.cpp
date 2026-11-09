#include "core/Zip.hpp"

#include <zlib.h>

#include <fstream>
#include <sstream>

namespace kaflip::core {

namespace {
constexpr std::uint32_t kEocdSig  = 0x06054b50;
constexpr std::uint32_t kCdSig    = 0x02014b50;
constexpr std::uint32_t kLocalSig = 0x04034b50;
constexpr std::uint32_t kZip64Loc = 0x07064b50;

std::uint16_t u16(const std::string& d, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(d[at]) |
                                      (static_cast<unsigned char>(d[at + 1]) << 8));
}
std::uint32_t u32(const std::string& d, std::size_t at) {
    return static_cast<std::uint32_t>(u16(d, at)) |
           (static_cast<std::uint32_t>(u16(d, at + 2)) << 16);
}
}  // namespace

bool Zip::open(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { m_error = "cannot open '" + path + "'"; return false; }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!in && !in.eof()) { m_error = "read failed on '" + path + "'"; return false; }
    return open_bytes(std::move(ss).str());
}

bool Zip::open_bytes(std::string bytes) {
    m_data = std::move(bytes);
    m_entries.clear();
    m_index.clear();
    m_error.clear();
    return parse();
}

bool Zip::parse() {
    const std::string& d = m_data;
    // The end-of-central-directory record is 22 bytes plus a comment of up to
    // 64 KiB, so search backwards over at most that much.
    if (d.size() < 22) { m_error = "not a zip (too short)"; return false; }
    std::size_t eocd = std::string::npos;
    const std::size_t lowest = d.size() > 22 + 65535 ? d.size() - 22 - 65535 : 0;
    for (std::size_t i = d.size() - 22 + 1; i-- > lowest;) {
        if (u32(d, i) == kEocdSig) { eocd = i; break; }
    }
    if (eocd == std::string::npos) { m_error = "not a zip (no end record)"; return false; }
    if (eocd >= 20 && u32(d, eocd - 20) == kZip64Loc) {
        m_error = "zip64 archives are not supported";
        return false;
    }
    const std::uint16_t disk   = u16(d, eocd + 4);
    const std::uint16_t cddisk = u16(d, eocd + 6);
    const std::uint16_t count  = u16(d, eocd + 10);
    const std::uint32_t cdsize = u32(d, eocd + 12);
    const std::uint32_t cdoff  = u32(d, eocd + 16);
    if (disk != 0 || cddisk != 0) { m_error = "multi-disk zips are not supported"; return false; }
    if (std::size_t(cdoff) + cdsize > eocd) { m_error = "central directory out of range"; return false; }

    std::size_t p = cdoff;
    for (std::uint16_t n = 0; n < count; ++n) {
        if (p + 46 > eocd || u32(d, p) != kCdSig) {
            m_error = "bad central directory entry #" + std::to_string(n);
            return false;
        }
        ZipEntry e;
        e.flags        = u16(d, p + 8);
        e.method       = u16(d, p + 10);
        e.crc          = u32(d, p + 16);
        e.comp_size    = u32(d, p + 20);
        e.size         = u32(d, p + 24);
        const std::uint16_t nlen = u16(d, p + 28);
        const std::uint16_t xlen = u16(d, p + 30);
        const std::uint16_t clen = u16(d, p + 32);
        e.local_offset = u32(d, p + 42);
        if (p + 46 + nlen > eocd) { m_error = "entry name out of range"; return false; }
        e.name = d.substr(p + 46, nlen);
        p += 46 + std::size_t(nlen) + xlen + clen;
        if (e.name.empty() || e.name.back() == '/') continue;   // directories
        m_index[e.name] = m_entries.size();
        m_entries.push_back(std::move(e));
    }
    return true;
}

const ZipEntry* Zip::find(const std::string& name) const {
    auto it = m_index.find(name);
    return it == m_index.end() ? nullptr : &m_entries[it->second];
}

std::optional<std::string> Zip::read(const std::string& name) const {
    const ZipEntry* e = find(name);
    if (!e) { m_error = "no entry '" + name + "'"; return std::nullopt; }
    if (e->flags & 0x1) { m_error = "'" + name + "' is encrypted"; return std::nullopt; }
    const std::string& d = m_data;
    const std::size_t lh = e->local_offset;
    if (lh + 30 > d.size() || u32(d, lh) != kLocalSig) {
        m_error = "bad local header for '" + name + "'";
        return std::nullopt;
    }
    const std::size_t start = lh + 30 + u16(d, lh + 26) + u16(d, lh + 28);
    if (start + e->comp_size > d.size()) {
        m_error = "'" + name + "' runs past the end of the archive";
        return std::nullopt;
    }

    std::string out;
    if (e->method == 0) {
        if (e->comp_size != e->size) { m_error = "'" + name + "' stored size mismatch"; return std::nullopt; }
        out = d.substr(start, e->size);
    } else if (e->method == 8) {
        out.resize(e->size);
        z_stream zs{};
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) { m_error = "inflateInit failed"; return std::nullopt; }
        zs.next_in   = reinterpret_cast<Bytef*>(const_cast<char*>(d.data() + start));
        zs.avail_in  = e->comp_size;
        zs.next_out  = reinterpret_cast<Bytef*>(out.data());
        zs.avail_out = e->size;
        const int rc = inflate(&zs, Z_FINISH);
        const uLong got = zs.total_out;
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || got != e->size) {
            m_error = "'" + name + "' failed to inflate (zlib " + std::to_string(rc) + ")";
            return std::nullopt;
        }
    } else {
        m_error = "'" + name + "' uses compression method " + std::to_string(e->method);
        return std::nullopt;
    }

    const uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(out.data()),
                            static_cast<uInt>(out.size()));
    if (crc != e->crc) { m_error = "'" + name + "' failed its CRC check"; return std::nullopt; }
    return out;
}

}  // namespace kaflip::core
