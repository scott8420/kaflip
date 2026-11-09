#include "core/Library.hpp"

#include "json.hpp"   // vendored nlohmann/json (third_party/, core-private)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kaflip::core {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Store
// ─────────────────────────────────────────────────────────────────────────────
std::optional<LibItem> Library::get(const Id& id) const {
    auto it = m_items.find(id);
    if (it == m_items.end()) return std::nullopt;
    return it->second;
}

void Library::put(const Id& id, const std::optional<LibItem>& v) {
    if (m_loading == 0 && m_door && !m_door()) ++m_strays;   // the gate
    if (v) m_items[id] = *v;
    else   m_items.erase(id);
    if (m_changed) m_changed();
}

std::vector<LibItem> Library::books() const {
    std::vector<LibItem> out;
    for (const auto& [id, v] : m_items)
        if (v.kind == LibItem::Kind::Book) out.push_back(v);
    std::sort(out.begin(), out.end(), [](const LibItem& a, const LibItem& b) {
        const auto la = lower(a.title), lb = lower(b.title);
        return la != lb ? la < lb : a.path < b.path;
    });
    return out;
}

// s011 -- the orders.
namespace {
struct SortRow { SortBy by; const char* name; const char* label; };
constexpr SortRow kSorts[] = {
    {SortBy::Title,    "title",    "Title"},
    {SortBy::Author,   "author",   "Author"},
    {SortBy::Recent,   "recent",   "Recently read"},
    {SortBy::Added,    "added",    "Date added"},
    {SortBy::Progress, "progress", "Progress"},
};
}  // namespace

const char* sort_name(SortBy s) {
    for (const auto& r : kSorts) if (r.by == s) return r.name;
    return "title";
}
const char* sort_label(SortBy s) {
    for (const auto& r : kSorts) if (r.by == s) return r.label;
    return "Title";
}
std::optional<SortBy> sort_from_name(const std::string& n) {
    for (const auto& r : kSorts) if (n == r.name) return r.by;
    return std::nullopt;
}
const std::vector<SortBy>& sort_orders() {
    static const std::vector<SortBy> v = [] {
        std::vector<SortBy> o;
        for (const auto& r : kSorts) o.push_back(r.by);
        return o;
    }();
    return v;
}

std::vector<LibItem> Library::books(SortBy by) const {
    struct Row { LibItem b; std::string t, a; std::int64_t when; double pct; int band; };
    std::vector<Row> rows;
    for (const auto& [id, v] : m_items) {
        if (v.kind != LibItem::Kind::Book) continue;
        Row r{v, library::title_key(v.title.empty() ? fs::path(v.path).stem().string() : v.title),
              library::author_key(v.author), -1, -1.0, 0};
        if (auto o = opened(v.path)) r.when = *o;
        if (auto p = progress(v.path)) r.pct = *p;
        // Progress bands: reading (0 < p < 1) | finished | unread.
        r.band = r.pct >= 1.0 ? 1 : r.pct > 0.0 ? 0 : 2;
        rows.push_back(std::move(r));
    }
    auto by_title = [](const Row& x, const Row& y) {
        return x.t != y.t ? x.t < y.t : x.b.path < y.b.path;
    };
    std::sort(rows.begin(), rows.end(), [&](const Row& x, const Row& y) {
        switch (by) {
        case SortBy::Author:
            if (x.a.empty() != y.a.empty()) return y.a.empty();   // no author last
            if (x.a != y.a) return x.a < y.a;
            break;
        case SortBy::Recent:
            if (x.when != y.when) return x.when > y.when;          // never (-1) last
            break;
        case SortBy::Added:
            if (x.b.added != y.b.added) return x.b.added > y.b.added;
            break;
        case SortBy::Progress:
            if (x.band != y.band) return x.band < y.band;
            if (x.band == 0 && x.pct != y.pct) return x.pct > y.pct;
            break;
        case SortBy::Title: break;
        }
        return by_title(x, y);
    });
    std::vector<LibItem> out;
    out.reserve(rows.size());
    for (auto& r : rows) out.push_back(std::move(r.b));
    return out;
}

