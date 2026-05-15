#!/usr/bin/env bash
# Fail if the V1.1 and V1.2 sketches diverge beyond the intentional
# display-class lines (EInkDisplay_WirelessPaperV1_1 vs ..._V1_2).
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
F1="$REPO_ROOT/Heltec V1.1/HeltecV1_1_Pala_One_2_1/HeltecV1_1_Pala_One_2_1.ino"
F2="$REPO_ROOT/Heltec V1.2/HeltecV1_2_Pala_One_2_1/HeltecV1_2_Pala_One_2_1.ino"

if [ ! -f "$F1" ] || [ ! -f "$F2" ]; then
  echo "sync-check: one of the .ino files is missing" >&2
  exit 1
fi

TMP1=$(mktemp)
TMP2=$(mktemp)
trap 'rm -f "$TMP1" "$TMP2"' EXIT

# Collapse the board-specific class suffix so the rest of the file must match byte-for-byte.
sed -E 's/WirelessPaperV1_[12]/WirelessPaper/g' "$F1" > "$TMP1"
sed -E 's/WirelessPaperV1_[12]/WirelessPaper/g' "$F2" > "$TMP2"

if ! diff -q "$TMP1" "$TMP2" >/dev/null; then
  echo "ERROR: Heltec V1.1 and V1.2 sketches have drifted beyond the known display-class lines." >&2
  echo "Differences (after normalizing V1_1/V1_2 -> V1):" >&2
  diff -u "$TMP1" "$TMP2" | sed -n '1,80p' >&2
  echo "" >&2
  echo "Fix: mirror the change to both sketches, or update scripts/check-board-sync.sh if the divergence is intentional." >&2
  exit 1
fi

echo "sync-check: V1.1 and V1.2 sketches in sync."
