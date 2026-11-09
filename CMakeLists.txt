cmake_minimum_required(VERSION 3.20)
project(kaflip VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
find_package(spdlog REQUIRED)
# s002 -- the EPUB engine. WebKitGTK renders (the app only); zlib inflates the
# zip (the core only). Fedora: webkitgtk6.0-devel zlib-ng-compat-devel.
pkg_check_modules(WEBKIT REQUIRED webkitgtk-6.0)
find_package(ZLIB REQUIRED)

# ── kaflip_core -- the GTK-free core (the model side of the one real seam) ──────
# No gtkmm on this library's link line, BY DESIGN: a stray UI include under
# core/ fails to compile, so the seam is enforced by the build graph and the
# core stays headless-testable. (docs/two-layer-seam.md.)
add_library(kaflip_core STATIC
    src/core/Recents.cpp
    src/core/TextMap.cpp
    # The SHORTCUT-REGISTRY paradigm: one list, two consumers (App wires accels
    # from it, ShortcutsDialog renders from it) so a key and its advertised row
    # can't drift. GTK-free, so it lives in core and the selftest guards it
    # headless -- a pure test proves no two chords collide.
    src/core/Shortcuts.cpp
    # The UNDO paradigm (s046): core/Undo.hpp is the journal (header-only, a
    # template over the app's store); Stones is the demo model + its verbs +
    # the verb table the selftest gate walks. docs/undo.md.
    src/core/Stones.cpp
    # s002 -- the EPUB reader: a small zip reader on zlib, then container.xml
    # -> OPF -> metadata / manifest / spine, and the kaflip-book: URI mapping.
    src/core/Zip.cpp
    src/core/Epub.cpp
    # s003 -- the page-turn rules, the column stylesheet, the page's report.
    src/core/Paging.cpp
    # s004 -- the one list of rail pages (ported from Trails).
    src/core/Tabs.cpp
    # s006 -- the library: the store + its undoable verbs, folder scanning,
    # the cover-thumbnail cache key.
    src/core/Library.cpp
    # s007 -- reading style: presets, the book stylesheet, the undoable store.
    src/core/ReadingStyle.cpp
    # Vendored pugixml 1.14 (MIT), compiled into the core and nowhere else.
    third_party/pugixml/pugixml.cpp
)
target_include_directories(kaflip_core PUBLIC include)
target_include_directories(kaflip_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugixml)
target_link_libraries(kaflip_core PUBLIC ZLIB::ZLIB)
# Vendored nlohmann/json (header-only, MIT), PRIVATE -- an implementation detail
# of the pump, no consumer inherits the include (CANON: surprise-free build).
target_include_directories(kaflip_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ── Headless selftest -- exercises the core with no display ────────────────────
add_executable(kaflip_selftest src/selftest.cpp)
target_link_libraries(kaflip_selftest PRIVATE kaflip_core)

# ── App resources (gresource) -- the RESOURCE-PIPELINE paradigm ────────────────
# glib-compile-resources embeds resources/kaflip.gresource.xml into a C source;
# App::on_activate registers it and points the icon theme at the bundle so
# set_icon_name("kaflip-logo-symbolic") resolves out of the binary. The generated
# .c compiles as C (project enables C above) so kaflip_resources_get_resource()
# has C linkage, matching the extern "C" prototype in App.cpp. Adding an icon:
# drop the .svg in resources/icons/scalable/apps/, list it in the .xml, rebuild.
# (docs/resources-and-dialog-lifetime.md)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(KAFLIP_GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/kaflip.gresource.xml")
set(KAFLIP_RESOURCES_C   "${CMAKE_CURRENT_BINARY_DIR}/kaflip-resources.c")
file(GLOB KAFLIP_RESOURCE_FILES CONFIGURE_DEPENDS   # s003: a new/edited .js or .svg must rebuild the bundle
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/js/*.js"    # s003: the pagination script
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.css")     # s004: the skin
add_custom_command(
    OUTPUT  "${KAFLIP_RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${KAFLIP_RESOURCES_C}"
            --generate-source
            --c-name kaflip_resources
            kaflip.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${KAFLIP_GRESOURCE_XML}" ${KAFLIP_RESOURCE_FILES}
    COMMENT "Compiling gresource bundle (kaflip-resources.c)"
    VERBATIM)

# ── kaflip -- the GTK app ───────────────────────────────────────────────────────
# The Shell is a SPLIT CLASS across TUs by category (see the index in
# include/Shell.hpp); all parts compile into the one kaflip::Shell.
add_executable(kaflip
    ${KAFLIP_RESOURCES_C}
    src/main.cpp
    src/App.cpp
    # Follows the desktop's light/dark preference (GTK4 will not do it for us
    # and we do not link libadwaita). Needs no new dependency: the D-Bus and
    # GSettings calls are GIO, which gtkmm-4.0 already brings in.
    src/Appearance.cpp
    src/Shell.cpp
    src/Shell_zones.cpp
    src/Shell_bindings.cpp
    src/Shell_handlers.cpp
    src/Shell_helpers.cpp
    src/AboutWindow.cpp
    src/ShortcutsDialog.cpp
    src/Registry.cpp
    src/Log.cpp
    # s002 -- the reading surface: a WebKitWebView fed by the kaflip-book: scheme.
    src/Reader.cpp
    # s006 -- the library: cover thumbnails (gdk-pixbuf) + the background folder scan.
    src/Covers.cpp
    src/Scanner.cpp
    # s007 -- the Reading style window (controls + a live WebKit preview).
    src/StyleDialog.cpp
)
target_include_directories(kaflip PRIVATE include ${GTKMM_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
target_link_libraries(kaflip     PRIVATE ${GTKMM_LIBRARIES} ${WEBKIT_LIBRARIES} kaflip_core spdlog::spdlog)
target_link_directories(kaflip   PRIVATE ${GTKMM_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
target_compile_options(kaflip    PRIVATE ${GTKMM_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
