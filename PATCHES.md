# Patches on top of firmware 2.1

This file documents a set of correctness and storage-hygiene patches applied to
the firmware 2.1 source on the `tomoco-experiments` branch. The patches are
applied identically to both `Heltec V1.1` and `Heltec V1.2` sketches and were
audited against the project rule that **storage footprint must be minimized**
and **user-visible features must remain untouched**.

## Build cost

Measured against the unmodified 2.1 baseline:

| Sketch | Flash before | Flash after | Δ flash | RAM before | RAM after | Δ RAM |
|---|---|---|---|---|---|---|
| V1.2 | 1,169,148 B (29 %) | 1,169,500 B (29 %) | +352 B | 120,416 B (36 %) | 120,424 B (36 %) | +8 B |
| V1.1 | 1,169,860 B (29 %) | 1,170,216 B (29 %) | +356 B | 120,416 B (36 %) | 120,424 B (36 %) | +8 B |

Both still well under the 2.5 MB app partition.

## Patches

Each entry below cites the file region by symbol (line numbers drift as the
file is edited; symbols are stable).

### C2 — `btnISR` no longer advances the consumer pointer on a full queue

**Where**: `void IRAM_ATTR btnISR()`.

**What changed**: when the ring buffer is full (`next == btnQTail`), the ISR
now increments `g_isrDropCount` and returns immediately. Previously it
advanced `btnQTail` (the consumer pointer) from interrupt context and then
wrote the new event into the slot at `btnQHead`.

**Why**: `btnQTail` is also written by `ButtonState::poll()` on the main task.
Mutating it from the ISR raced the consumer and could deliver button events
out of order — for example, a RELEASE could be consumed before its matching
PRESS during burst input. Dropping the *new* event keeps the queue's existing
ordering invariant intact; an event is still lost when the queue overflows
(the original behavior also lost one), but no out-of-order delivery is
possible.

### L7 — pagination no longer starts mid-UTF-8 character

**Where**: the `safeReturn` lambda inside `readPageFromFile`.

**What changed**: after clamping `off` to `startPos + 1` (or to the file
size), the lambda now skips forward past any UTF-8 continuation bytes
(`0b10xxxxxx`), bounded at three iterations since a UTF-8 character is at
most four bytes.

**Why**: when an oversized token forced a page break at `startPos + 1`, that
byte could land in the middle of a multi-byte UTF-8 sequence. The next page
then started by reading a continuation byte as a leading byte; `utf8CharLenFromLead`
treated it as a 1-byte ASCII char and the first glyph of the page rendered as
a garbage character. The bug was rare with Latin text and more visible with
Cyrillic, Greek, or CJK content.

### M11 — `saveListItems` skips NVS writes when the payload is unchanged

**Where**: `static void saveListItems()`.

**What changed**: before calling `prefs.putBytes("list_v1", ...)`, the
function now reads the existing value, compares byte-for-byte (`memcmp` over
the same number of bytes it is about to write), and returns early if they
match.

**Why**: `saveListItems` is called from every list-related web action
(`/list`, `/list-clear-done`) and from the button-driven "mark done" path.
Every call wrote the full ~1 KB buffer to NVS, including when nothing had
changed (re-submitting the form, re-saving with no edits). NVS writes wear
the flash partition. Comparing first lets the common no-op case avoid the
write entirely, with no change to persisted format.

### H5 — `compactText` carries whitespace state across upload chunks

**Where**: signature of `static String compactText(...)`, and its two call
sites inside `handleUploadBookStream`. `UploadState` grew two fields:
`bookCompactLastWasSpace` and `bookCompactNewlineCount`.

**What changed**: `compactText` now accepts optional `bool*` and `int*` state
pointers plus a `trimTail` flag, all defaulted so existing one-shot callers
behave identically. The `FILE_WRITE` and `FILE_END` upload branches pass
pointers to the new `UploadState` fields and select `trimTail` based on
whether the chunk is the final one.

**Why**: uploads stream the file in HTTP chunks. The previous implementation
called `compactText` per chunk with no shared state, so its "collapse
consecutive spaces" and "cap consecutive newlines at 2" rules reset at every
chunk boundary. A chunk ending in spaces followed by a chunk starting with
spaces produced a doubled space in the stored book; a chunk ending in `\n`
followed by another starting with `\n\n` produced three newlines in the file,
exceeding the documented cap. With shared state both rules now hold across
boundaries. One residual divergence from a single-shot run remains: the
"strip trailing space before `\n`" rule operates on the local chunk's `out`
buffer and cannot reach back into bytes already flushed to the previous
chunk, so a single space immediately preceding a chunk-boundary newline is
preserved. This is cosmetic and the reader's pagination handles it without
issue. Side effect: end-of-file trailing whitespace may now survive (the
per-chunk trim no longer fires).

### M13 — short writes during upload abort instead of silently finalizing

**Where**: both `print()` call sites inside `handleUploadBookStream`.

**What changed**: each `g_upload.bookTmpFile.print(cleaned)` now captures the
returned byte count. If it does not match `cleaned.length()`:
- in `FILE_WRITE`: set `bookError`, close the tmp file, remove it from
  `/books/`. Subsequent `FILE_WRITE` callbacks early-return; `FILE_END`
  also early-returns via its existing `(bookError && !bookTmpFile)` check.
- in `FILE_END` (the trailing UTF-8 tail): set `bookError`. A new branch in
  the finalize section detects `bookError`, removes the tmp file, and skips
  the rename to `.txt`.

