#pragma once
// Scanner -- adds a folder's books off the main thread (s006; REQUIREMENTS:
// long jobs run in the background with a "working on ..." bar, so the app
// never looks hung). The worker walks the folder (core::scan_epubs), opens
// each EPUB for its title / author / cover (Covers), and hands progress and
// the finished list back to the main loop through a Glib::Dispatcher. The UI
// then makes the ONE undo step (core::library::add_folder) -- the worker never
// touches the library.
#include "core/Library.hpp"

#include <glibmm/dispatcher.h>
#include <sigc++/signal.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kaflip {

class Scanner {
public:
    Scanner();
    ~Scanner();   // asks the worker to stop and joins it

    bool busy() const { return m_busy; }
    bool start(const std::string& folder);   // false: already scanning

    sigc::signal<void(int done, int total, const std::string& now)>& signal_progress() { return m_progress; }
    sigc::signal<void(const std::string& folder, std::vector<core::LibItem>)>& signal_done() { return m_done; }

private:
    void on_dispatch();   // main thread

    Glib::Dispatcher            m_dispatch;
    std::thread                 m_thread;
    std::atomic<bool>           m_stop{false};
    bool                        m_busy = false;

    std::mutex                  m_mx;           // guards the fields below
    int                         m_done_n = 0, m_total = 0;
    std::string                 m_now, m_folder;
    bool                        m_finished = false;
    std::vector<core::LibItem>  m_found;

    sigc::signal<void(int, int, const std::string&)>                    m_progress;
    sigc::signal<void(const std::string&, std::vector<core::LibItem>)>  m_done;
};

}  // namespace kaflip
