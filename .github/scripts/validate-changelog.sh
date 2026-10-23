#!/usr/bin/env bash
# Edited by: OpenAI-Codex_Responsible: Source_Code
set -euo pipefail

if (( $# != 1 )); then
  echo "usage: validate-changelog.sh <changelog-path>" >&2
  exit 1
fi

changelog=$1
if [[ ! -f $changelog || ! -r $changelog ]]; then
  echo "changelog does not exist or is unreadable: $changelog" >&2
  exit 1
fi

bytes=$(wc -c < "$changelog")
if (( bytes > 204800 )); then
  echo "changelog exceeds 204800 bytes" >&2
  exit 1
fi
without_nul=$(tr -d '\000' < "$changelog" | wc -c)
if (( bytes != without_nul )); then
  echo "changelog contains NUL bytes" >&2
  exit 1
fi
if ! iconv -f UTF-8 -t UTF-8 "$changelog" > /dev/null; then
  echo "changelog is not valid UTF-8" >&2
  exit 1
fi

IFS= read -r first_line < "$changelog" || true
if [[ $first_line != '# Change Log' ]]; then
  echo "first line must be # Change Log; front matter is not allowed" >&2
  exit 1
fi
if LC_ALL=C grep -Eiq '<script|<iframe|javascript:|<object|<embed|onerror=|onload=' "$changelog"; then
  echo "changelog contains forbidden markup or script content" >&2
  exit 1
fi
if LC_ALL=C grep -Eq '^(import |export )' "$changelog"; then
  echo "changelog contains MDX import or export" >&2
  exit 1
fi
if ! LC_ALL=C grep -Eq '^## \[Unreleased\][[:blank:]]*$' "$changelog"; then
  echo "Unreleased heading is missing" >&2
  exit 1
fi
headings=$(awk 'index($0, "## [") == 1 { n++ } END { print n+0 }' "$changelog")
if (( headings < 1 )); then
  echo "no version headings found" >&2
  exit 1
fi
lines=$(awk 'END { print NR+0 }' "$changelog")
printf 'lines=%s bytes=%s version_headings=%s\n' "$lines" "$bytes" "$headings"
