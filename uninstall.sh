#!/usr/bin/env bash
# Ka-flip uninstall -- removes exactly what install.sh recorded.
#   ./uninstall.sh [--system | --prefix DIR] [--purge]
#   --purge also removes settings, recents and cache (never your books).
set -e
APP=kaflip
PREFIX="$HOME/.local"; PURGE=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --system) PREFIX=/usr/local ;;
    --prefix) PREFIX="$2"; shift ;;
    --purge)  PURGE=1 ;;
    -h|--help) sed -n 2,4p "$0"; exit 0 ;;
    *) echo "unknown option: $1"; exit 2 ;;
  esac; shift
done
MAN="$PREFIX/share/$APP/install-manifest.txt"
[[ -f "$MAN" ]] || { echo "no install manifest at $MAN"; exit 1; }
SUDO=""; [[ -w "$PREFIX/share/$APP" ]] || SUDO=sudo
REAL=$(realpath "$PREFIX")
while IFS= read -r f; do
  [[ -z "$f" ]] && continue
  case "$(realpath -m "$f")" in "$REAL"/*) $SUDO rm -f "$f" ;; *) echo "skip (outside prefix): $f" ;; esac
done < "$MAN"
$SUDO rmdir "$PREFIX/share/$APP" 2>/dev/null || true
if [[ $PURGE == 1 ]]; then
  rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/$APP" "${XDG_DATA_HOME:-$HOME/.local/share}/$APP" "${XDG_CACHE_HOME:-$HOME/.cache}/$APP"
  echo "Purged settings, recents and cache."
fi
command -v gtk-update-icon-cache >/dev/null && $SUDO gtk-update-icon-cache -qtf "$PREFIX/share/icons/hicolor" || true
command -v update-desktop-database >/dev/null && $SUDO update-desktop-database -q "$PREFIX/share/applications" || true
echo "Uninstalled Ka-flip from $PREFIX"