void Library::set_opened(const std::string& path, std::int64_t when) {
    if (!path.empty()) m_opened[path] = when;
}

std::optional<std::int64_t> Library::opened(const std::string& path) const {
    auto it = m_opened.find(path);
    if (it == m_opened.end()) return std::nullopt;
    return it->second;
}

std::vector<LibItem> Library::folders() const {
    std::vector<LibItem> out;
    for (const auto& [id, v] : m_items)
        if (v.kind == LibItem::Kind::Folder) out.push_back(v);
    return out;   // std::map: already by id = by path
}

std::optional<Place> Library::place(const std::string& path) const {
    auto it = m_places.find(path);
    if (it == m_places.end()) return std::nullopt;
    return it->second;
}

void Library::set_place(const std::string& path, const Place& p) {
    if (path.empty() || p.href.empty()) return;
    m_places[path] = p;   // not a record: no door, no on_change, no step
}

void Library::set_progress(const std::string& path, double p) {
    if (path.empty() || !(p >= 0.0)) return;
    m_progress[path] = std::min(1.0, p);
}

std::optional<double> Library::progress(const std::string& path) const {
    auto it = m_progress.find(path);
    if (it == m_progress.end()) return std::nullopt;
    return it->second;
}

std::int64_t Library::next_added() const {
    std::int64_t n = 0;
    for (const auto& [id, v] : m_items) n = std::max(n, v.added);
    return n + 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON
// ─────────────────────────────────────────────────────────────────────────────
std::string Library::to_json() const {
    json arr = json::array();
    for (const auto& [id, v] : m_items) {
        json e = {{"kind", v.kind == LibItem::Kind::Folder ? "folder" : "book"},
                  {"path", v.path}, {"title", v.title}, {"author", v.author},
                  {"cover", v.cover}, {"folder", v.folder}, {"added", v.added}};
        if (!v.marks.empty()) {   // s008
            json ms = json::array();
            for (const auto& m : v.marks)
                ms.push_back({{"id", m.id}, {"href", m.at.href}, {"ch", m.at.ch}, {"spine", m.spine},
                              {"title", m.title}, {"snippet", m.snippet}});
            e["marks"] = ms;
        }
        arr.push_back(e);
    }
    json places = json::object();   // s008
    for (const auto& [path, p] : m_places) places[path] = {{"href", p.href}, {"ch", p.ch}};
    for (const auto& [path, pct] : m_progress) places[path]["progress"] = pct;   // s009
    for (const auto& [path, t] : m_opened) places[path]["opened"] = t;          // s011
    return json{{"version", 1}, {"items", arr}, {"places", places},
                {"view", {{"sort", sort_name(m_sort)}}}}.dump(1);   // s011
}

bool Library::from_json(const std::string& text) {
    Loading load(*this);
    m_items.clear();
    m_places.clear();
    m_progress.clear();
    m_opened.clear();          // s011
    m_sort = SortBy::Title;
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("items") || !doc["items"].is_array())
        return false;
    for (const auto& e : doc["items"]) {
        if (!e.is_object()) continue;
        LibItem v;
        v.kind   = e.value("kind", std::string("book")) == "folder" ? LibItem::Kind::Folder
                                                                    : LibItem::Kind::Book;
        v.path   = e.value("path", std::string());
        v.title  = e.value("title", std::string());
        v.author = e.value("author", std::string());
        v.cover  = e.value("cover", std::string());
        v.folder = e.value("folder", std::string());
        v.added  = e.value("added", std::int64_t{0});
        if (e.contains("marks") && e["marks"].is_array())   // s008
            for (const auto& m : e["marks"]) {
                if (!m.is_object()) continue;
                Bookmark b;
                b.id      = m.value("id", 0);
                b.at.href = m.value("href", std::string());
                b.at.ch   = std::max(0, m.value("ch", 0));
                b.spine   = m.value("spine", 0);
                b.title   = m.value("title", std::string());
                b.snippet = m.value("snippet", std::string());
                if (!b.at.href.empty()) v.marks.push_back(std::move(b));
            }
        if (v.path.empty()) continue;
        put(v.kind == LibItem::Kind::Folder ? folder_id(v.path) : book_id(v.path), v);
    }
    if (doc.contains("places") && doc["places"].is_object())   // s008
        for (const auto& [path, p] : doc["places"].items()) {
            if (!p.is_object()) continue;
            Place pl{p.value("href", std::string()), std::max(0, p.value("ch", 0))};
            if (!pl.href.empty()) m_places[path] = pl;
            if (p.contains("opened") && p["opened"].is_number_integer())   // s011
                m_opened[path] = p["opened"].get<std::int64_t>();
            if (p.contains("progress") && p["progress"].is_number())   // s009
                set_progress(path, p["progress"].get<double>());
        }
    if (doc.contains("view") && doc["view"].is_object() && doc["view"].contains("sort") &&
        doc["view"]["sort"].is_string())   // s011
        if (auto s = sort_from_name(doc["view"]["sort"].get<std::string>())) m_sort = *s;
    return true;
}

