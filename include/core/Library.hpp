#pragma once
// core::Library -- the books Ka-flip knows (s006). GTK-free; the selftest
// drives it headless.
//
// One store of records keyed by id: "book:<path>" and "folder:<path>". A book
// arrives two ways:
//   * you OPEN it -- it joins the library as a side effect, like recents. Not
//     a user choice about the library, so not an undo step (note_opened).
//   * you ADD A FOLDER -- the folder and every EPUB found under it arrive as
//     ONE undo step (add_folder). Undo takes the folder and its books away.
// REMOVE a book from the library is a user choice: one undo step.
//
// The store meets core/Undo.hpp's contract (get / put by id, the whole record
// by value) and keeps the one-door guard: a put outside the journal, outside a
// load, counts as a stray, and the selftest gate fails on one.
#include "core/Undo.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kaflip::core {

struct LibItem {
    enum class Kind { Book, Folder };
    Kind         kind = Kind::Book;
    std::string  path;
    std::string  title;     // book: OPF title (or the file's stem); folder: its name
    std::string  author;    // book: first creator
    std::string  cover;     // book: cached thumbnail file ("" = none -- a title tile)
    std::string  folder;    // book: the folder that brought it ("" = opened directly)
    std::int64_t added = 0; // arrival order
    bool operator==(const LibItem&) const = default;
};

class Library {
public:
    using Id    = std::string;
    using Value = LibItem;

    static Id book_id(const std::string& path)   { return "book:" + path; }
    static Id folder_id(const std::string& path) { return "folder:" + path; }

    // ── the journal's contract ──────────────────────────────────────────────
    std::optional<LibItem> get(const Id& id) const;
    void put(const Id& id, const std::optional<LibItem>& v);

    // ── reading ─────────────────────────────────────────────────────────────
    std::vector<LibItem> books() const;     // by title, case-insensitive, then path
    std::vector<LibItem> folders() const;   // by path
    bool                 has_book(const std::string& path) const { return m_items.count(book_id(path)) > 0; }
    std::size_t          size() const { return m_items.size(); }
    std::int64_t         next_added() const;

    // ── persistence (JSON). load() is not a user choice: not undoable. ──────
    std::string to_json() const;
    bool        from_json(const std::string& text);   // false: unreadable (store left empty)
    bool        save(const std::string& file) const;
    bool        load(const std::string& file);        // a missing file is an empty library

    // ── the one-door guard ──────────────────────────────────────────────────
    void set_door(std::function<bool()> open) { m_door = std::move(open); }
    int  strays() const { return m_strays; }
    struct Loading {
        explicit Loading(Library& l) : m_l(l) { ++m_l.m_loading; }
        ~Loading() { --m_l.m_loading; }
        Library& m_l;
    };
    void set_on_change(std::function<void()> f) { m_changed = std::move(f); }

private:
    std::map<Id, LibItem>  m_items;
    int                    m_strays = 0;
    int                    m_loading = 0;
    std::function<bool()>  m_door;
    std::function<void()>  m_changed;
};

using LibraryJournal = Journal<Library>;

namespace library {

// Opening a book puts it in the library (or refreshes its title / author /
// cover). A side effect, like recents: NOT a step.
void note_opened(Library& lib, LibItem book);

// One step: the folder and the books found under it. Books already in the
// library are left as they are (not re-owned by the folder). False: nothing to
// add (the folder is already there).
bool add_folder(LibraryJournal& j, Library& lib, const std::string& folder,
                std::vector<LibItem> books);

// One step: the book leaves the library (the file is not touched).
bool remove_book(LibraryJournal& j, Library& lib, const std::string& path);

// The verb table the selftest gate walks (CANON: every verb through the door).
struct Verb {
    std::string name;
    std::function<void(LibraryJournal&, Library&)> run;
};
const std::vector<Verb>& verbs();

}  // namespace library

// ── scanning + the thumbnail cache key (pure) ──────────────────────────────
// Every *.epub under `dir` (any case), recursively, hidden folders skipped,
// sorted. Unreadable folders are skipped, not fatal.
std::vector<std::string> scan_epubs(const std::string& dir);

// A stable cache file name for a book's cover thumbnail: changes when the file
// does (size or mtime), so a replaced book gets a fresh cover.
std::string cover_key(const std::string& path, std::uintmax_t size, std::int64_t mtime);

}  // namespace kaflip::core
