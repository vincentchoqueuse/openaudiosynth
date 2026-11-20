#!/bin/sh
# clean.sh — removes everything generated: compiled objects and binaries of every build directory (like
# `make clean`), rendered WAVs, plots, test renders, CTest logs, Python caches and macOS .DS_Store files.
# The build directories stay configured (a plain `cmake --build` rebuilds); --all deletes them entirely.
# Sources, patches, songs, docs and tests/golden.json (the reference sound hashes) are never touched.
# Usage: sh tools/clean.sh [--all] [--dry-run]
set -e
cd "$(dirname "$0")/.."
ALL=0
DRY=0
for a in "$@"; do
  case "$a" in
    --all) ALL=1 ;;
    --dry-run) DRY=1 ;;
    *) echo "usage: sh tools/clean.sh [--all] [--dry-run]"; exit 2 ;;
  esac
done
rm_path() { if [ -e "$1" ]; then echo "remove $1"; [ $DRY -eq 1 ] || rm -rf "$1"; fi; }

before=$(du -sk . | cut -f1)
for d in build-*; do
  [ -d "$d" ] || continue
  if [ $ALL -eq 1 ]; then
    rm_path "$d"
    continue
  fi
  echo "clean $d (objects and binaries)"
  [ $DRY -eq 1 ] || cmake --build "$d" --target clean >/dev/null 2>&1 || true
  rm_path "$d/renders"
  rm_path "$d/plots"
  rm_path "$d/Testing"
  for f in "$d"/test_*.wav; do rm_path "$f"; done
done
find . -name "__pycache__" -type d -not -path "./build-*" | while read -r p; do rm_path "$p"; done
find . -name ".DS_Store" -not -path "./build-*" | while read -r p; do rm_path "$p"; done
after=$(du -sk . | cut -f1)
[ $DRY -eq 1 ] || echo "freed $(( (before - after) / 1024 )) MB"
if [ $ALL -eq 1 ] && [ $DRY -eq 0 ]; then
  echo "reconfigure with: just setup   (or cmake --preset host, debug, asan, arm, wasm)"
fi
