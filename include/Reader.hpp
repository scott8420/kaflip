#pragma once
#include "widgets/Widgets.hpp"
#include "core/Epub.hpp"
#include "core/Paging.hpp"   // s003

#include <sigc++/signal.h>

#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Reader -- the reading surface (s002). A WebKitWebView fed straight out of the
// open zip through the kaflip-book: URI scheme. Nothing is extracted to disk:
// the view asks for kaflip-book://b<token>/OEBPS/Text/ch1.xhtml, the scheme
// handler hands back those bytes with the manifest's media type, and relative
// links, stylesheets and images resolve on their own because they are relative
// URIs under the same scheme.
//
// Owns the open core::Book and the current spine index. Knows nothing about
// menus or buttons: the Shell drives it (open / next / prev) and listens to
// signal_changed() to repaint its labels and grey its actions.
//
// Navigation is a VIEW position, not a model write, so it does not go through
// the undo journal (HANDOFF s002: decided, Scott may overrule).
// ─────────────────────────────────────────────────────────────────────────────
typedef struct _WebKitWebView WebKitWebView;
typedef struct _WebKitUserStyleSheet WebKitUserStyleSheet;   // s006

namespace kaflip {

class Reader : public widgets::Box {
public:
    Reader();
    ~Reader() override;

    // Open an EPUB and show its first linear spine item. On failure the old
    // book (if any) stays open and `error` says why.
    bool open(const std::string& file, std::string& error);

    bool               has_book() const { return m_book != nullptr; }
    const core::Book*  book() const { return m_book.get(); }
    int                chapter() const { return m_chapter; }   // spine index, -1 = none
    int                chapter_count() const;

    bool can_next() const { return next_index() >= 0; }
    bool can_prev() const { return prev_index() >= 0; }
    bool next();
    bool prev();
    bool go_to(int spine_index, core::Land land = core::Land::First);
    // s008: a TOC entry -- a zip path plus an optional fragment (an element
    // id). Same chapter: paginate.js moves to the page the element starts on;
    // another chapter: it loads with #fragment and lands there. false = the
    // path is not in the spine.
    bool go_to_path(const std::string& path, const std::string& fragment);
    // s008: give the page the keyboard (after a popover closes).
    void focus_page();

    // s003 -- pages. The chapter is CSS columns; paginate.js keeps the page and
    // reports it. A turn past either end hands off to the neighbouring linear
    // chapter (core::turn_forward / turn_back decide).
    int  page() const { return m_page; }               // 0-based
    int  page_count() const { return m_page_count; }   // 0 = not reported yet
    bool can_turn_forward() const;
    bool can_turn_back() const;
    bool turn_forward();
    bool turn_back();
    void first_page();
    void last_page();

    // s007 -- the ONE book stylesheet (core::book_css: columns, margins,
    // typography, colours), replaced live; the page re-measures and keeps its
    // place. `page` paints the view behind the book (#rrggbb) so a dark style
    // never flashes white while a chapter loads.
    void set_book_css(const std::string& css, const std::string& page);

    // Book opened, or the chapter moved (by us or by a link in the page).
    sigc::signal<void()>& signal_changed() { return m_changed; }
    // s005: a plain click in the page -- "back", "forward" or "center" (the
    // third it hit). The Shell decides what it means (focus mode only).
    sigc::signal<void(const std::string&)>& signal_tap() { return m_tap; }

private:
    int next_index() const;   // next LINEAR spine item after the current one
    int prev_index() const;   // previous linear one

    // WebKit's C callbacks live in Reader.cpp (the only file that includes
    // webkit.h); they reach back in through this friend.
    friend struct ReaderGlue;
    void on_committed(const std::string& uri);   // a page arrived: sync m_chapter
    void on_finished();                          // s003: land on the last page if asked
    void on_page_report(const std::string& msg); // s003: "page/count" from paginate.js
    void run_js(const std::string& code);

    WebKitWebView*              m_view = nullptr;   // owned by the GTK tree (managed)
    std::unique_ptr<core::Book> m_book;
    unsigned                    m_token = 0;        // bumps per book: no cross-book cache hits
    int                         m_chapter = -1;
    int                         m_page = 0;         // s003
    int                         m_page_count = 0;   // s003: 0 until the page reports
    core::Land                  m_land = core::Land::First;
    WebKitUserStyleSheet*       m_sheet = nullptr;   // s007: the book sheet now in use
    sigc::signal<void()>        m_changed;
    sigc::signal<void(const std::string&)> m_tap;   // s005
};

}  // namespace kaflip
