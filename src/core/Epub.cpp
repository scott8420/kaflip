#include "core/Epub.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace kaflip::core {

// ─────────────────────────────────────────────────────────────────────────────
// Path helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// OPF files use prefixes freely ("opf:item", "dc:title"); compare local names.
const char* local(const char* name) {
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}
bool is(const pugi::xml_node& n, const char* want) {
    return n.type() == pugi::node_element && std::strcmp(local(n.name()), want) == 0;
}
pugi::xml_node child(const pugi::xml_node& parent, const char* want) {
    for (auto c : parent.children())
        if (is(c, want)) return c;
    return {};
}
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
// The text of an element including nested inline children.
std::string text_of(const pugi::xml_node& n) {
    std::string out;
    for (auto t : n.select_nodes(".//text()")) out += t.node().value();
    return trim(out);
}
}  // namespace

std::string percent_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int h = hexval(s[i + 1]), l = hexval(s[i + 2]);
            if (h >= 0 && l >= 0) { out += char(h * 16 + l); i += 2; continue; }
        }
        out += s[i];
    }
    return out;
}

std::string percent_encode_path(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string normalize_path(const std::string& p) {
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i <= p.size()) {
        std::size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        const std::string seg = p.substr(i, j - i);
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        i = j + 1;
    }
    std::string out;
    for (const auto& s : parts) {
        if (!out.empty()) out += '/';
        out += s;
    }
    return out;
}

