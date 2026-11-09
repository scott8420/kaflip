#pragma once
// core::Zip -- a small, read-only zip reader. GTK-free; the only dependency is
// zlib (raw inflate). Enough for EPUB and no more:
//
//   * methods 0 (stored) and 8 (deflate). Anything else is reported, not read.
//   * the central directory is the truth (local headers are only consulted for
//     their variable-length name/extra fields, which may differ from the CD).
//   * CRC-32 is checked on every read: a corrupt entry is an error, never
//     garbage handed to the renderer.
//   * no zip64, no encryption, no multi-disk. Each is detected and reported.
//
// The whole archive is read into memory at open(). EPUBs are small and that
// makes every later read a pointer walk with no file handle to share between
// threads. (WEAKNESSES: a 300 MB comic EPUB would be all in RAM.)
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kaflip::core {

struct ZipEntry {
    std::string   name;              // as stored (forward slashes)
    std::uint16_t method = 0;        // 0 stored, 8 deflate
    std::uint16_t flags = 0;         // bit 0 = encrypted
    std::uint32_t crc = 0;
    std::uint32_t comp_size = 0;
    std::uint32_t size = 0;          // uncompressed
    std::uint32_t local_offset = 0;  // of the local file header
};

class Zip {
public:
    // Open from a file on disk. False + error() on failure.
    bool open(const std::string& path);
    // Open from bytes already in memory (the selftest builds archives this way).
    bool open_bytes(std::string bytes);

    const std::string& error() const { return m_error; }

    // Entries in central-directory order.
    const std::vector<ZipEntry>& entries() const { return m_entries; }
    const ZipEntry* find(const std::string& name) const;
    bool has(const std::string& name) const { return find(name) != nullptr; }

    // The uncompressed bytes of one entry, or nullopt + error().
    std::optional<std::string> read(const std::string& name) const;

private:
    bool parse();

    std::string                         m_data;
    std::vector<ZipEntry>               m_entries;
    std::map<std::string, std::size_t>  m_index;   // name -> m_entries slot
    mutable std::string                 m_error;
};

}  // namespace kaflip::core
