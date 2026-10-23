#!/usr/bin/env bash
# Edited by: OpenAI-Codex_Responsible: Source_Code
set -euo pipefail

if (( $# != 2 )); then
  echo "usage: extract-release-notes.sh <version|Unreleased> <changelog-path>" >&2
  exit 1
fi

version=$1
changelog=$2
if [[ $version != Unreleased && ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]]; then
  echo "invalid version: $version" >&2
  exit 1
fi
if [[ ! -f $changelog || ! -r $changelog ]]; then
  echo "changelog does not exist or is unreadable: $changelog" >&2
  exit 1
fi

awk -v target="$version" '
  function blank(s) { return s ~ /^[[:space:]]*$/ }
  BEGIN { token = "## [" target "]" }
  {
    line = $0
    sub(/\r$/, "", line)
    if (index(line, token) == 1) {
      matches++
      valid = 0
      rest = substr(line, length(token) + 1)
      if (target == "Unreleased") {
        valid = (rest ~ /^[[:blank:]]*$/)
      } else if (index(rest, " - ") == 1) {
        date = substr(rest, 4)
        valid = (date ~ /^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9][[:blank:]]*$/)
      }
      if (valid) { active = 1; found = 1 }
      else { active = 0; malformed = 1 }
      next
    }
    if (index(line, "## [") == 1) { active = 0; next }
    if (active) lines[++count] = line
  }
  END {
    if (matches > 1) { print "duplicate version heading" > "/dev/stderr"; exit 1 }
    if (malformed || !found) { print "version heading missing or malformed" > "/dev/stderr"; exit 1 }
    first = 1
    while (first <= count && blank(lines[first])) first++
    last = count
    while (last >= first && blank(lines[last])) last--
    if (first > last) { print "section is empty" > "/dev/stderr"; exit 1 }
    for (i = first; i <= last; i++) print lines[i]
  }
' "$changelog"
