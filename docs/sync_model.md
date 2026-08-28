# Sync model

What the integration mirrors between the device and Memos, and the rules that
govern it. TL;DR: **the device's SD card is the source of truth for notes; Memos
is the source of truth for task text; the device wins a task toggle race.**

## Notes (device → Memos, one-way)

- A recording is `note_NNN.wav` on the SD card, indexed in `/notes/index.csv`
  with its tag and a `hasText` flag.
- On sync, every note with `hasText == false` goes up to
  `POST /api/v1/ai:transcribe`. The transcript is written to `note_NNN.txt` and
  the flag flips to true.
- The transcript is then published with `POST /api/v1/memos`:
  - content = `#Tag\n\n<transcript>` when the tag is meaningful — the literal
    tags `Note` and `Untagged` (and notes with no tag) are published without a
    `#` prefix;
  - visibility = the configured default (`PRIVATE`/`PROTECTED`/`PUBLIC`).
- The returned memo id (`name`) is stored in `note_NNN.meta` as `memo_name=…`
  (alongside `created_utc`, `tag`, `synced`), so the link is never lost.
- **Notes never go the other way:** deleting or editing the memo in Memos does
  not touch the device copy, and deleting a note on the device does not touch
  the memo.
- Failures are safe: a note whose transcription fails keeps `hasText == false`
  and retries next sync. Sync reports `done`, `SYNC PARTIAL` or `SYNC FAILED`.

## Tasks (device ↔ Memos, two-way)

A task is a **checkbox line inside any memo**:

```
- [ ] buy oat milk
- [x] water the plants
```

(`[ ]` / `[x]` without the dash are also recognized; `X` counts as done.)

- **Pull:** sync fetches the 30 most recent memos (`GET /api/v1/memos?pageSize=30`)
  and collects every checkbox line into the device task list. A task is keyed by
  `(memo id, line index)` — so editing line 3 of a memo updates that task, while
  inserting a line above it re-keys the tasks below.
- **Toggle on device:** flips the checkbox in RAM, marks it `dirty` and persists
  to `/notes/tasks.txt` immediately (works offline).
- **Push:** at the start of the next sync, each dirty task re-reads its memo
  (`GET /api/v1/<memo id>`), flips only that line's checkbox, and writes the
  whole content back (`PATCH …?updateMask=content`).
- **Conflict rule:** dirty (device-toggled) tasks survive a pull — the pull
  adopts the server's text but keeps the device's done state, and the push then
  writes that state to the server. Device wins.
- Tasks live in memos you own, so any memo in your timeline can host checklists
  (e.g. a pinned `#tasks` memo).

## Config

Everything is stored on the SD card in `/notes/config.txt` (Wi-Fi, Memos URL,
token, visibility, timezone, portal PIN), edited via the portal `/setup` page or
BLE. `secrets.h` only seeds the very first boot.

## Time & housekeeping

- Each sync first sets the clock from NTP, applies the configured timezone, then
  talks to Memos. Note timestamps on the portal render in local time.
- Health check order on every sync: Wi-Fi → NTP → Memos API health
  (reachability + token) → tasks push/pull → note transcription/publish. Failing
  stages abort the sync with a specific on-screen error.
