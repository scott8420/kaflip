#pragma once
// core::Toc -- a book's table of contents (s008). GTK-free (pugixml).
//
// Two sources, best first:
//   EPUB 3  the nav document (manifest properties="nav"): the <nav> whose
//           epub:type (or role="doc-toc") says "toc", its <ol> of <li>s,
//           each an <a href> (or a <span> heading with no link) plus an
//           optional nested <ol>.
//   EPUB 2  the NCX (spine toc="id", else the manifest's NCX item):
//           <navMap> of <navPoint>s -- navLabel/text + content@src, nested.
//
// The tree is kept FLAT, in document order, each entry with its depth (0 =
// top level). That is the order a list shows it in and the order "which
// entry am I in" walks; the nesting is the depth. Every href is resolved to
// a ZIP PATH (core::resolve_href against the nav/NCX file) with the fragment
// kept apart, so it meets the spine by plain string compare.
#include <string>
#include <vector>

namespace kaflip::core {

struct TocEntry {
    std::string title;      // whitespace collapsed
    std::string path;       // zip path; "" = a heading with no link
    std::string fragment;   // "" = the top of the file
    int         depth = 0;
    int         spine = -1; // spine index of `path` (filled by Book), -1 = not in the spine
};

// Pure parsers. `base` is the nav / NCX file's own zip path. Malformed XML
// or no TOC in it -> empty (a book without a TOC still reads).
std::vector<TocEntry> parse_nav(const std::string& xhtml, const std::string& base);
std::vector<TocEntry> parse_ncx(const std::string& xml, const std::string& base);

// Which entry is the reader in, at spine index `chapter`? The first entry
// that points INTO this chapter; failing that, the last entry before it
// (a chapter split over several files keeps its title); -1 = none (the
// cover, or a book with no TOC).
int toc_index_for(const std::vector<TocEntry>& toc, int chapter);

// "a  b\n c" -> "a b c" (and trimmed).
std::string collapse_ws(const std::string& s);

}  // namespace kaflip::core