bool Library::save(const std::string& file) const {
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    const std::string tmp = file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << to_json();
        if (!out) return false;
    }
    fs::rename(tmp, file, ec);   // atomic replace: a crash never leaves half a file
    return !ec;
}

bool Library::load(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { Loading load(*this); m_items.clear(); m_places.clear(); m_progress.clear(); m_opened.clear(); m_sort = SortBy::Title; return true; }   // no file yet: empty
    std::ostringstream ss;
    ss << in.rdbuf();
    return from_json(ss.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Verbs
// ─────────────────────────────────────────────────────────────────────────────
namespace library {

void note_opened(Library& lib, LibItem book) {
    Library::Loading side_effect(lib);   // not a user choice about the library
    book.kind = LibItem::Kind::Book;
    const auto id = Library::book_id(book.path);
    if (auto old = lib.get(id)) {
        book.folder = old->folder;
        book.added  = old->added;
        book.marks  = old->marks;   // s008
        if (book.cover.empty()) book.cover = old->cover;
        if (*old == book) return;
    } else {
        book.added = lib.next_added();
    }
    lib.put(id, book);
}

bool add_folder(LibraryJournal& j, Library& lib, const std::string& folder,
                std::vector<LibItem> books) {
    const auto fid = Library::folder_id(folder);
    if (lib.get(fid)) return false;
    std::int64_t n = lib.next_added();
    LibItem f;
    f.kind  = LibItem::Kind::Folder;
    f.path  = folder;
    f.title = fs::path(folder).filename().string();
    f.added = n++;
    std::vector<Library::Id> ids{fid};
    std::vector<LibItem> fresh;
    for (auto& b : books) {
        if (lib.has_book(b.path)) continue;
        b.kind   = LibItem::Kind::Book;
        b.folder = folder;
        b.added  = n++;
        ids.push_back(Library::book_id(b.path));
        fresh.push_back(std::move(b));
    }
    const std::string label = "Add folder “" + f.title + "”";
    return j.run(label, ids, [&]() {
        lib.put(fid, f);
        for (const auto& b : fresh) lib.put(Library::book_id(b.path), b);
    });
}

bool remove_book(LibraryJournal& j, Library& lib, const std::string& path) {
    const auto id = Library::book_id(path);
    const auto b = lib.get(id);
    if (!b) return false;
    const std::string name = b->title.empty() ? fs::path(path).stem().string() : b->title;
    return j.run("Remove “" + name + "”", {id},
                 [&]() { lib.put(id, std::nullopt); });
}

bool add_bookmark(LibraryJournal& j, Library& lib, const std::string& path, Bookmark mark) {
    const auto id = Library::book_id(path);
    auto b = lib.get(id);
    if (!b || mark.at.href.empty()) return false;
    int next = 1;
    for (const auto& m : b->marks) {
        if (m.at == mark.at) return false;
        next = std::max(next, m.id + 1);
    }
    mark.id = next;
    b->marks.push_back(std::move(mark));
    std::stable_sort(b->marks.begin(), b->marks.end(), [](const Bookmark& x, const Bookmark& y) {
        return x.spine != y.spine ? x.spine < y.spine : x.at.ch < y.at.ch;
    });
    const auto& mk = *std::find_if(b->marks.begin(), b->marks.end(),
                                   [&](const Bookmark& m) { return m.id == next; });
    const std::string label = "Add bookmark" + (mk.title.empty() ? std::string() : " “" + mk.title + "”");
    return j.run(label, {id}, [&]() { lib.put(id, *b); });
}

bool remove_bookmark(LibraryJournal& j, Library& lib, const std::string& path, int mark_id) {
    const auto id = Library::book_id(path);
    auto b = lib.get(id);
    if (!b) return false;
    const auto it = std::find_if(b->marks.begin(), b->marks.end(),
                                 [&](const Bookmark& m) { return m.id == mark_id; });
    if (it == b->marks.end()) return false;
    const std::string label = "Remove bookmark" + (it->title.empty() ? std::string() : " “" + it->title + "”");
    b->marks.erase(it);
    return j.run(label, {id}, [&]() { lib.put(id, *b); });
}

// s011 -- search. UTF-8 in, UTF-8 out: ASCII lowered; the two-byte Latin-1
// letters (C3 80..C3 BF) folded to their base ASCII letter; any other byte
// sequence copied as is (so Cyrillic or CJK still match themselves exactly).
std::string fold(const std::string& text) {
    // C3 80 .. C3 BF -> base letter ('\0' = keep the original two bytes).
    static const char kLatin1[64] = {
        'a','a','a','a','a','a', 0 ,'c','e','e','e','e','i','i','i','i',   // C0-CF
        'd','n','o','o','o','o','o', 0 ,'o','u','u','u','u','y', 0 , 0 ,  // D0-DF
        'a','a','a','a','a','a', 0 ,'c','e','e','e','e','i','i','i','i',   // E0-EF
        'd','n','o','o','o','o','o', 0 ,'o','u','u','u','u','y', 0 ,'y'};  // F0-FF
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) { out += char(std::tolower(c)); continue; }
        if (c == 0xC3 && i + 1 < text.size()) {
            const auto d = static_cast<unsigned char>(text[i + 1]);
            if (d >= 0x80 && d <= 0xBF && kLatin1[d - 0x80]) { out += kLatin1[d - 0x80]; ++i; continue; }
        }
        out += char(c);
    }
    return out;
}

std::string title_key(const std::string& title) {
    std::string t = fold(title);
    std::size_t i = 0;
    while (i < t.size() && (std::isspace(static_cast<unsigned char>(t[i])) ||
                            t[i] == '"' || t[i] == '\'' || t[i] == '(' || t[i] == '[')) ++i;
    t.erase(0, i);
    for (const char* art : {"the ", "a ", "an "}) {
        const std::string a = art;
        if (t.size() > a.size() && t.compare(0, a.size(), a) == 0) { t.erase(0, a.size()); break; }
    }
    return t;
}

std::string author_key(const std::string& author) {
    std::string a = fold(author);
    while (!a.empty() && std::isspace(static_cast<unsigned char>(a.back()))) a.pop_back();
    if (a.empty()) return a;
    if (const auto comma = a.find(','); comma != std::string::npos)   // "Laumer, Keith"
        return a.substr(0, comma) + '\x01' + a.substr(comma + 1);
    // "Keith Laumer" -> "laumer\x01keith"; a trailing "Jr." / "Sr." / "III" is
    // not the surname ("Kurt Vonnegut Jr." files under V).
    std::vector<std::string> w;
    std::size_t i = 0;
    while (i < a.size()) {
        while (i < a.size() && std::isspace(static_cast<unsigned char>(a[i]))) ++i;
        std::size_t j = i;
        while (j < a.size() && !std::isspace(static_cast<unsigned char>(a[j]))) ++j;
        if (j > i) w.push_back(a.substr(i, j - i));
        i = j;
    }
    std::size_t last = w.size() - 1;
    for (const char* suf : {"jr.", "jr", "sr.", "sr", "ii", "iii", "iv"})
        if (last > 0 && w[last] == suf) { --last; break; }
    std::string key = w[last] + '\x01';
    for (std::size_t k = 0; k < last; ++k) key += w[k] + ' ';
    return key;
}

bool matches(const LibItem& book, const std::string& query) {
    const std::string hay = fold(book.title) + '\n' + fold(book.author) + '\n' +
                            fold(std::filesystem::path(book.path).filename().string());
    const std::string q = fold(query);
    std::size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && std::isspace(static_cast<unsigned char>(q[i]))) ++i;
        std::size_t j = i;
        while (j < q.size() && !std::isspace(static_cast<unsigned char>(q[j]))) ++j;
        if (j > i && hay.find(q.substr(i, j - i)) == std::string::npos) return false;
        i = j;
    }
    return true;
}

