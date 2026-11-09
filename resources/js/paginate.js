// paginate.js -- Ka-flip's page keeper (s003). Injected into every chapter as a
// WebKit user script, AFTER core::pagination_css has cut the body into columns
// exactly one viewport apart. So page n sits at scrollLeft = n * innerWidth.
//
// The app decides (core::turn_forward / turn_back); this script only moves to
// the page it is told and reports "page/count" back through the "kaflip"
// message handler whenever either number changes. The book's own scripts are
// off; this one is ours and runs regardless.
(function () {
    'use strict';
    if (window.kaflip) return;

    const root = () => document.scrollingElement || document.documentElement;
    const W = () => window.innerWidth;
    let page = 0, count = 1, wantLast = false, sent = '';
    let wantId = '';   // s008: an anchor we were sent to -- kept through relayouts
    let mark = -1;     // s008: the character the reader is held to (-1 = take the page's first)

    // s008 -- places in the text. Every non-blank text node in the body, in
    // document order, with its starting character. Text flows column after
    // column, so a character's page only grows with its offset: two binary
    // searches find the first character on a page, or a character's page.
    let texts = null, total = 0;
    function index() {
        if (texts) return;
        texts = [];
        total = 0;
        const top = document.body || document.documentElement;
        const walk = document.createTreeWalker(top, NodeFilter.SHOW_TEXT);
        for (let n = walk.nextNode(); n; n = walk.nextNode()) {
            const v = n.nodeValue;
            if (!v || !/\S/.test(v)) continue;
            const p = n.parentElement;
            if (p && /^(script|style|head|title)$/i.test(p.tagName)) continue;
            texts.push({ node: n, start: total, len: v.length });
            total += v.length;
        }
    }
    function xOf(t, off) {
        off = Math.max(0, Math.min(off, t.len - 1));
        const r = document.createRange();
        r.setStart(t.node, off);
        r.setEnd(t.node, off + 1);
        const rects = r.getClientRects();
        let rc = rects.length ? rects[0] : null;
        if (!rc && t.node.parentElement) rc = t.node.parentElement.getBoundingClientRect();
        return rc ? rc.left + root().scrollLeft : 0;
    }
    const pageOfX = (x) => Math.floor((x + 1) / W());
    // The first character whose page is >= p (total when there is none).
    function charAt(p) {
        index();
        let lo = 0, hi = texts.length;
        while (lo < hi) {
            const mid = (lo + hi) >> 1;
            if (pageOfX(xOf(texts[mid], texts[mid].len - 1)) >= p) hi = mid; else lo = mid + 1;
        }
        if (lo >= texts.length) return total;
        const t = texts[lo];
        let a = 0, b = t.len - 1;
        while (a < b) {
            const m = (a + b) >> 1;
            if (pageOfX(xOf(t, m)) >= p) b = m; else a = m + 1;
        }
        return t.start + a;
    }
    // The page a character sits on.
    function pageOfChar(c) {
        index();
        if (!texts.length) return 0;
        let lo = 0, hi = texts.length - 1;
        while (lo < hi) {
            const mid = (lo + hi + 1) >> 1;
            if (texts[mid].start <= c) lo = mid; else hi = mid - 1;
        }
        const t = texts[lo];
        return pageOfX(xOf(t, Math.min(c - t.start, t.len - 1)));
    }
    function snippetAt(c) {
        index();
        let out = '';
        for (const t of texts) {
            if (t.start + t.len <= c) continue;
            out += t.node.nodeValue.slice(Math.max(0, c - t.start)) + ' ';
            if (out.length > 200) break;
        }
        out = out.replace(/\s+/g, ' ').trim();
        return out.length > 90 ? out.slice(0, 90).replace(/\s+\S*$/, '') + '\u2026' : out;
    }

    // s011 -- a publisher's box taller than our page (Beartown's cover:
    // .ipadfp { height: 98vh } inside our top + bottom margins) spills an
    // EMPTY column after it -- "page 1 of 2" on a cover. Trailing pages with
    // no text starting on them and no picture on them are not pages.
    function trimBlank(n) {
        index();
        const media = Array.from(document.querySelectorAll('img, svg, video, image'))
            .map((m) => m.getBoundingClientRect())
            .filter((r) => r.width > 0 && r.height > 0)
            .map((r) => pageOfX(r.left + root().scrollLeft));
        while (n > 1) {
            const p = n - 1;
            if (charAt(p) < total) break;                 // text starts on it
            if (media.some((q) => q >= p)) break;         // a picture sits on it
            n--;
        }
        return n;
    }

    // The last column ends one right margin short of a full page, and WebKit
    // clamps scrollLeft to the overflow -- so without help the last page sits
    // a margin too far left. An absolutely placed 1px marker outside the
    // columns stretches the overflow to exactly count * innerWidth.
    let marker = null;
    function measure() {
        const w = W();
        if (w <= 0) return 1;
        if (!marker) {
            marker = document.createElement('div');
            marker.id = 'kaflip-end';
            marker.setAttribute('aria-hidden', 'true');
            marker.style.cssText = 'position:absolute;top:0;width:1px;height:1px;' +
                                   'margin:0;padding:0;border:0;pointer-events:none;';
            document.documentElement.appendChild(marker);
        }
        marker.style.display = 'none';                       // measure the columns alone
        // s007: ceil, not round -- a spread's last page may be one column
        // (half a viewport) and still a page. 2px of slack for subpixels.
        const n = trimBlank(Math.max(1, Math.ceil((root().scrollWidth - 2) / w)));
        marker.style.left = (n * w - 1) + 'px';
        marker.style.display = 'block';
        return n;
    }
    function report() {
        // s008: "page/count@start,end,mark<TAB>first words"
        const s = charAt(page);
        const e = page + 1 < count ? charAt(page + 1) : total;
        if (mark < 0) mark = s;
        const msg = page + '/' + count + '@' + s + ',' + Math.max(s, e) + ',' + mark + '\t' + snippetAt(s);
        if (msg === sent) return;
        sent = msg;
        try { window.webkit.messageHandlers.kaflip.postMessage(msg); } catch (e) { /* no host */ }
    }
    function show() {
        root().scrollLeft = page * W();
        report();
    }
    function go(n) {
        wantLast = false;
        wantId = '';
        mark = -1;   // s008: held to the new page's first character (set in report)
        count = measure();
        page = Math.min(Math.max(0, n | 0), count - 1);
        show();
    }
    function last() {
        go(measure() - 1);
        wantLast = true;          // stay on the last page while images load
    }
    // The layout changed (window resized, an image or font arrived): keep the
    // same place in the chapter, as a fraction, or the last page if asked.
    function relayout() {
        if (wantId && toId(wantId)) return;   // s008: an image above it arrived
        const old = count;
        count = measure();
        if (wantLast) page = count - 1;
        else if (mark >= 0) page = Math.min(count - 1, pageOfChar(mark));   // s008: the same words
        else if (old !== count) page = Math.min(count - 1, Math.floor(page * count / old));
        else page = Math.min(page, count - 1);
        show();
    }
    // An anchor (an element id) -> the page its element starts on. s008: the
    // id is remembered until a turn, so a late image or font that pushes the
    // element onto another page takes us with it.
    function toId(id) {
        if (!id) return false;
        const el = document.getElementById(id) || document.getElementsByName(id)[0];
        if (!el) return false;
        const x = el.getBoundingClientRect().left + root().scrollLeft;
        go(Math.floor((x + 1) / W()));
        wantId = id;
        return true;
    }
    // s008: a character -- the saved position or a bookmark. Held there.
    function toChar(c) {
        c = Math.max(0, c | 0);
        count = measure();
        go(Math.min(count - 1, pageOfChar(c)));
        mark = c;
        sent = '';
        report();
    }
    function toHash() {
        let id = location.hash.slice(1);
        try { id = decodeURIComponent(id); } catch (e) { /* keep it raw */ }
        const m = /^kaflip-char=(\d+)$/.exec(id);
        if (m) { toChar(parseInt(m[1], 10)); return true; }
        return toId(id);
    }

    window.kaflip = {
        go: go,
        next: () => go(page + 1),
        prev: () => go(page - 1),
        first: () => go(0),
        last: last,
        relayout: relayout,
        anchor: (id) => { if (!toId(id)) go(0); },   // s008: a TOC entry in this chapter
        toChar: toChar,                               // s008: a bookmark / the saved place
    };

    // Anything else that scrolls (WebKit's own anchor jump, a selection drag)
    // is snapped back onto a page boundary and reported.
    window.addEventListener('scroll', () => {
        const want = page * W();
        if (Math.abs(root().scrollLeft - want) > 1) {
            wantLast = false;
            wantId = '';
            mark = -1;
            page = Math.min(Math.max(0, Math.round(root().scrollLeft / W())), count - 1);
            show();
        }
    }, { passive: true });
    // s005: tap zones. A plain click (not a link, not ending a selection)
    // reports which third of the page it hit; the app decides what it means
    // (in focus mode: back / forward / show the bar).
    document.addEventListener('click', (e) => {
        if (e.button !== 0) return;
        if (e.target && e.target.closest && e.target.closest('a, button, input, select, textarea')) return;
        const sel = window.getSelection();
        if (sel && !sel.isCollapsed) return;
        const f = e.clientX / W();
        const zone = f < 0.3 ? 'back' : (f > 0.7 ? 'forward' : 'center');
        try { window.webkit.messageHandlers.kaflip.postMessage('tap:' + zone); } catch (err) { /* no host */ }
    });
    window.addEventListener('resize', relayout);
    window.addEventListener('hashchange', toHash);
    // s008: the hash was honoured once, at the start; on load just re-measure
    // (wantId / mark keep the place) -- a page turned meanwhile must stand.
    window.addEventListener('load', relayout);
    for (const img of document.images) img.addEventListener('load', relayout);
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(relayout);

    count = measure();
    if (!toHash()) go(0);
})();