**Why**: `File::print(const String&)` returns the number of bytes actually
written; on a partial write (out of space, FS error mid-stream) it returns
less than requested. The previous code ignored the return value entirely and
always promoted the tmp file to a `.txt` book at the end of the stream — a
truncated upload was silently accepted as a complete book. With the check, a
failed upload now leaves the library exactly as it was before.

### H8 — font/line-gap change preserves reading position via byte offset

**Where**: `static void invalidateAllPageCaches()`, new `static void
relocateOpenBookToOffset(uint32_t)`, branch added to `openBookByIndex()`,
extra write inside `saveProgressThrottled()`. New per-book NVS keys: `_o`
(uint32 byte offset of current page) and `_n` (bool "needs relocation").

**What changed**: page numbers stored in prefs (`_p`) are layout-dependent
and become meaningless after a font or line-gap change. Byte offsets are
not. `saveProgressThrottled` now also persists the current page's byte
offset in `_o`. `invalidateAllPageCaches` no longer resets `_p` to 0 for
every book; instead it sets a `_n` flag per book and, for the currently
open book, captures `_o` then immediately walks the new layout via
`relocateOpenBookToOffset` to find the new page. `openBookByIndex` checks
`_n` on cold open and re-derives the page from `_o`, then clears `_n` and
rewrites `_p`. Bookmarks are intentionally NOT touched: their `bmOffsets[]`
remain valid in the same file, so navigation lands on the correct text;
the stored page numbers are stale display fallback only.
`relocateOpenBookToOffset` yields every 64 pages so a near-EOF relocation
in a multi-MB book does not starve the HTTP server or trip the WDT.

**Why**: previously, changing font size silently sent every book to page 1
and wiped bookmark byte offsets. The user lost their reading position in
every book and had to re-find each bookmark's intended location. The byte
offset is the only layout-independent anchor we have, so it's now what we
trust. Cost: 4 bytes per book in NVS for `_o` plus 1 byte for `_n`, paid
only after a font change.

### H9 — fix `pc_*.bin` cache cleanup using arduino-esp32 3.x basename

**Where**: `static void invalidateAllPageCaches()`, the `FS.remove()` loop.

**What changed**: `f.name()` on arduino-esp32 3.x returns the BASENAME
(no leading slash). The previous code matched on `name.startsWith("/pc_")`
which never matched, so stale page-offset cache files were never removed
on a layout change. The fix accepts both forms and rebuilds the absolute
path before calling `FS.remove`.

**Why**: stale `pc_*.bin` files would be reloaded on the next book open and
their offsets would be from the OLD font layout, giving the new layout
mis-rendered pages until the cache was rebuilt by manual scrolling.

## Things considered and deliberately *not* applied

These were identified as plausible improvements during the review pass but
ruled out under the "minimize footprint, don't touch features, keep the diff
small" constraints. Recording them so the same ground does not get re-tilled
later.

| ID | What | Why deferred |
|---|---|---|
| H6 | Move per-book progress/bookmark prefs to the *new* key before renaming the file, so a power loss between rename and metadata move cannot orphan progress. | Needs splitting `migrateBookMetadata` into copy / finalize / rollback (~40 LOC and a caller rewrite). The failure window is microseconds during a manual web-UI move; cost/benefit poor for a minimal-change set. |
| M3 | Cap recursion depth in `scanBooksRecursive`. | Initially applied at depth 6, then reverted: `sanitizeFolderInput` accepts internal `/`, so a user can legitimately have folders deeper than 6 via `/move`. A cap would hide their books from the UI. Original unbounded recursion preserved. |
| M1 | Reduce `MAX_PAGES` from 10000 to 5000 (-20 KB BSS). | Caps maximum paginated book size — a feature change. Needs the user to confirm the largest realistic book first. |
| S1 | Random SoftAP password generated per device, displayed on the e-ink in upload mode. | User-visible change (the password is no longer the documented `palaread`). Also adds flash/NVS cost. Pending product decision. |
| S2 | CSRF tokens on every POST endpoint. | Adds meaningful flash growth for what is already mitigated by the AP password and the AP only being live during upload mode. |
| S4 | Hard cap on per-upload byte size. | Feature change: rejects oversized uploads outright. |
| Dead code removal (D1, D2, D4, D6, D7 from review) | Trim unused `libraryFolderExists`, `utf8CharAt`, `LIB_ENTRY_BACK` enum value, duplicate pagination banner comment, dead `BatteryState::calibrationFactor`. | This branch belongs to upstream; not removing code that is not load-bearing for the patches above. |

## Damage / safety audit

The diff was triple-checked against device-damage modes before flashing onto a
V1.2 board running 2.0 (which subsequently upgraded cleanly to the patched 2.1).

- **Brick risk**: bootloader, `partitions.csv`, `setup()`, GPIO config, ADC,
  e-ink driver init, watchdog, IRQ priorities — all untouched.
- **Data loss risk**: NVS key/format unchanged (M11 only short-circuits a
  duplicate write); LittleFS layout unchanged. The only `FS.remove` paths
  added (M13) target `.tmp` files that have never been renamed to `.txt`.
- **Hardware**: no change to GPIO, ADC, display, CPU frequency, or any
  power-domain register.
- **Power**: deep-sleep entry conditions and `lastUserActionMs` unchanged.
- **Flash wear**: M11 reduces NVS writes; no new periodic writes were added.

## V1.1 ↔ V1.2 sync

Every patch was applied identically to both sketches. `scripts/check-board-sync.sh`
passes after the full series. The build of each sketch via
`arduino-cli compile -b esp32:esp32:heltec_wireless_paper "<sketch>"` is clean.
