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
        const n = Math.max(1, Math.ceil((root().scrollWidth - 2) / w));
        marker.style.left = (n * w - 1) + 'px';
        marker.style.display = 'block';
        return n;
    }
    function report() {
        const msg = page + '/' + count;
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
        const old = count;
        count = measure();
        if (wantLast) page = count - 1;
        else if (old !== count) page = Math.min(count - 1, Math.floor(page * count / old));
        else page = Math.min(page, count - 1);
        show();
    }
    // An anchor (#id) -> the page its element starts on.
    function toHash() {
        const id = decodeURIComponent(location.hash.slice(1));
        if (!id) return false;
        const el = document.getElementById(id) || document.getElementsByName(id)[0];
        if (!el) return false;
        const x = el.getBoundingClientRect().left + root().scrollLeft;
        go(Math.floor(x / W()));
        return true;
    }

    window.kaflip = {
        go: go,
        next: () => go(page + 1),
        prev: () => go(page - 1),
        first: () => go(0),
        last: last,
        relayout: relayout,
    };

    // Anything else that scrolls (WebKit's own anchor jump, a selection drag)
    // is snapped back onto a page boundary and reported.
    window.addEventListener('scroll', () => {
        const want = page * W();
        if (Math.abs(root().scrollLeft - want) > 1) {
            wantLast = false;
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
    window.addEventListener('load', () => { if (!toHash()) relayout(); });
    for (const img of document.images) img.addEventListener('load', relayout);
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(relayout);

    count = measure();
    if (!toHash()) go(0);
})();
