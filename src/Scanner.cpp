#include "Scanner.hpp"
#include "Covers.hpp"
#include "Log.hpp"

#include <filesystem>

namespace kaflip {

Scanner::Scanner() {
    m_dispatch.connect(sigc::mem_fun(*this, &Scanner::on_dispatch));
}

Scanner::~Scanner() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
}

bool Scanner::start(const std::string& folder) {
    if (m_busy) return false;
    if (m_thread.joinable()) m_thread.join();
    m_busy = true;
    m_stop = false;
    {
        std::lock_guard lk(m_mx);
        m_done_n = m_total = 0;
        m_now.clear();
        m_folder = folder;
        m_finished = false;
        m_found.clear();
    }
    if (auto lg = log::get(log::Area::Library)) lg->info("scan '{}': start", folder);
    m_thread = std::thread([this, folder] {
        const auto files = core::scan_epubs(folder);
        { std::lock_guard lk(m_mx); m_total = static_cast<int>(files.size()); }
        m_dispatch.emit();
        std::vector<core::LibItem> found;
        for (std::size_t i = 0; i < files.size() && !m_stop; ++i) {
            const std::string name = std::filesystem::path(files[i]).filename().string();
            { std::lock_guard lk(m_mx); m_now = name; }
            core::Book b;
            if (b.open(files[i])) found.push_back(covers::describe(b, files[i]));
            else if (auto lg = log::get(log::Area::Library)) lg->warn("scan: skip '{}': {}", files[i], b.error());
            { std::lock_guard lk(m_mx); m_done_n = static_cast<int>(i + 1); }
            m_dispatch.emit();
        }
        { std::lock_guard lk(m_mx); m_found = std::move(found); m_finished = true; }
        m_dispatch.emit();
    });
    return true;
}

void Scanner::on_dispatch() {
    int done, total;
    std::string now, folder;
    bool finished;
    std::vector<core::LibItem> found;
    {
        std::lock_guard lk(m_mx);
        done = m_done_n; total = m_total; now = m_now; folder = m_folder; finished = m_finished;
        if (finished) { found = std::move(m_found); m_finished = false; }
    }
    if (!m_busy) return;
    m_progress.emit(done, total, now);
    if (finished) {
        m_busy = false;
        if (m_thread.joinable()) m_thread.join();
        if (auto lg = log::get(log::Area::Library))
            lg->info("scan '{}': {} book(s)", folder, found.size());
        m_done.emit(folder, std::move(found));
    }
}

}  // namespace kaflip