std::string dir_of(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

std::string resolve_href(const std::string& base_file, const std::string& href) {
    std::string h = href;
    if (const auto cut = h.find_first_of("#?"); cut != std::string::npos) h.resize(cut);
    // A scheme ("http:", "mailto:") means it is not inside the book.
    const auto colon = h.find(':');
    if (colon != std::string::npos && h.find('/') > colon) return {};
    h = percent_decode(h);
    if (h.empty()) return normalize_path(base_file);   // "#frag" -> the same file
    if (h.front() == '/') return normalize_path(h);
    return normalize_path(dir_of(base_file) + h);
}

std::string media_type_for_ext(const std::string& path) {
    const auto dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : lower(path.substr(dot + 1));
    if (ext == "xhtml" || ext == "xht") return "application/xhtml+xml";
    if (ext == "html" || ext == "htm")  return "text/html";
    if (ext == "css")                   return "text/css";
    if (ext == "jpg" || ext == "jpeg")  return "image/jpeg";
    if (ext == "png")                   return "image/png";
    if (ext == "gif")                   return "image/gif";
    if (ext == "svg")                   return "image/svg+xml";
    if (ext == "webp")                  return "image/webp";
    if (ext == "ttf")                   return "font/ttf";
    if (ext == "otf")                   return "font/otf";
    if (ext == "woff")                  return "font/woff";
    if (ext == "woff2")                 return "font/woff2";
    if (ext == "js")                    return "text/javascript";
    if (ext == "ncx")                   return "application/x-dtbncx+xml";
    if (ext == "opf")                   return "application/oebps-package+xml";
    if (ext == "xml")                   return "application/xml";
    return "application/octet-stream";
}

std::string book_uri(unsigned token, const std::string& zip_path) {
    return std::string(kBookScheme) + "://b" + std::to_string(token) + "/" +
           percent_encode_path(zip_path);
}

std::optional<BookUri> parse_book_uri(const std::string& uri) {
    const std::string prefix = std::string(kBookScheme) + "://b";
    if (uri.rfind(prefix, 0) != 0) return std::nullopt;
    std::size_t i = prefix.size();
    std::size_t j = i;
    while (j < uri.size() && std::isdigit(static_cast<unsigned char>(uri[j]))) ++j;
    if (j == i || j >= uri.size() || uri[j] != '/') return std::nullopt;
    BookUri out;
    try { out.token = static_cast<unsigned>(std::stoul(uri.substr(i, j - i))); }
    catch (...) { return std::nullopt; }
    std::string rest = uri.substr(j + 1);
    if (const auto cut = rest.find_first_of("#?"); cut != std::string::npos) rest.resize(cut);
    out.path = normalize_path(percent_decode(rest));
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Book
// ─────────────────────────────────────────────────────────────────────────────
bool Book::open(const std::string& file) {
    m_file = file;
    if (!m_zip.open(file)) { m_error = m_zip.error(); return false; }
    return load();
}

bool Book::open_bytes(std::string bytes) {
    m_file.clear();
    if (!m_zip.open_bytes(std::move(bytes))) { m_error = m_zip.error(); return false; }
    return load();
}

bool Book::load() {
    m_opf.clear();
    m_cover.clear();
    m_meta = {};
    m_manifest.clear();
    m_spine.clear();
    m_toc.clear();
    m_toc_source.clear();
    m_error.clear();

    // 1. container.xml -> the OPF.
    const auto container = m_zip.read("META-INF/container.xml");
    if (!container) { m_error = "not an EPUB: " + m_zip.error(); return false; }
    pugi::xml_document cdoc;
    if (!cdoc.load_buffer(container->data(), container->size())) {
        m_error = "META-INF/container.xml is not well-formed XML";
        return false;
    }
    if (auto rfs = child(child(cdoc, "container"), "rootfiles")) {
        for (auto rf : rfs.children()) {
            if (!is(rf, "rootfile")) continue;
            const std::string mt = rf.attribute("media-type").value();
            if (!mt.empty() && mt != "application/oebps-package+xml") continue;
            m_opf = normalize_path(percent_decode(rf.attribute("full-path").value()));
            break;
        }
    }
    if (m_opf.empty()) { m_error = "container.xml names no OPF rootfile"; return false; }

    // 2. The OPF.
    const auto opf = m_zip.read(m_opf);
    if (!opf) { m_error = "OPF missing: " + m_zip.error(); return false; }
    pugi::xml_document odoc;
    if (const auto r = odoc.load_buffer(opf->data(), opf->size()); !r) {
        m_error = m_opf + " is not well-formed XML (" + r.description() + ")";
        return false;
    }
    const auto pkg = child(odoc, "package");
    if (!pkg) { m_error = m_opf + " has no <package>"; return false; }

    std::string cover_id;   // s006: EPUB 2's <meta name="cover" content="id">
    if (const auto md = child(pkg, "metadata")) {
        for (auto n : md.children()) {
            if (is(n, "meta") && std::string(n.attribute("name").value()) == "cover")
                cover_id = n.attribute("content").value();
            if (is(n, "title") && m_meta.title.empty())            m_meta.title = text_of(n);
            else if (is(n, "creator"))                              m_meta.creators.push_back(text_of(n));
            else if (is(n, "language") && m_meta.language.empty()) m_meta.language = text_of(n);
            else if (is(n, "identifier") && m_meta.identifier.empty()) m_meta.identifier = text_of(n);
        }
    }

    if (const auto mf = child(pkg, "manifest")) {
        for (auto n : mf.children()) {
            if (!is(n, "item")) continue;
            ManifestItem it;
            it.id         = n.attribute("id").value();
            it.path       = resolve_href(m_opf, n.attribute("href").value());
            it.media_type = n.attribute("media-type").value();
            it.properties = n.attribute("properties").value();
            if (!it.id.empty() && !it.path.empty()) m_manifest.push_back(std::move(it));
        }
    }

    std::string ncx_id;   // s008: <spine toc="ncx">
    if (const auto sp = child(pkg, "spine")) {
        ncx_id = sp.attribute("toc").value();
        for (auto n : sp.children()) {
            if (!is(n, "itemref")) continue;
            const std::string idref = n.attribute("idref").value();
            const auto it = std::find_if(m_manifest.begin(), m_manifest.end(),
                                         [&](const ManifestItem& m) { return m.id == idref; });
            if (it == m_manifest.end()) continue;   // dangling idref: skip, don't fail the book
            SpineItem s;
            s.idref      = idref;
            s.path       = it->path;
            s.media_type = it->media_type;
            s.linear     = std::string(n.attribute("linear").value()) != "no";
            m_spine.push_back(std::move(s));
        }
    }
    if (m_spine.empty()) { m_error = m_opf + " has an empty spine"; return false; }

    // s006: the cover, best evidence first.
    const auto is_image = [](const ManifestItem& m) { return m.media_type.rfind("image/", 0) == 0; };
    for (const auto& m : m_manifest)
        if (is_image(m) && (" " + m.properties + " ").find(" cover-image ") != std::string::npos) { m_cover = m.path; break; }
    if (m_cover.empty() && !cover_id.empty())
        for (const auto& m : m_manifest)
            if (m.id == cover_id && is_image(m)) { m_cover = m.path; break; }
    if (m_cover.empty())
        for (const auto& m : m_manifest)
            if (is_image(m) && (lower(m.id).find("cover") != std::string::npos ||
                                lower(m.path).find("cover") != std::string::npos)) { m_cover = m.path; break; }

    // s008: the TOC. The nav document first; the NCX if there is no nav or
    // it lists nothing. A missing or broken TOC never fails the book.
    for (const auto& m : m_manifest) {
        if ((" " + m.properties + " ").find(" nav ") == std::string::npos) continue;
        if (const auto x = m_zip.read(m.path)) {
            m_toc = parse_nav(*x, m.path);
            if (!m_toc.empty()) m_toc_source = "nav";
        }
        break;
    }
    if (m_toc.empty()) {
        const ManifestItem* ncx = nullptr;
        for (const auto& m : m_manifest)
            if (!ncx_id.empty() && m.id == ncx_id) { ncx = &m; break; }
        if (!ncx)
            for (const auto& m : m_manifest)
                if (m.media_type == "application/x-dtbncx+xml") { ncx = &m; break; }
        if (ncx)
            if (const auto x = m_zip.read(ncx->path)) {
                m_toc = parse_ncx(*x, ncx->path);
                if (!m_toc.empty()) m_toc_source = "ncx";
            }
    }
    for (auto& e : m_toc) e.spine = e.path.empty() ? -1 : spine_index_of(e.path);
    return true;
}

int Book::spine_index_of(const std::string& path) const {
    std::string p = path;
    if (const auto cut = p.find('#'); cut != std::string::npos) p.resize(cut);
    for (std::size_t i = 0; i < m_spine.size(); ++i)
        if (m_spine[i].path == p) return static_cast<int>(i);
    return -1;
}

std::optional<std::string> Book::read(const std::string& path) const {
    auto bytes = m_zip.read(path);
    if (!bytes) m_error = m_zip.error();
    return bytes;
}

std::string Book::media_type_of(const std::string& path) const {
    for (const auto& m : m_manifest)
        if (m.path == path && !m.media_type.empty()) return m.media_type;
    return media_type_for_ext(path);
}

}  // namespace kaflip::core
