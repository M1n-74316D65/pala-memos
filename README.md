# pala-memos

**usememos integration add-on for the Pala Note** — a palm-sized ESP32-S3
voice-note device with a 200×200 e-paper display. Voice notes are AI-transcribed
by your self-hosted [Memos](https://www.usememos.com/) server, published as memos
with their tag, and a two-way task list syncs with checkbox lines inside your
memos.

This repository contains **only my own additions** — the Memos module, the config
storage layer, optional Bluetooth config, and the integration docs. It is a
drop-in add-on, **not** a fork of the Pala Note firmware.

> **Credit:** The Pala Note device and its base firmware are **© Paul Lagier**
> ([paullagier.craft.me](https://paullagier.craft.me)), sold for personal use.
> This add-on is built on top of that firmware but does **not** redistribute any
> of it — see [`INTEGRATION.md`](INTEGRATION.md) for how to wire it into your
> own copy.

---

## What it does

- **AI transcription, self-hosted:** recordings on the SD card are sent to your
  Memos instance (`POST /api/v1/ai:transcribe`) and the transcript lands next to
  the WAV on the device.
- **Auto-publish:** each transcribed note becomes a memo, prefixed with its
  device tag (`#Work`, `#Idea`, …). The memo id is stored in the note's metadata.
- **Two-way tasks:** checkbox lines inside memos (`- [ ]`, `- [x]`) appear on the
  device as a task list; toggling on the device patches the memo back at the next
  sync.
- **No hardcoded config:** a `/setup` page in the device's web portal lets you set
  Wi-Fi, Memos URL, access token, default visibility, timezone and an optional
  portal PIN — all stored on the SD card, changeable without reflashing.
- **Health-checked sync:** sync checks reachability and token validity first and
  reports precise errors (`MEMOS 401 AUTH`, `MEMOS UNREACHABLE`, …) on screen.
- **Optional BLE config:** a GATT service exposes device status as JSON and accepts
  the same settings from a phone or any Web Bluetooth page.
- **Hourly background sync (optional):** the device wakes from deep sleep once an
  hour, syncs pending notes, and goes back to sleep.

## Repository layout

```
memos/                 the Memos module (drop into firmware src/app/memos/)
  memos.h  memos.cpp      transport, health, transcribe, publish, tasks sync
config_store/          SD-backed config (drop into src/app/)
  config_store.h  config_store.cpp
ble_service/           OPTIONAL Bluetooth config & status (drop into src/app/)
shtc3/                 OPTIONAL temp/humidity sensor module used by ble_service
extras/                certs.h + secrets_inc.h + secrets.example.h (sketch root)
docs/
  memos_server_setup.md   Memos server prep: token, AI transcription, HTTPS
  sync_model.md           what lands where, tag/visibility rules, conflicts
INTEGRATION.md         exact edits to add this to the base firmware
```

## Installing into a Pala Note

You need your own copy of the Pala Note firmware. Then follow
[`INTEGRATION.md`](INTEGRATION.md): copy the drop-in files, apply the listed
edits to `types.h`, `globals.h`, `notes`, `rtc`, `network`, `ui` and
`pala_note.ino`, and build with a **large app partition** (e.g. "Minimal SPIFFS
1.9MB APP" or "Huge APP (3MB No OTA)").

Instructions target the stock v1.x firmware layout. If your firmware is a newer
stock revision, the anchor text of individual edits may need fuzzy matching —
the *"APIs used from the base firmware"* list at the bottom of `INTEGRATION.md`
tells you which symbols must exist.

## Status

Feature-complete and in daily use on the author's device (with the desk clock,
button engine and power hardening that live outside this add-on's scope).

## License

My own code and docs in this repo are released under the **MIT License** (see
[`LICENSE`](LICENSE)). This does **not** cover the Pala Note base firmware, case
files, or build guide, which remain © Paul Lagier. Memos itself is © its
authors ([usememos.com](https://www.usememos.com/)).
