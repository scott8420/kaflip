#!/usr/bin/env bash
# Ka-flip install.
#   ./install.sh              -> ~/.local (no sudo)
#   ./install.sh --system     -> /usr/local (sudo if needed)
#   ./install.sh --prefix DIR -> DIR
#   --build                   -> run ./build.sh first
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
APP=kaflip
ID=io.github.scott8420.Kaflip
PREFIX="$HOME/.local"; DO_BUILD=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --system) PREFIX=/usr/local ;;
    --prefix) PREFIX="$2"; shift ;;
    --build)  DO_BUILD=1 ;;
    -h|--help) sed -n 2,7p "$0"; exit 0 ;;
    *) echo "unknown option: $1"; exit 2 ;;
  esac; shift
done
[[ $DO_BUILD == 1 ]] && ./build.sh
[[ -x build/$APP ]] || { echo "build/$APP missing -- run ./build.sh (or pass --build)"; exit 1; }

SUDO=""
mkdir -p "$PREFIX" 2>/dev/null || true
[[ -w "$PREFIX" ]] || SUDO=sudo

BIN="$PREFIX/bin/$APP"
DESK="$PREFIX/share/applications/$ID.desktop"
ICON="$PREFIX/share/icons/hicolor/scalable/apps/$ID.svg"
SYM="$PREFIX/share/icons/hicolor/symbolic/apps/$ID-symbolic.svg"
MAN="$PREFIX/share/$APP/install-manifest.txt"

$SUDO install -Dm755 "build/$APP" "$BIN"
TMP=$(mktemp); sed "s#@BINDIR@#$PREFIX/bin#g" "data/$ID.desktop.in" > "$TMP"
$SUDO install -Dm644 "$TMP" "$DESK"; rm -f "$TMP"
$SUDO install -Dm644 "data/$ID.svg" "$ICON"
$SUDO install -Dm644 "data/$ID-symbolic.svg" "$SYM"
printf '%s\n' "$BIN" "$DESK" "$ICON" "$SYM" "$MAN" | $SUDO install -Dm644 /dev/stdin "$MAN"

command -v gtk-update-icon-cache >/dev/null && $SUDO gtk-update-icon-cache -qtf "$PREFIX/share/icons/hicolor" || true
command -v update-desktop-database >/dev/null && $SUDO update-desktop-database -q "$PREFIX/share/applications" || true
echo "Installed Ka-flip to $PREFIX"
