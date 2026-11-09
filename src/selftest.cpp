// kaflip_selftest -- the headless core harness.
//
// A seed is only trustworthy if its paradigms are EXERCISED, not just described
// (CANON: "a claim in the doc isn't a fact until a consumer exercises it"). This
// runs the GTK-free core with no display, so a future you can trust the pump and
// the mapping before wiring any widget on top:
//   * Recents  -- move-to-front + trim + the JSON round-trip with stale-prune;
//   * TextMap  -- forward(selection) -> inverse(map_range) round-trips byte-exact
//                 across single-line / multi-line / cross-paragraph / UTF-8, and
//                 chrome lines are never landed on.
//
//   cmake --build build --target kaflip_selftest && ./build/kaflip_selftest

#include "core/Recents.hpp"
#include "core/Shortcuts.hpp"
#include "core/Stones.hpp"
#include "core/Undo.hpp"
#include "core/TextMap.hpp"
#include "core/Zip.hpp"
#include "core/Epub.hpp"
#include "core/Paging.hpp"
#include "core/Tabs.hpp"
#include "core/Library.hpp"
#include "core/ReadingStyle.hpp"
#include <fstream>
#include <set>

#include <zlib.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace core = kaflip::core;

namespace {
int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { ++g_pass; std::cout << "  PASS  " << name; }
    else    { ++g_fail; std::cout << "  FAIL  " << name; }
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
}

// The FORWARD mapping, mirroring what a viewer's selection() would do: a
// (al,ac)-(cl,cc) mark -> V byte endpoints -> codepoint range + quote. Returns
// the byte endpoints too, so the round-trip through map_range can be exact.
struct Fwd { int cp_start, cp_end, bstart, bend; std::string quote, V; };
Fwd forward(const std::vector<core::FlatLine>& flat, int al, int ac, int cl, int cc) {
    std::vector<int> off;
    std::string V = core::visible_text(flat, off);
    int bstart = off[static_cast<std::size_t>(al)] +
                 std::min(ac, static_cast<int>(flat[static_cast<std::size_t>(al)].text.size()));
    int bend   = off[static_cast<std::size_t>(cl)] +
                 std::min(cc, static_cast<int>(flat[static_cast<std::size_t>(cl)].text.size()));
    std::string quote = V.substr(static_cast<std::size_t>(bstart),
                                 static_cast<std::size_t>(bend - bstart));
    int cps = core::utf8_length(V.substr(0, static_cast<std::size_t>(bstart)));
    int cpe = cps + core::utf8_length(quote);
    return {cps, cpe, bstart, bend, quote, V};
}

// Assert map_range inverts forward(): endpoints mapped back through byte_off
// reproduce the forward byte endpoints and reconstruct the same quote.
void roundtrip(const std::vector<core::FlatLine>& flat,
               int al, int ac, int cl, int cc, const std::string& what) {
    Fwd f = forward(flat, al, ac, cl, cc);
    std::vector<int> off;
    (void)core::visible_text(flat, off);
    core::LineCol s, e;
    const bool ok = core::map_range(flat, f.cp_start, f.cp_end, s, e);
    check("textmap: " + what + " maps", ok);
    if (!ok) return;
    const int rb0 = off[static_cast<std::size_t>(s.line)] + s.col;
    const int rb1 = off[static_cast<std::size_t>(e.line)] + e.col;
    check("textmap: " + what + " round-trips byte-exact",
          rb0 == f.bstart && rb1 == f.bend,
          std::to_string(rb0) + "," + std::to_string(rb1) +
          " vs " + std::to_string(f.bstart) + "," + std::to_string(f.bend));
    check("textmap: " + what + " reconstructs quote",
          f.V.substr(static_cast<std::size_t>(rb0),
                     static_cast<std::size_t>(rb1 - rb0)) == f.quote);
}

// ── A test-only zip WRITER (s002) ──────────────────────────────────────────
// The fixtures are built in code, so a test reads as the archive it checks and
// no binary file has to ride in the repo. method 0 = stored, 8 = deflate.
struct ZEnt { std::string name, data; int method = 8; };
void put16(std::string& o, unsigned v) { o += char(v & 0xff); o += char((v >> 8) & 0xff); }
void put32(std::string& o, unsigned long v) { put16(o, unsigned(v & 0xffff)); put16(o, unsigned((v >> 16) & 0xffff)); }
std::string deflate_raw(const std::string& in) {
    z_stream zs{};
    deflateInit2(&zs, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::string out(deflateBound(&zs, static_cast<uLong>(in.size())), '\0');
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    deflate(&zs, Z_FINISH);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
}
std::string make_zip(const std::vector<ZEnt>& ents, const std::string& comment = "") {
    std::string body, cd;
    for (const auto& e : ents) {
        const unsigned long crc = crc32(0L, reinterpret_cast<const Bytef*>(e.data.data()),
                                        static_cast<uInt>(e.data.size()));
        const std::string payload = e.method == 8 ? deflate_raw(e.data) : e.data;
        const unsigned long off = body.size();
        put32(body, 0x04034b50); put16(body, 20); put16(body, 0); put16(body, unsigned(e.method));
        put16(body, 0); put16(body, 0); put32(body, crc);
        put32(body, payload.size()); put32(body, e.data.size());
        put16(body, unsigned(e.name.size())); put16(body, 0);
        body += e.name; body += payload;
        put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, unsigned(e.method));
        put16(cd, 0); put16(cd, 0); put32(cd, crc);
        put32(cd, payload.size()); put32(cd, e.data.size());
        put16(cd, unsigned(e.name.size())); put16(cd, 0); put16(cd, 0);
        put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, off);
        cd += e.name;
    }
    std::string out = body + cd;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0);
    put16(out, unsigned(ents.size())); put16(out, unsigned(ents.size()));
    put32(out, cd.size()); put32(out, body.size());
    put16(out, unsigned(comment.size())); out += comment;
    return out;
}

