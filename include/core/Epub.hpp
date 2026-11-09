#pragma once
// core::Epub -- an EPUB opened for reading. GTK-free (pugixml + core::Zip).
//
//   META-INF/container.xml  -> the OPF's path (first rootfile)
//   OPF <metadata>          -> title, creators, language, identifier
//   OPF <manifest>          -> id -> {zip path, media type, properties}
//   OPF <spine>             -> reading order (itemrefs, linear or not)
//
// Every path the Book hands out is a ZIP PATH: resolved against the OPF's
// folder, percent-decoded, "." and ".." folded, no fragment. That is the one
// form the zip, the URI scheme and the spine all speak, so a link from a
// chapter maps back to its spine index by plain string compare.
//
// The NCX / nav TOC is NOT read yet (s006).
#include "core/Zip.hpp"

#include <optional>
#include <string>
#include <vector>

namespace kaflip::core {

struct ManifestItem {
    std::string id;
    std::string path;         // zip path
    std::string media_type;
    std::string properties;   // e.g. "nav", "cover-image"
};

struct SpineItem {
    std::string idref;
    std::string path;         // zip path of the manifest item
    std::string media_type;
    bool        linear = true;
};

struct BookMeta {
    std::string              title;
    std::vector<std::string> creators;
    std::string              language;
    std::string              identifier;
};

class Book {
public:
    bool open(const std::string& file);         // from disk
    bool open_bytes(std::string bytes);         // from memory (selftest)

    const std::string& error() const { return m_error; }
    const std::string& file() const { return m_file; }

    const BookMeta&                  meta() const { return m_meta; }
    const std::string&               opf_path() const { return m_opf; }
    const std::vector<ManifestItem>& manifest() const { return m_manifest; }
    const std::vector<SpineItem>&    spine() const { return m_spine; }

    // s006: the cover image's zip path, or "". EPUB 3 properties="cover-image",
    // then EPUB 2 <meta name="cover" content="id">, then an image item whose
    // id or file name says "cover".
    const std::string& cover_path() const { return m_cover; }

    // Spine index of a zip path (fragment ignored), or -1.
    int spine_index_of(const std::string& path) const;

    // Bytes + media type for one zip path (the URI scheme's whole job).
    std::optional<std::string> read(const std::string& path) const;
    std::string media_type_of(const std::string& path) const;

private:
    bool load();

    Zip                       m_zip;
    std::string               m_file;
    std::string               m_opf;
    std::string               m_cover;   // s006
    BookMeta                  m_meta;
    std::vector<ManifestItem> m_manifest;
    std::vector<SpineItem>    m_spine;
    mutable std::string       m_error;
};

// ── Path helpers (pure; the selftest pins them) ────────────────────────────
// "%20" -> " ". Malformed escapes are left as they are.
std::string percent_decode(const std::string& s);
// Encode everything outside RFC 3986 unreserved + '/'.
std::string percent_encode_path(const std::string& s);
// Fold "." and ".." segments and duplicate slashes; never climbs above root.
std::string normalize_path(const std::string& p);
// The folder part of a zip path, with a trailing slash ("" at the root).
std::string dir_of(const std::string& path);
// Resolve an href found in a file at `base_file` to a zip path. Strips the
// fragment and query, percent-decodes. Absolute (scheme:) hrefs -> "".
std::string resolve_href(const std::string& base_file, const std::string& href);
// A media type guessed from the extension (the fallback when the manifest
// does not list a file).
std::string media_type_for_ext(const std::string& path);

// ── The kaflip-book: URI scheme ────────────────────────────────────────────
// kaflip-book://b<token>/<encoded zip path>
// The token changes with every opened book so WebKit's cache can never hand
// one book's chapter to another.
inline constexpr const char* kBookScheme = "kaflip-book";
std::string book_uri(unsigned token, const std::string& zip_path);
struct BookUri { unsigned token = 0; std::string path; };   // path decoded, no fragment
std::optional<BookUri> parse_book_uri(const std::string& uri);

}  // namespace kaflip::core
