#!/bin/sh
# The release step of site/Dockerfile: downloads the files of the pinned release into the site and checks every byte.
#   release.sh PIN_DIR SITE_DIR BASE_URL
# PIN_DIR holds RELEASE (one line: a release tag, or none) and release.sha256 (one line per file: its SHA-256, its path
# on the site, the name of its release asset). Each asset is read from BASE_URL/<tag>/<asset>. The step fails, and the
# build with it, when a download fails, when a file is not the pinned bytes, when a path is already a page of the
# site, and when RELEASE and release.sha256 disagree about whether a release is pinned.
set -eu
pin=$1 site=$2 base=$3
tag=$(cat "$pin/RELEASE")
fail() { echo "site release: $*" >&2; exit 1; }
[ -n "$tag" ] || fail "site/RELEASE is empty: write a release tag, or none"
count=0
while read -r sum path asset || [ -n "$sum" ]; do
  case "$sum" in ''|'#'*) continue ;; esac
  [ -n "$asset" ] || fail "site/release.sha256: a line needs a SHA-256, a path and an asset name: $sum $path"
  [ "$tag" != none ] || fail "site/RELEASE says none, but site/release.sha256 lists $path"
  [ ! -e "$site/$path" ] || fail "$path is already a file of the site: a release may not replace it"
  mkdir -p "$(dirname "$site/$path")"
  wget -q -O "$site/$path" "$base/$tag/$asset" || fail "could not download $asset of $tag from $base"
  echo "$sum  $site/$path" | sha256sum -c - >/dev/null || fail "$asset of $tag is not the pinned file (SHA-256 differs): $path"
  count=$((count + 1))
done < "$pin/release.sha256"
[ "$tag" = none ] || [ "$count" -gt 0 ] || fail "site/RELEASE names $tag, but site/release.sha256 lists no file"
echo "site release: $tag, $count files"
