#pragma once
#include <gtkmm/application.h>
#include <memory>

// App -- the Gtk::Application. Thin: stands up logging in create(), builds the
// Shell on activate. (CANON: thin bootstrap -- main -> App::create() -> Shell.)
namespace kaflip {

class Shell;

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create();

protected:
    App();
    void on_activate() override;
    void on_startup() override;   // accelerators, driven from core::shortcut_registry()
    // s002: `kaflip book.epub`, or "Open With" from the file manager.
    void on_open(const type_vec_files& files, const Glib::ustring& hint) override;

private:
    void ensure_shell();          // build + present the one window (activate and open share it)
    Shell* m_shell = nullptr;   // owned by the application via add_window
};

}  // namespace kaflip
