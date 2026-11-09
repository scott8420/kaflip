#pragma once
// core::Progress -- how far through the whole book (s009). GTK-free.
//
// paginate.js counts a chapter's text as the UTF-16 length of every non-blank
// text node in <body> (not in script / style). text_length() counts the SAME
// way from the chapter's bytes, so a Place's character (from the page) and a
// chapter's length (from here) are in one unit and the book's percentage is
// plain arithmetic: the linear chapters before this one, plus how far into
// this one, over all the linear chapters.
#include <string>
#include <vector>

namespace kaflip::core {

// UTF-16 code units of the chapter's body text, as paginate.js counts it.
// Malformed XML -> 0 (the chapter then weighs nothing; the book still reads).
int text_length(const std::string& xhtml);

// 0..1. `through` = characters read in this chapter (the end of the page on
// screen). A non-linear chapter (a note) counts as where the linear text
// before it ended. Empty / zero-length books -> 0.
double book_progress(const std::vector<int>& lengths, const std::vector<bool>& linear,
                     int chapter, int through);

// "38%" -- floored, so 100% means the last page really is on screen.
std::string percent_text(double progress);

}  // namespace kaflip::core
