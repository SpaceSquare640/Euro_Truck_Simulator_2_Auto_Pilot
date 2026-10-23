#!/usr/bin/env bash
# Edited by: OpenAI-Codex_Responsible: Source_Code
set -euo pipefail

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)
validate="$script_dir/validate-changelog.sh"
passed=0
failed=0

pass() { passed=$((passed + 1)); }
fail() { printf 'FAIL: %s\n' "$1" >&2; failed=$((failed + 1)); }
expect_failure() {
  local name=$1 file=$2
  if bash "$validate" "$file" >"$tmp/output" 2>"$tmp/error"; then
    fail "$name"
  elif [[ ! -s $tmp/output && -s $tmp/error ]]; then
    pass
  else
    fail "$name"
  fi
}

cat > "$tmp/valid.md" <<'EOF'
# Change Log
## [Unreleased]
### Added
- Safe update
EOF
if output=$(bash "$validate" "$tmp/valid.md") && [[ $output =~ ^lines=[0-9]+\ bytes=[0-9]+\ version_headings=[0-9]+$ ]]; then
  pass
else
  fail 'valid document and summary'
fi

cat > "$tmp/oversize.md" <<'EOF'
# Change Log
## [Unreleased]
EOF
awk 'BEGIN { for (i = 0; i < 205000; i++) printf "a" }' >> "$tmp/oversize.md"
expect_failure 'oversize document' "$tmp/oversize.md"

cat > "$tmp/invalid-utf8.md" <<'EOF'
# Change Log
## [Unreleased]
EOF
printf '\377\n' >> "$tmp/invalid-utf8.md"
expect_failure 'invalid UTF-8' "$tmp/invalid-utf8.md"

cat > "$tmp/wrong-first.md" <<'EOF'
# Wrong
## [Unreleased]
EOF
expect_failure 'wrong first line' "$tmp/wrong-first.md"
cat > "$tmp/front-matter.md" <<'EOF'
---
# Change Log
## [Unreleased]
EOF
expect_failure 'front matter' "$tmp/front-matter.md"
cat > "$tmp/script.md" <<'EOF'
# Change Log
## [Unreleased]
<ScRiPt>alert(1)</ScRiPt>
EOF
expect_failure 'mixed-case script tag' "$tmp/script.md"
cat > "$tmp/javascript.md" <<'EOF'
# Change Log
## [Unreleased]
[bad](JaVaScRiPt:alert(1))
EOF
expect_failure 'javascript URL' "$tmp/javascript.md"
cat > "$tmp/import.md" <<'EOF'
# Change Log
## [Unreleased]
import widget from 'x'
EOF
expect_failure 'MDX import' "$tmp/import.md"
cat > "$tmp/export.md" <<'EOF'
# Change Log
## [Unreleased]
export const widget = 1
EOF
expect_failure 'MDX export' "$tmp/export.md"
cat > "$tmp/no-unreleased.md" <<'EOF'
# Change Log
## [1.0.0] - 2026-10-23
Older
EOF
expect_failure 'missing Unreleased' "$tmp/no-unreleased.md"
cat > "$tmp/no-headings.md" <<'EOF'
# Change Log
No version headings
EOF
expect_failure 'no headings' "$tmp/no-headings.md"
cat > "$tmp/nul.md" <<'EOF'
# Change Log
## [Unreleased]
EOF
printf '\000\n' >> "$tmp/nul.md"
expect_failure 'NUL byte' "$tmp/nul.md"
expect_failure 'missing file' "$tmp/missing.md"

if output=$(bash "$validate" "$repo_root/Change Log.md") && [[ $output =~ ^lines=[0-9]+\ bytes=[0-9]+\ version_headings=[0-9]+$ ]]; then
  pass
else
  fail 'real Change Log.md'
fi

printf 'PASSED=%s FAILED=%s\n' "$passed" "$failed"
(( failed == 0 ))
