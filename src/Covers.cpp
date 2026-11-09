#include "Covers.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace kaflip::covers {

namespace fs = std::filesystem;

namespace {
// Exactly 2x the 150 x 225 tile, cropped to 2:3 from the centre: the library
// draws it pixel for pixel on a HiDPI screen.
constexpr int kThumbW = 300;
constexpr int kThumbH = 450;
}  // namespace

std::string cache_dir() {
    return (fs::path(g_get_user_cache_dir()) / "kaflip" / "covers").string();
}

std::string thumbnail(const core::Book& book, const std::string& file) {
    if (book.cover_path().empty()) return {};
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) return {};
    const auto mtime = fs::last_write_time(file, ec);
    if (ec) return {};
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count();
    const std::string out = (fs::path(cache_dir()) / core::cover_key(file, size, secs)).string();
    if (fs::exists(out, ec)) return out;

    auto bytes = book.read(book.cover_path());
    if (!bytes || bytes->empty()) return {};
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    GError* err = nullptr;
    const bool fed = gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar*>(bytes->data()),
                                             bytes->size(), &err);
    const bool closed = gdk_pixbuf_loader_close(loader, fed ? &err : nullptr);
    GdkPixbuf* pb = (fed && closed) ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
    std::string result;
    if (pb) {
        const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
        // Cover-fit: scale so both sides reach the box, then crop the centre.
        const double k = std::max(double(kThumbW) / w, double(kThumbH) / h);
        GdkPixbuf* small = gdk_pixbuf_new(GDK_COLORSPACE_RGB, gdk_pixbuf_get_has_alpha(pb), 8, kThumbW, kThumbH);
        if (small)
            gdk_pixbuf_scale(pb, small, 0, 0, kThumbW, kThumbH,
                             (kThumbW - w * k) / 2, (kThumbH - h * k) / 2, k, k, GDK_INTERP_BILINEAR);
        fs::create_directories(cache_dir(), ec);
        const std::string tmp = out + ".tmp";
        if (small && gdk_pixbuf_save(small, tmp.c_str(), "png", nullptr, nullptr)) {
            fs::rename(tmp, out, ec);
            if (!ec) result = out;
        }
        if (small) g_object_unref(small);
    }
    if (err) g_error_free(err);
    g_object_unref(loader);
    return result;
}

core::LibItem describe(const core::Book& book, const std::string& file) {
    core::LibItem it;
    it.kind   = core::LibItem::Kind::Book;
    it.path   = file;
    it.title  = book.meta().title.empty() ? fs::path(file).stem().string() : book.meta().title;
    it.author = book.meta().creators.empty() ? std::string{} : book.meta().creators.front();
    it.cover  = thumbnail(book, file);
    return it;
}

}  // namespace kaflip::covers
