#pragma once
// Covers -- a book's cover as a cached thumbnail (s006). GTK-side (gdk-pixbuf
// decodes and scales) but touches no widget, so it is safe on the scan thread.
//
// Thumbnails live under $XDG_CACHE_HOME/kaflip/covers/<core::cover_key>.png --
// keyed by path, size and mtime, so a replaced book gets a fresh one and an
// unchanged book is never decoded twice.
#include "core/Epub.hpp"
#include "core/Library.hpp"

#include <string>

namespace kaflip::covers {

std::string cache_dir();

// The cached thumbnail for an open book: made if missing. "" when the book has
// no cover or it would not decode (the library shows a title tile instead).
std::string thumbnail(const core::Book& book, const std::string& file);

// A library record for an open book (title falls back to the file's stem).
core::LibItem describe(const core::Book& book, const std::string& file);

}  // namespace kaflip::covers