std::vector<int> marks_between(const LibItem& book, const std::string& href, int from, int to) {
    std::vector<int> out;
    for (const auto& m : book.marks)
        if (m.at.href == href && m.at.ch >= from && (to < 0 || m.at.ch < to)) out.push_back(m.id);
    return out;
}

const std::vector<Verb>& verbs() {
    // Each verb against a fixture library (two books, opened directly). The
    // gate: through the door, a step, undo restores exactly, redo reproduces.
    static const std::vector<Verb> kVerbs = {
        {"add folder", [](LibraryJournal& j, Library& l) {
             add_folder(j, l, "/lib/shelf", {LibItem{LibItem::Kind::Book, "/lib/shelf/a.epub", "A"},
                                             LibItem{LibItem::Kind::Book, "/lib/shelf/b.epub", "B"}});
         }},
        {"remove book", [](LibraryJournal& j, Library& l) {
             remove_book(j, l, "/books/one.epub");
         }},
        {"add bookmark", [](LibraryJournal& j, Library& l) {   // s008
             add_bookmark(j, l, "/books/one.epub", Bookmark{0, Place{"OEBPS/c2.xhtml", 120}, 2, "Two", "It was"});
         }},
        {"remove bookmark", [](LibraryJournal& j, Library& l) {   // s008: the fixture's one.epub carries bookmark 1
             remove_bookmark(j, l, "/books/one.epub", 1);
         }},
    };
    return kVerbs;
}

}  // namespace library

// ─────────────────────────────────────────────────────────────────────────────
// Scanning + the cache key
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> scan_epubs(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) return out;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const auto& p = it->path();
        const std::string name = p.filename().string();
        if (!name.empty() && name[0] == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && lower(p.extension().string()) == ".epub")
            out.push_back(p.string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string cover_key(const std::string& path, std::uintmax_t size, std::int64_t mtime) {
    // FNV-1a 64 over path | size | mtime. Not security, just a stable name.
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](const std::string& s) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        h ^= 0xff; h *= 1099511628211ull;
    };
    mix(path);
    mix(std::to_string(size));
    mix(std::to_string(mtime));
    mix("thumb-300x450");   // the thumbnail's shape: a new shape, a new name
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 15; i >= 0; --i) out += hex[(h >> (i * 4)) & 0xf];
    return out + ".png";
}

}  // namespace kaflip::core
