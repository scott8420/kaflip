# Ka-flip

An EPUB reader for GNOME, written in C++23 with gtkmm 4. It has the same
colourful, big-icon look as its sibling utility apps.

Planned:

- A library shelf with a cover grid.
- **Immersion mode**: full-screen, the chrome fades away, and you turn pages
  with a key or a click.
- **Your reading style**: heading and body fonts, size, line height, margins,
  and background and text colours. You can keep named presets (Day, Sepia,
  Night, Paper) and override the style for a single book.
- A **blurb** for each book, looked up online (Open Library, then Google
  Books) and cached.
- A table of contents, bookmarks, highlights and notes, and reading progress.

Status: early. The app is currently the bare Cairn starter, renamed.

## Build, install, uninstall

```
./build.sh                 # build/kaflip and build/kaflip_selftest
./build/kaflip_selftest    # headless core tests
./install.sh               # to ~/.local   (--system for /usr/local, --prefix DIR)
./uninstall.sh             # removes exactly what install.sh put down (--purge for settings)
```

Dependencies on Fedora: `gtkmm4.0-devel spdlog-devel webkitgtk6.0-devel zlib-ng-compat-devel cmake gcc-c++ ninja-build`

Dependencies on Ubuntu: `libgtkmm-4.0-dev libspdlog-dev libwebkitgtk-6.0-dev zlib1g-dev cmake g++ ninja-build`

## License

MIT.