// A small, well-formed EPUB 3: the OPF in OEBPS/ with a prefixed namespace
// (opf:), a chapter in a subfolder, a non-linear item, a dangling idref, a
// file name with a space, and an image.
const char* kContainer =
    "<?xml version=\"1.0\"?><container version=\"1.0\" "
    "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>"
    "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
    "</rootfiles></container>";
const char* kOpf =
    "<?xml version=\"1.0\"?><opf:package xmlns:opf=\"http://www.idpf.org/2007/opf\" "
    "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" version=\"3.0\">"
    "<opf:metadata><dc:title>Tapper  </dc:title><dc:creator>Scott</dc:creator>"
    "<dc:creator>Second Author</dc:creator><dc:language>en</dc:language>"
    "<dc:identifier>urn:test:1</dc:identifier></opf:metadata>"
    "<opf:manifest>"
    "<opf:item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
    "<opf:item id=\"c1\" href=\"Text/ch%201.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<opf:item id=\"c2\" href=\"Text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<opf:item id=\"note\" href=\"Text/note.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<opf:item id=\"img\" href=\"Images/cover.png\" media-type=\"image/png\" properties=\"cover-image\"/>"
    "<opf:item id=\"css\" href=\"Styles/book.css\" media-type=\"text/css\"/>"
    "</opf:manifest><opf:spine>"
    "<opf:itemref idref=\"c1\"/><opf:itemref idref=\"ghost\"/>"
    "<opf:itemref idref=\"c2\"/><opf:itemref idref=\"note\" linear=\"no\"/>"
    "</opf:spine></opf:package>";
const std::string kCh1 = "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>One. "
                         + std::string(400, 'x') + "</p></body></html>";
const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + std::string("\0\x01\x02\xff", 4);
std::vector<ZEnt> good_epub() {
    return {
        {"mimetype", "application/epub+zip", 0},
        {"META-INF/", "", 0},
        {"META-INF/container.xml", kContainer, 8},
        {"OEBPS/content.opf", kOpf, 8},
        {"OEBPS/nav.xhtml", "<html/>", 8},
        {"OEBPS/Text/ch 1.xhtml", kCh1, 8},
        {"OEBPS/Text/ch2.xhtml", "<html/>", 0},
        {"OEBPS/Text/note.xhtml", "<html/>", 8},
        {"OEBPS/Images/cover.png", kPng, 0},
        {"OEBPS/Styles/book.css", "p{}", 8},
    };
}
}  // namespace

