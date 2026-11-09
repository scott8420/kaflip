#include "Reader.hpp"
#include "Log.hpp"
#include "core/ReadingStyle.hpp"

#include <webkit/webkit.h>
#include <gio/gio.h>

#include <cstdlib>
#include <functional>

// Reader.cpp -- the only file that includes webkit.h. WebKitGTK has no C++
// binding, so the view is created through the C API, wrapped as a plain
// Gtk::Widget for the tree, and its signals land in ReaderGlue below.

namespace kaflip {

namespace {
// The one live Reader. The scheme is registered once per WebKitWebContext and
// lives as long as the process; the Reader may not, so the handler looks the
// reader up here instead of holding a pointer WebKit could outlive.
Reader* g_reader = nullptr;
bool    g_scheme_registered = false;
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ReaderGlue -- WebKit's C callbacks, with friend access to Reader.
// ─────────────────────────────────────────────────────────────────────────────
struct ReaderGlue {
    // kaflip-book://b<token>/<path> -> the bytes of that zip entry.
    static void serve(WebKitURISchemeRequest* request, gpointer) {
        const std::string uri = webkit_uri_scheme_request_get_uri(request);
        auto fail = [&](const std::string& why) {
            if (auto lg = log::get(log::Area::Web)) lg->warn("serve {} -> {}", uri, why);
            GError* err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, why.c_str());
            webkit_uri_scheme_request_finish_error(request, err);
            g_error_free(err);
        };
        const auto parsed = core::parse_book_uri(uri);
        if (!parsed) return fail("malformed book URI");
        Reader* r = g_reader;
        if (!r || !r->m_book) return fail("no book open");
        if (parsed->token != r->m_token) return fail("stale book token");
        auto bytes = r->m_book->read(parsed->path);
        if (!bytes) return fail(r->m_book->error());

        const std::string mime = r->m_book->media_type_of(parsed->path);
        if (auto lg = log::get(log::Area::Web))
            lg->debug("serve {} ({} bytes, {})", parsed->path, bytes->size(), mime);
        const gsize len = bytes->size();
        // GBytes takes ownership of a heap copy; the stream holds the GBytes.
        auto* heap = new std::string(std::move(*bytes));
        GBytes* gb = g_bytes_new_with_free_func(heap->data(), heap->size(),
            [](gpointer p) { delete static_cast<std::string*>(p); }, heap);
        GInputStream* stream = g_memory_input_stream_new_from_bytes(gb);
        g_bytes_unref(gb);
        webkit_uri_scheme_request_finish(request, stream, static_cast<gint64>(len), mime.c_str());
        g_object_unref(stream);
    }

    // Only kaflip-book: (and about:blank) loads inside the view. A clicked web
    // link goes to the desktop's browser; anything else is refused.
    static gboolean decide_policy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                  WebKitPolicyDecisionType type, gpointer) {
        if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) return FALSE;   // default
        auto* nav = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(nav);
        const char* raw = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
        const std::string uri = raw ? raw : "";
        auto lg = log::get(log::Area::Web);

