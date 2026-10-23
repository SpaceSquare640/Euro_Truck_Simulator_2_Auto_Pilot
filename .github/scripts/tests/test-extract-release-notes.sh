#!/usr/bin/env bash
# Edited by: OpenAI-Codex_Responsible: Source_Code
set -euo pipefail

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)
extract="$script_dir/extract-release-notes.sh"
passed=0
failed=0

pass() { passed=$((passed + 1)); }
fail() { printf 'FAIL: %s\n' "$1" >&2; failed=$((failed + 1)); }
expect_success() {
  local name=$1 expected=$2 version=$3 file=$4 output
  if output=$(bash "$extract" "$version" "$file" 2>"$tmp/error") && [[ $output == "$expected" ]]; then
    pass
  else
    fail "$name"
  fi
}
expect_failure() {
  local name=$1 version=$2 file=$3
  if bash "$extract" "$version" "$file" >"$tmp/output" 2>"$tmp/error"; then
    fail "$name"
  elif [[ ! -s $tmp/output && -s $tmp/error ]]; then
    pass
  else
    fail "$name"
  fi
}

cat > "$tmp/main.md" <<'EOF'
# Change Log
## [Unreleased]

Upcoming

## [1.0.0] - 2026-10-23

### Added
- First release

## [0.9.0] - 2026-10-01
Old release
EOF
expect_success 'normal extraction excludes both headings and next section' $'### Added\n- First release' 1.0.0 "$tmp/main.md"
expect_failure 'missing version' 2.0.0 "$tmp/main.md"
expect_success 'Unreleased extraction' Upcoming Unreleased "$tmp/main.md"
expect_success 'last section reaches EOF' 'Old release' 0.9.0 "$tmp/main.md"
expect_success 'leading and trailing blank lines trimmed' $'### Added\n- First release' 1.0.0 "$tmp/main.md"

cat > "$tmp/empty.md" <<'EOF'
# Change Log
## [1.0.0] - 2026-10-23


## [0.9.0] - 2026-10-01
Older
EOF
expect_failure 'empty section' 1.0.0 "$tmp/empty.md"
if bash "$extract" 1.0.0 "$tmp/empty.md" >"$tmp/output" 2>"$tmp/error"; then
  fail 'empty section error text'
elif grep -q 'section is empty' "$tmp/error"; then
  pass
else
  fail 'empty section error text'
fi

cat > "$tmp/lookalikes.md" <<'EOF'
# Change Log
## [1.0.01] - 2026-10-23
Long patch
EOF
expect_failure '1.0.01 is not 1.0.0' 1.0.0 "$tmp/lookalikes.md"
cat > "$tmp/major.md" <<'EOF'
# Change Log
## [11.0.0] - 2026-10-23
Long major
EOF
expect_failure '11.0.0 is not 1.0.0' 1.0.0 "$tmp/major.md"
cat > "$tmp/prerelease.md" <<'EOF'
# Change Log
## [1.0.0-rc.1] - 2026-10-23
Prerelease
EOF
expect_failure 'prerelease is not final version' 1.0.0 "$tmp/prerelease.md"
expect_success 'prerelease extracts exactly' Prerelease 1.0.0-rc.1 "$tmp/prerelease.md"

cat > "$tmp/no-date.md" <<'EOF'
# Change Log
## [1.0.0]
No date
EOF
expect_failure 'missing date' 1.0.0 "$tmp/no-date.md"
cat > "$tmp/bad-date.md" <<'EOF'
# Change Log
## [1.0.0] - 2026-1-5
Bad date
EOF
expect_failure 'malformed date' 1.0.0 "$tmp/bad-date.md"
cat > "$tmp/duplicate.md" <<'EOF'
# Change Log
## [1.0.0] - 2026-10-23
First
## [1.0.0] - 2026-10-24
Second
EOF
expect_failure 'duplicate version heading' 1.0.0 "$tmp/duplicate.md"
expect_failure 'invalid short version' 1.0 "$tmp/main.md"
expect_failure 'invalid prefixed version' v1.0.0 "$tmp/main.md"
expect_failure 'invalid shell-like version' '1.0.0; rm' "$tmp/main.md"
expect_failure 'missing file' 1.0.0 "$tmp/missing.md"
if bash "$extract" 1.0.0 >"$tmp/output" 2>"$tmp/error"; then
  fail 'missing argument'
elif [[ ! -s $tmp/output && -s $tmp/error ]]; then
  pass
else
  fail 'missing argument'
fi

if output=$(bash "$extract" Unreleased "$repo_root/Change Log.md" 2>"$tmp/error") && [[ -n $output ]]; then
  pass
else
  fail 'real Change Log.md Unreleased section'
fi

printf 'PASSED=%s FAILED=%s\n' "$passed" "$failed"
(( failed == 0 ))