int main() {
    std::cout << "kaflip_selftest -- recents pump + textmap mapping\n";

    // ── Undo -- the one door, two pictures, gestures, merging (s046) ──────────
    {
        std::cout << "\n-- undo --\n";
        using core::Stones;
        using core::Stone;
        using Clock = core::StoneJournal::Clock;
        using SnapMap = std::vector<std::pair<std::string, Stone>>;
        const auto snap = [](const Stones& s) {
            SnapMap out;
            for (const auto& id : s.ids()) out.emplace_back(id, *s.get(id));
            return out;
        };
        const auto names = [](const Stones& s) {
            std::string out;
            for (const auto& id : s.ids()) out += s.get(id)->name + ",";
            return out;
        };
        // A fixture: three stones, LOADED (not a user choice -- not undoable).
        struct Fix {
            Stones s;
            core::StoneJournal j{s};
            std::vector<std::string> sel;
            Fix() {
                s.set_door([this] { return j.open(); });
                j.set_selection_source([this] { return sel; });
                Stones::Loading load(s);
                for (const char* n : {"Ruby", "Jade", "Onyx"}) {
                    const auto id = s.fresh_id();
                    s.put(id, Stone{n, 3, s.pos_after({})});
                }
            }
        };

        // THE GATE. Every verb the page offers, from a fresh fixture: nothing
        // written with the door shut; at least one step; undoing them all puts
        // the model back exactly; redoing them all reproduces the result.
        for (const auto& v : core::stones::verbs()) {
            Fix f;
            const auto first = f.s.ids().front();
            const auto before = snap(f.s);
            v.run(f.j, f.s, first);
            const auto after = snap(f.s);
            const auto steps = f.j.size();
            check("gate/" + v.name + ": through the one door", f.s.strays() == 0,
                  std::to_string(f.s.strays()) + " stray write(s)");
            check("gate/" + v.name + ": is a step", steps > 0 && !(after == before));
            for (std::size_t i = 0; i < steps; ++i) f.j.undo();
            check("gate/" + v.name + ": undo puts it back exactly", snap(f.s) == before);
            for (std::size_t i = 0; i < steps; ++i) f.j.redo();
            check("gate/" + v.name + ": redo reproduces it", snap(f.s) == after);
        }
        {   // ...and the gate bites: a write around the door is counted.
            Fix f;
            const auto id = f.s.ids().front();
            f.s.put(id, Stone{"sneaky", 3, 0.5});
            check("gate: a write around the door is a stray", f.s.strays() == 1);
        }

        {   // Delete comes back whole, in its place, under its id, selected.
            Fix f;
            const auto ids = f.s.ids();
            f.sel = {ids[1]};
            core::stones::remove(f.j, f.s, ids[1]);
            f.sel = {ids[2]};      // the UI selects the neighbour...
            f.j.reselect(f.sel);   // ...and tells the step
            check("delete: gone", names(f.s) == "Ruby,Onyx,", names(f.s));
            check("delete: the label names it", f.j.undo_label() == "Delete “Jade”", f.j.undo_label());
            const auto r = f.j.undo();
            check("delete: undo -- back in its place", names(f.s) == "Ruby,Jade,Onyx,", names(f.s));
            check("delete: undo -- the same id", f.s.ids()[1] == ids[1]);
            check("delete: undo hands back its selection", r.selection == std::vector<std::string>{ids[1]});
            const auto r2 = f.j.redo();
            check("delete: redo -- gone again, the neighbour selected",
                  names(f.s) == "Ruby,Onyx," && r2.selection == std::vector<std::string>{ids[2]});
        }

        {   // Created records: undo removes, redo brings back the same id.
            Fix f;
            const auto first = f.s.ids()[0];
            const auto made = core::stones::add(f.j, f.s, "Opal", first);
            check("add: just after the one given", names(f.s) == "Ruby,Opal,Jade,Onyx,", names(f.s));
            f.j.undo();
            check("add: undone", !f.s.get(made));
            f.j.redo();
            check("add: redone, same id, same place", f.s.get(made) && names(f.s) == "Ruby,Opal,Jade,Onyx,");
        }

        {   // A no-op is not a step.
            Fix f;
            const auto id = f.s.ids()[0];
            core::stones::rename(f.j, f.s, id, "Ruby");
            core::stones::remove(f.j, f.s, "nope");
            core::stones::move_up(f.j, f.s, id);   // already first
            check("no-op: nothing kept", f.j.size() == 0 && !f.j.can_undo());
        }

        {   // Merging: arrows on the slider fold into one step inside the window.
            Fix f;
            const auto id = f.s.ids()[0];
            const auto t0 = Clock::now();
            core::stones::resize(f.j, f.s, id, 4);
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 5; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::milliseconds(400));
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 6; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::milliseconds(800));
            check("merge: three presses, one step", f.j.size() == 1, std::to_string(f.j.size()));
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 7; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::seconds(10));
            check("merge: after the window, a new step", f.j.size() == 2);
            f.j.undo();
            f.j.undo();
            check("merge: two undos, back to the start", f.s.get(id)->size == 3);
            // Another stone's key never merges into this one's step.
            Fix g;
            const auto a = g.s.ids()[0], b = g.s.ids()[1];
            core::stones::resize(g.j, g.s, a, 5);
            core::stones::resize(g.j, g.s, b, 5);
            check("merge: a different key is a different step", g.j.size() == 2);
            // Nor into a step that was undone.
            core::stones::resize(g.j, g.s, b, 6);
            g.j.undo();
            core::stones::resize(g.j, g.s, b, 7);
            check("merge: never into an undone step", g.j.size() == 2 && g.j.can_undo() && !g.j.can_redo());
            // Back to where it started inside the window: the step goes away.
            Fix h;
            const auto c = h.s.ids()[0];
            core::stones::resize(h.j, h.s, c, 4);
            core::stones::resize(h.j, h.s, c, 3);
            check("merge: there and back again is no step", h.j.size() == 0);
        }

        {   // A gesture is one step, however many writes the drag made.
            Fix f;
            const auto id = f.s.ids()[0];
            core::stones::drag_begin(f.j, f.s, id);
            for (int n = 4; n <= 9; ++n) core::stones::drag_to(f.j, f.s, id, n);
            check("gesture: open while dragging", f.j.in_gesture() && f.j.size() == 0);
            core::stones::drag_end(f.j);
            check("gesture: a six-write drag is one step", f.j.size() == 1 && f.s.get(id)->size == 9);
            f.j.undo();
            check("gesture: one undo, back to the start", f.s.get(id)->size == 3);
            // Escape mid-drag: back, and nothing kept.
            core::stones::drag_begin(f.j, f.s, id);
            core::stones::drag_to(f.j, f.s, id, 7);
            f.j.cancel();
            check("gesture: cancel puts it back and keeps nothing",
                  f.s.get(id)->size == 3 && !f.j.in_gesture() && f.j.can_redo());
            // Ctrl+Z mid-drag: the drag becomes a step, then is undone.
            core::stones::drag_begin(f.j, f.s, id);
            core::stones::drag_to(f.j, f.s, id, 2);
            f.j.undo();
            check("gesture: undo mid-drag closes the drag, then undoes it",
                  !f.j.in_gesture() && f.s.get(id)->size == 3 && f.j.can_redo());
            check("gesture: no stray writes", f.s.strays() == 0);
        }

        {   // A new step forgets the redo branch; Move keeps both stones in one step.
            Fix f;
            const auto ids = f.s.ids();
            core::stones::move_down(f.j, f.s, ids[0]);
            check("move: one step for both stones", f.j.size() == 1 && names(f.s) == "Jade,Ruby,Onyx,", names(f.s));
            f.j.undo();
            check("move: undone", names(f.s) == "Ruby,Jade,Onyx,");
            core::stones::rename(f.j, f.s, ids[2], "Coal");
            check("redo branch: a new step forgets it", !f.j.can_redo() && f.j.size() == 1);
        }

        {   // The cap: the oldest step falls off; the newest still undoes.
            Fix f;
            const auto id = f.s.ids()[0];
            for (std::size_t i = 0; i < core::StoneJournal::kMax + 20; ++i)
                core::stones::rename(f.j, f.s, id, "n" + std::to_string(i));
            check("cap: kMax steps kept", f.j.size() == core::StoneJournal::kMax);
            f.j.undo();
            check("cap: the newest still undoes", f.s.get(id)->name == "n" + std::to_string(core::StoneJournal::kMax + 18));
        }

        {   // on_change: the UI hears every journal change (labels, greying).
            Fix f;
            int heard = 0;
            f.j.set_on_change([&] { ++heard; });
            core::stones::rename(f.j, f.s, f.s.ids()[0], "X");
            f.j.undo();
            f.j.redo();
            f.j.clear();
            check("on_change: step, undo, redo, clear -- four", heard == 4, std::to_string(heard));
        }
    }
    // ── Zip (s002) ───────────────────────────────────────────────────────────
    {
        std::cout << "\n-- zip --\n";
        const std::string big(5000, 'q');
        core::Zip z;
        const bool ok = z.open_bytes(make_zip({{"a.txt", "hello", 0}, {"dir/", "", 0},
                                               {"dir/b.txt", big, 8}}, "a trailing comment"));
        check("zip: opens (with an archive comment)", ok, z.error());
        check("zip: directory entries are skipped", z.entries().size() == 2);
        auto a = z.read("a.txt");
        check("zip: stored entry reads back", a && *a == "hello");
        auto b = z.read("dir/b.txt");
        check("zip: deflated entry inflates back", b && *b == big);
        check("zip: deflate really compressed it", z.find("dir/b.txt")->comp_size < 200);
        check("zip: a missing entry is nullopt + error", !z.read("nope") && !z.error().empty());

        std::string bad = make_zip({{"a.txt", "hello", 0}});
        bad[30 + 5] = 'j';   // flip a payload byte; the CRC no longer matches
        core::Zip zb;
        check("zip: corrupt bytes still open", zb.open_bytes(bad));
        check("zip: ...but fail the CRC on read", !zb.read("a.txt") &&
              zb.error().find("CRC") != std::string::npos, zb.error());

        std::string odd = make_zip({{"a.txt", "hello", 0}});
        odd[8] = 12;   // local header method -> bzip2; the CD still says stored
        const auto cdm = odd.find("PK\x01\x02");
        odd[cdm + 10] = 12;
        core::Zip zo;
        zo.open_bytes(odd);
        check("zip: an unsupported method is reported", !zo.read("a.txt") &&
              zo.error().find("method 12") != std::string::npos, zo.error());

        core::Zip zn;
        check("zip: plain text is not a zip", !zn.open_bytes(std::string(100, 'z')));
        const std::string full = make_zip({{"a.txt", "hello", 8}});
        check("zip: a truncated archive is refused", !zn.open_bytes(full.substr(0, full.size() - 10)));
    }

    // ── EPUB paths + the URI scheme (s002) ────────────────────────────────────
    {
        std::cout << "\n-- epub paths --\n";
        check("path: normalize folds . and ..",
              core::normalize_path("OEBPS/./Text/../Images//a.png") == "OEBPS/Images/a.png");
        check("path: normalize never climbs above the root", core::normalize_path("../../a") == "a");
        check("path: dir_of", core::dir_of("OEBPS/Text/c.xhtml") == "OEBPS/Text/" && core::dir_of("c") == "");
        check("href: relative to the file's folder",
              core::resolve_href("OEBPS/content.opf", "Text/c1.xhtml") == "OEBPS/Text/c1.xhtml");
        check("href: .. climbs out of a subfolder",
              core::resolve_href("OEBPS/Text/c1.xhtml", "../Images/a.png") == "OEBPS/Images/a.png");
        check("href: fragment and query are stripped",
              core::resolve_href("OEBPS/Text/c1.xhtml", "c2.xhtml?x=1#sec") == "OEBPS/Text/c2.xhtml");
        check("href: a bare #frag is the same file",
              core::resolve_href("OEBPS/Text/c1.xhtml", "#top") == "OEBPS/Text/c1.xhtml");
        check("href: percent escapes decode",
              core::resolve_href("OPS/a.xhtml", "ch%201.xhtml") == "OPS/ch 1.xhtml");
        check("href: http and mailto are outside the book",
              core::resolve_href("a.xhtml", "https://x.org/a").empty() &&
              core::resolve_href("a.xhtml", "mailto:s@x.org").empty());
        check("encode/decode round-trip",
              core::percent_decode(core::percent_encode_path("OEBPS/ch 1 é#.xhtml")) == "OEBPS/ch 1 é#.xhtml");
        const std::string u = core::book_uri(7, "OEBPS/Text/ch 1.xhtml");
        check("uri: built", u == "kaflip-book://b7/OEBPS/Text/ch%201.xhtml", u);
        auto pu = core::parse_book_uri(u + "#sec2");
        check("uri: parses back (token, path, no fragment)",
              pu && pu->token == 7 && pu->path == "OEBPS/Text/ch 1.xhtml");
        check("uri: other schemes and bad hosts are refused",
              !core::parse_book_uri("https://b7/a") && !core::parse_book_uri("kaflip-book://x7/a") &&
              !core::parse_book_uri("kaflip-book://b/a"));
        check("media type from the extension",
              core::media_type_for_ext("a/B.XHTML") == "application/xhtml+xml" &&
              core::media_type_for_ext("f.woff2") == "font/woff2" &&
              core::media_type_for_ext("noext") == "application/octet-stream");
    }

    // ── EPUB: container -> OPF -> metadata, manifest, spine (s002) ────────────
    {
        std::cout << "\n-- epub --\n";
        core::Book b;
        const bool ok = b.open_bytes(make_zip(good_epub()));
        check("epub: opens", ok, b.error());
        check("epub: OPF found through container.xml", b.opf_path() == "OEBPS/content.opf");
        check("epub: title (trimmed, prefixed dc:)", b.meta().title == "Tapper", b.meta().title);
        check("epub: both creators, in order", b.meta().creators ==
              std::vector<std::string>{"Scott", "Second Author"});
        check("epub: language + identifier", b.meta().language == "en" &&
              b.meta().identifier == "urn:test:1");
        check("epub: manifest has all six items", b.manifest().size() == 6);
        check("epub: spine skips the dangling idref", b.spine().size() == 3);
        check("epub: spine order + resolved, decoded paths",
              b.spine().size() == 3 && b.spine()[0].path == "OEBPS/Text/ch 1.xhtml" &&
              b.spine()[1].path == "OEBPS/Text/ch2.xhtml");
        check("epub: linear=no is kept and marked",
              b.spine().size() == 3 && !b.spine()[2].linear && b.spine()[0].linear);
        check("epub: spine_index_of ignores the fragment",
              b.spine_index_of("OEBPS/Text/ch2.xhtml#p3") == 1 && b.spine_index_of("OEBPS/nav.xhtml") == -1);
        auto c1 = b.read(b.spine()[0].path);
        check("epub: a chapter reads back byte-exact", c1 && *c1 == kCh1);
        auto png = b.read("OEBPS/Images/cover.png");
        check("epub: binary (an image) reads back byte-exact", png && *png == kPng);
        check("epub: media type from the manifest", b.media_type_of("OEBPS/Images/cover.png") == "image/png");
        check("epub: media type falls back to the extension",
              b.media_type_of("OEBPS/Fonts/x.otf") == "font/otf");
        const std::string uri = core::book_uri(3, b.spine()[0].path);
        auto back = core::parse_book_uri(uri);
        check("epub: spine -> uri -> spine round-trips",
              back && b.spine_index_of(back->path) == 0, uri);

        // Failures are reported, never a half-open book.
        auto without = [](const std::string& drop) {
            std::vector<ZEnt> v;
            for (auto& e : good_epub()) if (e.name != drop) v.push_back(e);
            return v;
        };
        core::Book f1;
        check("epub: no container.xml -> refused",
              !f1.open_bytes(make_zip(without("META-INF/container.xml"))) && !f1.error().empty(), f1.error());
        core::Book f2;
        check("epub: container names a missing OPF -> refused",
              !f2.open_bytes(make_zip(without("OEBPS/content.opf"))), f2.error());
        auto v3 = without("OEBPS/content.opf");
        v3.push_back({"OEBPS/content.opf", "<package><metadata>", 8});
        core::Book f3;
        check("epub: malformed OPF -> refused with the reason",
              !f3.open_bytes(make_zip(v3)) && f3.error().find("well-formed") != std::string::npos, f3.error());
        auto v4 = without("OEBPS/content.opf");
        v4.push_back({"OEBPS/content.opf", "<package><manifest/><spine/></package>", 8});
        core::Book f4;
        check("epub: empty spine -> refused",
              !f4.open_bytes(make_zip(v4)) && f4.error().find("empty spine") != std::string::npos, f4.error());
        core::Book f5;
        check("epub: a missing file on disk -> refused", !f5.open("/nonexistent/x.epub"));
    }
    // ── Paging: turns, the column sheet, the page's report (s003) ────────────
    {
        std::cout << "\n-- paging --\n";
        using K = core::Turn::Kind;
        using core::Land;
        auto fw = core::turn_forward, bk = core::turn_back;
        check("turn: forward within the chapter", fw(2, 5, 7) == core::Turn{K::Page, 3, Land::First});
        check("turn: forward off the last page -> next chapter, first page",
              fw(4, 5, 7) == core::Turn{K::Chapter, 7, Land::First});
        check("turn: forward at the end of the book does nothing", fw(4, 5, -1).kind == K::None);
        check("turn: back within the chapter", bk(3, 5, 1) == core::Turn{K::Page, 2, Land::First});
        check("turn: back off page one -> previous chapter, LAST page",
              bk(0, 5, 1) == core::Turn{K::Chapter, 1, Land::Last});
        check("turn: back at the start of the book does nothing", bk(0, 5, -1).kind == K::None);
        check("turn: a one-page chapter hands off both ways",
              fw(0, 1, 4).kind == K::Chapter && bk(0, 1, 2).kind == K::Chapter);
        check("turn: nothing while the page has not reported (count 0)",
              fw(0, 0, 4).kind == K::None && bk(0, 0, 2).kind == K::None);

        const std::string css = core::pagination_css({48, 40, 760});
        const std::string X = "max(48px, calc((100vw - 760px) / 2))";
        check("css: columns one viewport apart (width = 100vw - 2x, gap = 2x)",
              css.find("column-width: calc(100vw - calc(2 * " + X + "))") != std::string::npos &&
              css.find("column-gap: calc(2 * " + X + ")") != std::string::npos, css);
        check("css: body padding is y x, x growing past the line cap",
              css.find("padding: 40px " + X + " !important") != std::string::npos);
        check("css: the root does not scroll", css.find("overflow: hidden !important") != std::string::npos);
        check("css: images held inside one page",
              css.find("max-height: calc(100vh - 80px)") != std::string::npos);

        const std::string spread = core::pagination_css({48, 40, 760, true});
        check("css: a spread is two columns, the cap per page (50vw)",
              spread.find("column-count: 2 !important") != std::string::npos &&
              spread.find("calc((50vw - 760px) / 2)") != std::string::npos, spread);

        auto r = core::parse_page_report("3/11");
        check("report: parses", r && r->page == 3 && r->count == 11);
        check("report: junk refused",
              !core::parse_page_report("") && !core::parse_page_report("3") &&
              !core::parse_page_report("/4") && !core::parse_page_report("3/") &&
              !core::parse_page_report("a/4") && !core::parse_page_report("3/4x") &&
              !core::parse_page_report("-1/4"));
        check("report: impossible numbers refused",
              !core::parse_page_report("0/0") && !core::parse_page_report("4/4"));
    }
    // ── Tabs: the rail's one list (s004) ──────────────────────────────────────
    {
        std::cout << "\n-- tabs --\n";
        std::set<std::string> ids;
        bool shaped = true;
        for (const auto& t : core::tab_registry()) {
            ids.insert(std::string(t.id));
            if (t.title.empty() || t.icon.empty() || t.css.empty()) shaped = false;
            if (std::string(t.icon).rfind("kaflip-tab-", 0) != 0 ||
                std::string(t.icon).find("-symbolic") == std::string::npos) shaped = false;
            if (std::string(t.css).rfind("tab-", 0) != 0) shaped = false;
        }
        check("tabs: ids unique", ids.size() == core::tab_registry().size());
        check("tabs: every page has a title, a bundled symbolic glyph and a colour class", shaped);
        check("tabs: the Library is first", !core::tab_registry().empty() && core::tab_registry().front().id == "library");
        check("tabs: find_tab", core::find_tab("recents") && !core::find_tab("nope"));
    }
    // ── EPUB covers (s006) ────────────────────────────────────────────────────
    {
        std::cout << "\n-- covers --\n";
        core::Book b;
        b.open_bytes(make_zip(good_epub()));
        check("cover: EPUB 3 properties=cover-image", b.cover_path() == "OEBPS/Images/cover.png", b.cover_path());
        auto with_opf = [](const std::string& opf) {
            std::vector<ZEnt> v;
            for (auto& e : good_epub()) if (e.name != "OEBPS/content.opf") v.push_back(e);
            v.push_back({"OEBPS/content.opf", opf, 8});
            return v;
        };
        const std::string epub2 =
            "<package><metadata><meta name=\"cover\" content=\"pic\"/></metadata><manifest>"
            "<item id=\"c1\" href=\"Text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "<item id=\"pic\" href=\"Images/cover.png\" media-type=\"image/png\"/>"
            "</manifest><spine><itemref idref=\"c1\"/></spine></package>";
        core::Book b2;
        b2.open_bytes(make_zip(with_opf(epub2)));
        check("cover: EPUB 2 <meta name=cover>", b2.cover_path() == "OEBPS/Images/cover.png", b2.cover_path());
        const std::string named =
            "<package><manifest>"
            "<item id=\"c1\" href=\"Text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "<item id=\"i9\" href=\"Images/Cover.png\" media-type=\"image/png\"/>"
            "</manifest><spine><itemref idref=\"c1\"/></spine></package>";
        core::Book b3;
        b3.open_bytes(make_zip(with_opf(named)));
        check("cover: an image named cover", b3.cover_path() == "OEBPS/Images/Cover.png", b3.cover_path());
        const std::string none =
            "<package><manifest><item id=\"c1\" href=\"Text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "</manifest><spine><itemref idref=\"c1\"/></spine></package>";
        core::Book b4;
        b4.open_bytes(make_zip(with_opf(none)));
        check("cover: none -> empty", b4.cover_path().empty());
    }

    // ── Library: store, verbs through the door, JSON, scan (s006) ─────────────
    {
        std::cout << "\n-- library --\n";
        using core::Library;
        using core::LibItem;
        using K = LibItem::Kind;
        struct Fix {
            Library l;
            core::LibraryJournal j{l};
            Fix() {
                l.set_door([this] { return j.open(); });
                core::library::note_opened(l, LibItem{K::Book, "/books/one.epub", "One", "Scott"});
                core::library::note_opened(l, LibItem{K::Book, "/books/two.epub", "Two"});
            }
        };
        const auto snap = [](const Library& l) { return l.to_json(); };

        // The gate: every verb through the one door, a step, undo exact, redo exact.
        for (const auto& v : core::library::verbs()) {
            Fix f;
            const auto before = snap(f.l);
            v.run(f.j, f.l);
            const auto after = snap(f.l);
            check("library gate/" + v.name + ": through the one door", f.l.strays() == 0);
            check("library gate/" + v.name + ": is one step", f.j.size() == 1 && after != before);
            f.j.undo();
            check("library gate/" + v.name + ": undo puts it back exactly", snap(f.l) == before);
            f.j.redo();
            check("library gate/" + v.name + ": redo reproduces it", snap(f.l) == after);
        }
        {
            Fix f;
            check("library: opening is not a step", f.j.size() == 0 && f.l.books().size() == 2 && f.l.strays() == 0);
            f.l.put(Library::book_id("/x.epub"), LibItem{K::Book, "/x.epub"});
            check("library: a write around the door is a stray", f.l.strays() == 1);
        }
        {
            Fix f;
            core::library::add_folder(f.j, f.l, "/shelf",
                {LibItem{K::Book, "/shelf/z.epub", "zebra"}, LibItem{K::Book, "/books/one.epub", "One again"},
                 LibItem{K::Book, "/shelf/a.epub", "Aardvark"}});
            const auto bs = f.l.books();
            std::string titles;
            for (const auto& b : bs) titles += b.title + ",";
            check("library: add folder brings its books, sorted by title (any case)",
                  titles == "Aardvark,One,Two,zebra,", titles);
            check("library: a book already there keeps its own record",
                  f.l.get(Library::book_id("/books/one.epub"))->folder.empty());
            check("library: the step's label names the folder", f.j.undo_label() == "Add folder “shelf”", f.j.undo_label());
            check("library: the same folder twice is no step",
                  !core::library::add_folder(f.j, f.l, "/shelf", {}) && f.j.size() == 1);
            f.j.undo();
            check("library: one undo takes the folder and its books away",
                  f.l.books().size() == 2 && f.l.folders().empty());
        }
        {
            Fix f;
            core::library::remove_book(f.j, f.l, "/books/two.epub");
            check("library: remove names the book", f.j.undo_label() == "Remove “Two”", f.j.undo_label());
            check("library: removing a book that is not there is no step",
                  !core::library::remove_book(f.j, f.l, "/books/two.epub") && f.j.size() == 1);
        }
        {
            Fix f;
            core::library::note_opened(f.l, LibItem{K::Book, "/books/one.epub", "One (2nd ed)", "Scott", "c.png"});
            const auto one = f.l.get(Library::book_id("/books/one.epub"));
            check("library: reopening refreshes title and cover, keeps its place",
                  one && one->title == "One (2nd ed)" && one->cover == "c.png" && one->added == 1);
        }
        {
            Fix f;
            core::library::add_folder(f.j, f.l, "/shelf", {LibItem{K::Book, "/shelf/a.epub", "A"}});
            Library back;
            check("library: JSON round-trips", back.from_json(f.l.to_json()) && back.to_json() == f.l.to_json());
            check("library: junk JSON is refused", !back.from_json("{nope"));
            const auto dir = std::filesystem::temp_directory_path() / "kaflip_selftest_lib";
            std::filesystem::remove_all(dir);
            const std::string file = (dir / "library.json").string();
            check("library: save", f.l.save(file));
            Library disk;
            check("library: load", disk.load(file) && disk.to_json() == f.l.to_json());
            Library none;
            check("library: a missing file is an empty library", none.load((dir / "nope.json").string()) && none.size() == 0);

            // scan
            const auto root = dir / "scan";
            std::filesystem::create_directories(root / "Author" / "Title");
            std::filesystem::create_directories(root / ".hidden");
            for (auto p : {root / "b.epub", root / "Author" / "Title" / "a.EPUB",
                           root / ".hidden" / "x.epub", root / "notes.txt"})
                std::ofstream(p.string()) << "x";
            const auto found = core::scan_epubs(root.string());
            check("scan: every .epub (any case), recursively, hidden skipped, sorted",
                  found.size() == 2 && found[0] == (root / "Author" / "Title" / "a.EPUB").string() &&
                  found[1] == (root / "b.epub").string());
            check("scan: a missing folder is empty, not fatal", core::scan_epubs((dir / "gone").string()).empty());
            std::filesystem::remove_all(dir);
        }
        check("cover key: stable", core::cover_key("/a.epub", 10, 5) == core::cover_key("/a.epub", 10, 5));
        check("cover key: changes with the file",
              core::cover_key("/a.epub", 10, 5) != core::cover_key("/a.epub", 11, 5) &&
              core::cover_key("/a.epub", 10, 5) != core::cover_key("/a.epub", 10, 6) &&
              core::cover_key("/a.epub", 10, 5) != core::cover_key("/b.epub", 10, 5));
        check("cover key: a .png name", core::cover_key("/a", 1, 1).size() == 20);
    }
    // ── Reading style (s007) ──────────────────────────────────────────────────
    {
        std::cout << "\n-- reading style --\n";
        using core::ReadingStyle;
        using core::Override;
        const auto has = [](const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; };
        std::string names;
        for (const auto& p : core::presets()) names += p.preset + ",";
        check("presets: Day, Sepia, Night, Paper", names == "Day,Sepia,Night,Paper,", names);
        check("presets: Night forces its colours (Everything)", core::find_preset("Night")->level == Override::Everything);

        ReadingStyle s;   // Day, Fonts
        s.body_font = "Literata";
        const std::string css = core::style_css(s);
        check("style: the page ground and base text are always ours",
              has(css, "html { background: #ffffff !important;") && has(css, "color: #1f2328 !important;"));
        check("style: size scales the root", has(css, "font-size: 100% !important"));
        check("style: Fonts level forces the body font onto the book's elements",
              has(css, "font-family: \"Literata\" !important;") && has(css, "body p, body li"));
        check("style: Fonts level leaves element colours to the book", !has(css, "color: inherit !important"));
        ReadingStyle pub = s;
        pub.level = Override::Publisher;
        const std::string pcss = core::style_css(pub);
        check("style: Publisher level -- our font is only a default",
              has(pcss, "font-family: \"Literata\";") && !has(pcss, "font-family: \"Literata\" !important"));
        ReadingStyle all = s;
        all.level = Override::Everything;
        check("style: Everything clears the book's element colours",
              has(core::style_css(all), "body * { background-color: transparent !important; color: inherit !important; }"));
        ReadingStyle para = s;
        para.paras = core::Paras::Indent; para.justify = core::Justify::Full; para.hyphens = true;
        const std::string pa = core::style_css(para);
        check("style: indented paragraphs, first one flush",
              has(pa, "text-indent: 1.5em !important") && has(pa, "body h1 + p"));
        check("style: justified + hyphenated", has(pa, "text-align: justify !important") && has(pa, "hyphens: auto !important"));
        check("style: book_css = the columns + the style",
              has(core::book_css(s), "column-fill: auto") && has(core::book_css(s), "font-family: \"Literata\""));

        ReadingStyle bad = s;
        bad.body_font = "x\"; } body { display:none"; bad.page = "red"; bad.size_pct = 999; bad.line_height = 0.2;
        const ReadingStyle clean = core::sanitized(bad);
        check("sanitize: an unsafe font name is dropped", clean.body_font.empty());
        check("sanitize: a bad colour falls back, numbers clamp",
              clean.page == "#ffffff" && clean.size_pct == 220 && clean.line_height == 1.0);

        ReadingStyle j = *core::find_preset("Paper");
        j.size_pct = 115; j.spread = true; j.head_font = "Inter";
        auto back = core::style_from_json(core::style_to_json(j));
        check("style JSON round-trips", back && *back == j);
        check("style JSON: missing fields keep defaults", core::style_from_json("{}") && *core::style_from_json("{}") == ReadingStyle{});
        check("style JSON: junk refused", !core::style_from_json("[1,2"));

        // the store + the gate
        core::Styles st;
        check("styles: defaults are Day for reading, Night for focus",
              st.at(core::Styles::kReading).preset == "Day" && st.at(core::Styles::kFocus).preset == "Night");
        for (const auto& v : core::styles::verbs()) {
            core::Styles s2;
            core::StyleJournal jn{s2};
            s2.set_door([&] { return jn.open(); });
            const auto before = s2.to_json();
            v.run(jn, s2);
            const auto after = s2.to_json();
            check("style gate/" + v.name + ": through the door", s2.strays() == 0);
            check("style gate/" + v.name + ": one step", jn.size() == 1 && after != before);
            jn.undo();
            check("style gate/" + v.name + ": undo exact", s2.to_json() == before);
            jn.redo();
            check("style gate/" + v.name + ": redo exact", s2.to_json() == after);
        }
        {
            core::Styles s3;
            core::StyleJournal jn{s3};
            s3.set_door([&] { return jn.open(); });
            const auto t0 = core::StyleJournal::Clock::now();
            ReadingStyle n = s3.at(core::Styles::kReading);
            for (int i = 1; i <= 5; ++i) {
                n.size_pct = 100 + i * 5;
                jn.run("Text size", {core::Styles::kReading}, [&] { s3.put(core::Styles::kReading, n); },
                       "reading:size", t0 + std::chrono::milliseconds(i * 100));
            }
            check("styles: a slider drag merges into one step", jn.size() == 1 &&
                  s3.at(core::Styles::kReading).size_pct == 125);
            check("styles: the same preset twice is no step",
                  core::styles::apply_preset(jn, s3, core::Styles::kReading, "Night") &&
                  !core::styles::apply_preset(jn, s3, core::Styles::kReading, "Night"));
            core::Styles back2;
            check("styles: store JSON round-trips", back2.from_json(s3.to_json()) && back2.to_json() == s3.to_json());
        }
    }

    std::cout << "-----------------------------------------------\n";

    // -- Recents: list ops + JSON round-trip with prune -----------------------
    {
        std::vector<std::string> l;
        core::recents_add(l, "/a");
        core::recents_add(l, "/b");
        core::recents_add(l, "/c");
        check("recents: newest first", l.size() == 3 && l.front() == "/c");
        core::recents_add(l, "/a");
        check("recents: re-add moves to front (no dup)",
              l.size() == 3 && l.front() == "/a");
        core::recents_add(l, "/x/");
        check("recents: trailing slash normalised",
              std::find(l.begin(), l.end(), "/x") != l.end() &&
              std::find(l.begin(), l.end(), "/x/") == l.end());
        std::vector<std::string> cap;
        for (int i = 0; i < 20; ++i)
            core::recents_add(cap, "/p" + std::to_string(i), 8);
        check("recents: trim to max (8)", cap.size() == 8 && cap.front() == "/p19");

        const std::string base =
            (std::filesystem::temp_directory_path() / "kaflip_selftest_recents").string();
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
        const std::string live = base + "/live";
        std::filesystem::create_directories(live, ec);
        const std::string file = base + "/recent.json";

        std::vector<std::string> to_save = {live, base + "/GONE"};
        check("recents: save ok", core::save_recents(file, to_save));
        auto loaded = core::load_recents(file);
        check("recents: load prunes the missing path",
              loaded.size() == 1 && loaded.front() == live);
        check("recents: missing file -> empty",
              core::load_recents(base + "/nope.json").empty());
    }

    // -- TextMap: forward/inverse round-trip ----------------------------------
    {
        // A scene as a viewer flattens it:
        //   0 "The Cellar"     title  (non-prose)
        //   1 ""               gap    (non-prose)
        //   2 "She opened the" prose, para_start   <- para 1, wrap 1
        //   3 "old iron door." prose               <- para 1, wrap 2
        //   4 "Caf\u00e9 dust." prose, para_start  <- para 2 (UTF-8: é)
        std::vector<core::FlatLine> flat = {
            {"The Cellar",     false, false},
            {"",               false, false},
            {"She opened the", true,  true },
            {"old iron door.", true,  false},
            {"Caf\xC3\xA9 dust.", true, true },
        };
        roundtrip(flat, 2, 0, 2, 14, "single line");
        roundtrip(flat, 2, 4, 3, 8,  "multi-line (soft wrap)");
        roundtrip(flat, 2, 0, 4, 10, "cross-paragraph");
        roundtrip(flat, 4, 0, 4, 6,  "UTF-8 span (accented)");

        // Chrome is never landed on: a range covering all prose starts on line 2.
        std::vector<int> off;
        (void)core::visible_text(flat, off);
        core::LineCol s, e;
        core::map_range(flat, 0, 3, s, e);
        check("textmap: start snaps to prose, never chrome", s.line == 2);

        // Degenerate / empty inputs fail cleanly.
        check("textmap: empty range rejected", !core::map_range(flat, 5, 5, s, e));
        std::vector<core::FlatLine> no_prose = {{"Title", false, false}};
        check("textmap: no prose -> false", !core::map_range(no_prose, 0, 3, s, e));
    }
    // -- Shortcuts: the registry paradigm, exercised ---------------------------
    // A seed's claim isn't a fact until a consumer exercises it. The registry's
    // whole promise is that a key and its advertised row can't drift, because
    // both read one list -- so the guard that matters is pure and lives here:
    // no two chords may collide. (App wires from this same list; ShortcutsDialog
    // renders from it.)
    {
        namespace sc = kaflip::core;

        check("shortcuts: format_accel renders modifiers + key",
              sc::format_accel("<Ctrl><Shift>n") == "Ctrl+Shift+N",
              sc::format_accel("<Ctrl><Shift>n"));
        check("shortcuts: format_accel maps a named key to its glyph",
              sc::format_accel("<Ctrl>question") == "Ctrl+?",
              sc::format_accel("<Ctrl>question"));

        {
            sc::ShortcutSpec derived; derived.accels = {"<Ctrl>question", "<Ctrl>slash"};
            check("shortcuts: display_keys derives + joins accels",
                  derived.display_keys() == "Ctrl+?  /  Ctrl+/",
                  derived.display_keys());
            sc::ShortcutSpec doc; doc.keys = "Click a recent";
            check("shortcuts: display_keys prefers an explicit key string",
                  doc.display_keys() == "Click a recent");
        }

        check("shortcuts: registry is populated",
              !sc::shortcut_registry().empty(),
              std::to_string(sc::shortcut_registry().size()) + " specs");
        check("shortcuts: registry is accel-collision-free",
              sc::find_accel_collisions().empty(),
              sc::find_accel_collisions().empty()
                  ? "clean" : sc::find_accel_collisions().front());

        // Sections authored A-Z + contiguous: the dialog walks linearly and
        // starts a heading on change, so a stray out-of-order row would split a
        // section into two headings.
        {
            std::vector<std::string> secs;
            for (const auto& r : sc::shortcut_registry())
                if (secs.empty() || secs.back() != r.section) secs.push_back(r.section);
            std::vector<std::string> sorted = secs;
            std::sort(sorted.begin(), sorted.end());
            check("shortcuts: sections are contiguous and A-Z ordered", secs == sorted);
        }

        // The doc-only half of the paradigm: a gesture row documents without
        // binding (no accels), but must still show keys.
        {
            bool ok = true, saw_doc_only = false;
            for (const auto& r : sc::shortcut_registry())
                if (r.action.empty()) {
                    saw_doc_only = true;
                    if (!r.accels.empty() || r.keys.empty()) ok = false;
                }
            check("shortcuts: a doc-only row binds nothing but still displays keys",
                  ok && saw_doc_only);
        }
    }



    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