        if (uri.rfind(std::string(core::kBookScheme) + ":", 0) == 0 || uri == "about:blank") {
            if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
                // target="_blank" inside the book: read it here, no new window.
                webkit_policy_decision_ignore(decision);
                webkit_web_view_load_uri(view, uri.c_str());
                return TRUE;
            }
            return FALSE;   // use
        }
        webkit_policy_decision_ignore(decision);
        if (webkit_navigation_action_get_navigation_type(action) == WEBKIT_NAVIGATION_TYPE_LINK_CLICKED &&
            (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0 || uri.rfind("mailto:", 0) == 0)) {
            if (lg) lg->info("external link -> desktop: {}", uri);
            g_app_info_launch_default_for_uri_async(uri.c_str(), nullptr, nullptr, nullptr, nullptr);
        } else if (lg) {
            lg->info("navigation refused: {}", uri);
        }
        return TRUE;
    }

    static void load_changed(WebKitWebView* view, WebKitLoadEvent event, gpointer) {
        if (!g_reader) return;
        if (event == WEBKIT_LOAD_COMMITTED) {
            const char* uri = webkit_web_view_get_uri(view);
            if (uri) g_reader->on_committed(uri);
        } else if (event == WEBKIT_LOAD_FINISHED) {
            g_reader->on_finished();
        }
    }

    // s003: paginate.js -> window.webkit.messageHandlers.kaflip.postMessage("3/11")
    static void page_message(WebKitUserContentManager*, JSCValue* value, gpointer) {
        if (!g_reader || !value || !jsc_value_is_string(value)) return;
        char* str = jsc_value_to_string(value);
        g_reader->on_page_report(str ? str : "");
        g_free(str);
    }

    static gboolean load_failed(WebKitWebView*, WebKitLoadEvent, gchar* uri, GError* err, gpointer) {
        if (auto lg = log::get(log::Area::Web))
            lg->warn("load failed: {} ({})", uri ? uri : "?", err ? err->message : "?");
        return FALSE;   // let WebKit show its own error page
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────────────────────────────────────
Reader::Reader()
    : widgets::Box("reader", Gtk::Orientation::VERTICAL) {
    g_reader = this;

    // s004: the Reader IS the book card; the Shell shows a hero instead of it
    // until a book is open. overflow HIDDEN clips the view to the card's corners.
    add_css_class("book-card");
    set_overflow(Gtk::Overflow::HIDDEN);

    GtkWidget* w = webkit_web_view_new();
    m_view = WEBKIT_WEB_VIEW(w);
    gtk_widget_set_name(w, "reader.view");

    WebKitWebContext* ctx = webkit_web_view_get_context(m_view);
    if (!g_scheme_registered) {
        webkit_web_context_register_uri_scheme(ctx, core::kBookScheme,
                                               &ReaderGlue::serve, nullptr, nullptr);
        g_scheme_registered = true;
    }

    // The book's own <script> elements stay off (an EPUB is a document, and
    // s003's pagination script is ours, injected as a user script, which this
    // setting does not block). KAFLIP_WEB_INSPECTOR=1 turns on the inspector.
    WebKitSettings* st = webkit_web_view_get_settings(m_view);
    webkit_settings_set_enable_javascript_markup(st, FALSE);
    webkit_settings_set_enable_developer_extras(st, std::getenv("KAFLIP_WEB_INSPECTOR") != nullptr);

    // s003 -- pagination: the column stylesheet (USER level, so its !important
    // outranks the publisher's) and the page keeper script, both top frame only.
    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(m_view);
    {
        // s007: the book sheet is set by the Shell (set_book_css) from the
        // reading style; until then, Day.
        set_book_css(core::book_css(core::ReadingStyle{}), "#ffffff");

        GError* err = nullptr;
        GBytes* js = g_resources_lookup_data("/io/github/scott8420/Kaflip/js/paginate.js",
                                             G_RESOURCE_LOOKUP_FLAGS_NONE, &err);
        if (js) {
            gsize n = 0;
            const char* data = static_cast<const char*>(g_bytes_get_data(js, &n));
            const std::string src(data, n);
            WebKitUserScript* script = webkit_user_script_new(
                src.c_str(), WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, nullptr, nullptr);
            webkit_user_content_manager_add_script(ucm, script);
            webkit_user_script_unref(script);
            g_bytes_unref(js);
        } else if (auto lg = log::get(log::Area::Web)) {
            lg->error("paginate.js missing from the resource bundle: {}", err ? err->message : "?");
            if (err) g_error_free(err);
        }
        webkit_user_content_manager_register_script_message_handler(ucm, "kaflip", nullptr);
        g_signal_connect(ucm, "script-message-received::kaflip",
                         G_CALLBACK(&ReaderGlue::page_message), nullptr);
    }

    g_signal_connect(m_view, "decide-policy", G_CALLBACK(&ReaderGlue::decide_policy), nullptr);
    g_signal_connect(m_view, "load-changed",  G_CALLBACK(&ReaderGlue::load_changed), nullptr);
    g_signal_connect(m_view, "load-failed",   G_CALLBACK(&ReaderGlue::load_failed), nullptr);

    Gtk::Widget* view = Gtk::manage(Glib::wrap(w));
    view->set_vexpand(true);
    view->set_hexpand(true);
    append(*view);
}

Reader::~Reader() {
    if (g_reader == this) g_reader = nullptr;
    if (m_sheet) webkit_user_style_sheet_unref(m_sheet);
}

int Reader::chapter_count() const {
    return m_book ? static_cast<int>(m_book->spine().size()) : 0;
}

bool Reader::open(const std::string& file, std::string& error) {
    auto book = std::make_unique<core::Book>();
    if (!book->open(file)) {
        error = book->error();
        if (auto lg = log::get(log::Area::Book)) lg->warn("open '{}': {}", file, error);
        return false;
    }
    if (auto lg = log::get(log::Area::Book))
        lg->info("open '{}': \"{}\", {} spine item(s), OPF {}", file, book->meta().title,
                 book->spine().size(), book->opf_path());
    m_book = std::move(book);
    ++m_token;
    m_chapter = -1;

    gtk_widget_grab_focus(GTK_WIDGET(m_view));   // s003: page keys land here

    // Start at the first linear item (a cover page counts; s007 may skip it).
    int first = 0;
    const auto& sp = m_book->spine();
    for (std::size_t i = 0; i < sp.size(); ++i)
        if (sp[i].linear) { first = static_cast<int>(i); break; }
    go_to(first);
    return true;
}

namespace {
// A book with no linear items at all is malformed; read it in spine order.
bool linear_at(const core::Book& b, int i) {
    const auto& sp = b.spine();
    for (const auto& s : sp) if (s.linear) return sp[static_cast<std::size_t>(i)].linear;
    return true;
}
}  // namespace

int Reader::next_index() const {
    if (!m_book) return -1;
    for (int i = m_chapter + 1; i < chapter_count(); ++i)
        if (linear_at(*m_book, i)) return i;
    return -1;
}

int Reader::prev_index() const {
    if (!m_book) return -1;
    for (int i = m_chapter - 1; i >= 0; --i)
        if (linear_at(*m_book, i)) return i;
    return -1;
}

bool Reader::next() { const int i = next_index(); return i >= 0 && go_to(i); }
bool Reader::prev() { const int i = prev_index(); return i >= 0 && go_to(i); }

bool Reader::go_to(int index, core::Land land) {
    if (!m_book || index < 0 || index >= chapter_count()) return false;
    m_chapter = index;
    m_land = land;
    m_page = 0;
    m_page_count = 0;   // until the new chapter reports
    const std::string uri = core::book_uri(m_token, m_book->spine()[static_cast<std::size_t>(index)].path);
    if (auto lg = log::get(log::Area::Book))
        lg->info("chapter {}/{} -> {}", index + 1, chapter_count(), uri);
    webkit_web_view_load_uri(m_view, uri.c_str());
    m_changed.emit();
    return true;
}

void Reader::on_committed(const std::string& uri) {
    const auto parsed = core::parse_book_uri(uri);
    if (!parsed || !m_book || parsed->token != m_token) return;
    const int idx = m_book->spine_index_of(parsed->path);
    if (idx < 0 || idx == m_chapter) return;
    // A link in the page took us to another spine item.
    if (auto lg = log::get(log::Area::Book))
        lg->info("link -> chapter {}/{}", idx + 1, chapter_count());
    m_chapter = idx;
    m_land = core::Land::First;
    m_page = 0;
    m_page_count = 0;
    m_changed.emit();
}

// ─────────────────────────────────────────────────────────────────────────────
// s003 -- pages
// ─────────────────────────────────────────────────────────────────────────────
void Reader::run_js(const std::string& code) {
    webkit_web_view_evaluate_javascript(m_view, code.c_str(), -1, nullptr, nullptr,
                                        nullptr, nullptr, nullptr);
}

void Reader::on_finished() {
    if (m_land == core::Land::Last) {
        m_land = core::Land::First;
        run_js("window.kaflip && window.kaflip.last()");
    }
}

void Reader::on_page_report(const std::string& msg) {
    if (msg.rfind("tap:", 0) == 0) {   // s005: a tap zone, not a page report
        m_tap.emit(msg.substr(4));
        return;
    }
    const auto r = core::parse_page_report(msg);
    if (!r) {
        if (auto lg = log::get(log::Area::Web)) lg->warn("bad page report '{}'", msg);
        return;
    }
    if (r->page == m_page && r->count == m_page_count) return;
    m_page = r->page;
    m_page_count = r->count;
    if (auto lg = log::get(log::Area::Book))
        lg->debug("page {}/{} (chapter {})", m_page + 1, m_page_count, m_chapter + 1);
    m_changed.emit();
}

bool Reader::can_turn_forward() const {
    return m_book && core::turn_forward(m_page, m_page_count, next_index()).kind != core::Turn::Kind::None;
}

bool Reader::can_turn_back() const {
    return m_book && core::turn_back(m_page, m_page_count, prev_index()).kind != core::Turn::Kind::None;
}

namespace {
bool apply(Reader& r, const core::Turn& t, const std::function<void(int)>& page_to) {
    switch (t.kind) {
        case core::Turn::Kind::None:    return false;
        case core::Turn::Kind::Page:    page_to(t.target); return true;
        case core::Turn::Kind::Chapter: return r.go_to(t.target, t.land);
    }
    return false;
}
}  // namespace

bool Reader::turn_forward() {
    if (!m_book) return false;
    return apply(*this, core::turn_forward(m_page, m_page_count, next_index()),
                 [this](int p) { run_js("window.kaflip && window.kaflip.go(" + std::to_string(p) + ")"); });
}

bool Reader::turn_back() {
    if (!m_book) return false;
    return apply(*this, core::turn_back(m_page, m_page_count, prev_index()),
                 [this](int p) { run_js("window.kaflip && window.kaflip.go(" + std::to_string(p) + ")"); });
}

void Reader::set_book_css(const std::string& css, const std::string& page) {
    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(m_view);
    if (m_sheet) {
        webkit_user_content_manager_remove_style_sheet(ucm, m_sheet);
        webkit_user_style_sheet_unref(m_sheet);
    }
    m_sheet = webkit_user_style_sheet_new(css.c_str(), WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                                          WEBKIT_USER_STYLE_LEVEL_USER, nullptr, nullptr);
    webkit_user_content_manager_add_style_sheet(ucm, m_sheet);
    GdkRGBA rgba;
    if (gdk_rgba_parse(&rgba, page.c_str())) webkit_web_view_set_background_color(m_view, &rgba);
    // Margins, size or the spread may have moved every page boundary.
    if (m_book) run_js("window.kaflip && window.kaflip.relayout()");
}

void Reader::first_page() { if (m_book) run_js("window.kaflip && window.kaflip.first()"); }
void Reader::last_page()  { if (m_book) run_js("window.kaflip && window.kaflip.last()"); }

}  // namespace kaflip
