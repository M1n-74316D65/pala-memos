# Sync model

This page describes the data that moves between the device and the Memos server,
and the rules that govern it.

Short version: the device SD card is the source of truth for notes. The Memos
server is the source of truth for task text. The device wins a task conflict.

## Notes (device to server, one-way)

- A recording is `note_NNN.wav` on the SD card. The index `/notes/index.csv` holds
  the note tag and a `hasText` flag.
- On sync, each note with `hasText == false` goes to `POST /api/v1/ai:transcribe`.
  The device writes the transcript to `note_NNN.txt` and sets `hasText` to true.
- The device then publishes the transcript with `POST /api/v1/memos`:
  - The memo content is `#Tag\n\n<transcript>` when the tag is meaningful. The tags
    `Note` and `Untagged`, and notes without a tag, publish without the `#` prefix.
  - The visibility comes from the config (`PRIVATE`, `PROTECTED`, or `PUBLIC`).
- The device stores the memo ID (`name`) in `note_NNN.meta` as `memo_name`. The meta
  file also holds `created_utc`, `tag`, and `synced`. The link to the memo survives
  reboots.
- Notes never move from the server to the device. If you delete or edit the memo in
  Memos, the device copy stays the same. If you delete the note on the device, the
  memo stays.
- Failures are safe. A failed transcription keeps `hasText == false`. The next sync
  retries it. The screen reports `done`, `SYNC PARTIAL`, or `SYNC FAILED`.

## Tasks (two-way)

A task is a checkbox line inside a memo:

```
- [ ] buy oat milk
- [x] water the plants
```

(`[ ]` and `[x]` without the dash also count. `X` also means done.)

- **Pull:** the sync sends `GET /api/v1/memos?pageSize=30` to fetch the 30 newest
  memos. It collects each checkbox line into the device task list. A task key is
  `(memo ID, line index)`. When you edit line 3 of a memo, you update that task.
  When you insert a line above it, you change the keys of the tasks below.
- **Toggle on the device:** the device flips the checkbox in memory, marks the task
  `dirty`, and stores it in `/notes/tasks.txt`. This works offline.
- **Push:** at the start of the next sync, the device reads each dirty task memo
  (`GET /api/v1/<memo id>`). It flips only that checkbox line. It writes the full
  content back (`PATCH ...?updateMask=content`).
- **Conflict rule:** dirty tasks survive a pull. The pull takes the text from the
  server but keeps the device done state. The push then writes that state to the
  server. The device wins.
- Store your tasks in your own memos. Any memo can hold a checklist, for example a
  pinned `#tasks` memo.

## Config

The file `/notes/config.txt` on the SD card holds the settings: Wi-Fi, Memos URL,
token, visibility, timezone, and portal PIN. Edit them on the portal `/setup` page
or over BLE. `secrets.h` only seeds the first boot.

## Time and housekeeping

- Each sync gets the time from NTP, applies the configured timezone, and then talks
  to the Memos server. The portal shows note timestamps in local time.
- Each sync runs these stages in order: Wi-Fi, NTP, Memos health check, task push
  and pull, note transcription and publish. A failed stage stops the sync. The
  screen shows the cause.
