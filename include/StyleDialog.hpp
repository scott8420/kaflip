#pragma once
#include "core/ReadingStyle.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/colordialogbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/fontdialogbutton.h>
#include <gtkmm/label.h>
#include <gtkmm/scale.h>
#include <gtkmm/switch.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/window.h>

#include <string>
#include <vector>

typedef struct _WebKitWebView WebKitWebView;

// ─────────────────────────────────────────────────────────────────────────────
// StyleDialog -- "Reading style" (s007). A hide-on-close singleton (the About
// window's shape) that edits the two styles in core::Styles: the window's
// ("Reading") and focus mode's. Presets in one click; fonts, size, spacing,
// paragraphs, alignment, colours, margins, line width, two pages.
//
// Every control writes through core::styles::set -- one door, one undo step
// (a slider drag merges into one). The dialog has its own Undo / Redo and
// Ctrl+Z, over the style journal. It never paints the book itself: the store
// changes, the Shell restyles the open book (live behind the dialog) and calls
// refresh(), which repaints these controls and the preview from the model.
// ─────────────────────────────────────────────────────────────────────────────
namespace kaflip {

class StyleDialog : public Gtk::Window {
public:
    StyleDialog(core::Styles& styles, core::StyleJournal& journal);
    ~StyleDialog() override;

    void refresh();                          // the controls + preview from the model
    void edit(const core::Styles::Id& id);   // which style the controls edit

private:
    void build();
    // One change from a control: copy the current style, change it, set.
    template <class F> void change(const std::string& label, const std::string& merge, F&& f);
    void update_preview();
    void on_undo();
    void on_redo();

    core::Styles&       m_styles;
    core::StyleJournal& m_journal;
    core::Styles::Id    m_id = core::Styles::kReading;
    bool                m_painting = false;   // refresh() sets controls: not a user choice

    Gtk::ToggleButton   m_edit_reading, m_edit_focus;
    Gtk::Button         m_undo, m_redo;
    Gtk::Label          m_said;
    Gtk::DropDown       m_level, m_paras, m_justify;
    Gtk::FontDialogButton m_body_font, m_head_font;
    Gtk::CheckButton    m_body_book, m_head_book;
    Gtk::Scale          m_size, m_line, m_margin_x, m_margin_y, m_max_line;
    Gtk::Switch         m_hyphens, m_spread;
    Gtk::ColorDialogButton m_page, m_text, m_head, m_link;
    WebKitWebView*      m_preview = nullptr;   // owned by the GTK tree
    Gtk::Label          m_focus_note;          // s008a: "focus mode uses your Reading text and page"
    std::vector<Gtk::Widget*> m_reading_only;  // s008a: greyed while editing focus mode
};

}  // namespace kaflip
