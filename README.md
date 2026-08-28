# pala-memos

**Memos integration add-on for the Pala Note.** The Pala Note is a palm-sized ESP32-S3 voice-note device with a 200×200 e-paper display. This add-on sends voice notes to your self-hosted [Memos](https://www.usememos.com/) server. Memos transcribes them with AI. The add-on publishes them as memos with tags. It also syncs a two-way task list with the checkbox lines inside your memos.

This repository contains only my own additions: the Memos module, the config storage layer, the optional Bluetooth config, and the integration docs. It is a drop-in add-on, not a fork of the Pala Note firmware.

> **Credit:** The Pala Note device and its base firmware are **© Paul Lagier**
> ([paullagier.craft.me](https://paullagier.craft.me)), sold for personal use.
> This add-on runs on that firmware but does not redistribute any of it.
> See [`INTEGRATION.md`](INTEGRATION.md) to install it into your own copy.

---

## Requirements

- **A Pala Note device** with the Waveshare ESP32-S3-ePaper-1.54 board (ESP32-S3, 200×200 e-paper, mic, SD card, Wi-Fi + BLE).
- **Your own copy of the Pala Note firmware** (© Paul Lagier, [paullagier.craft.me](https://paullagier.craft.me)). This add-on does not include it.
- **Arduino IDE** or `arduino-cli` with **ESP32 core 3.x** installed.
- **A partition scheme with a large app partition** — "Minimal SPIFFS 1.9MB APP" or "Huge APP 3MB No OTA". The stock default 1.25MB is too small.
- **A Memos server** you control (self-hosted or hosted). The server must expose the v1 API, including `POST /api/v1/ai:transcribe` for transcription. See [`docs/memos_server_setup.md`](docs/memos_server_setup.md).
- **An SD card** on the device (all data lives there — notes, config, tasks).

## What it does

- **AI transcription, self-hosted.** The add-on sends each recording to your Memos server (`POST /api/v1/ai:transcribe`). The transcript lands on the SD card next to the audio file.
- **Auto-publish.** Each transcribed note becomes a memo. The memo starts with the note tag (`#Work`, `#Idea`, and so on). The add-on stores the memo ID in the note metadata.
- **Two-way tasks.** The add-on reads checkbox lines (`- [ ]`, `- [x]`) inside your memos and shows them as a task list. When you toggle a task on the device, the next sync updates the memo.
- **No hardcoded config.** The `/setup` page in the device web portal holds all settings. Set the Wi-Fi, Memos URL, access token, visibility, timezone, and an optional portal PIN. The device stores them on the SD card. No reflash needed.
- **Health-checked sync.** Each sync checks the server and the token first. On an error, the screen shows the cause (`MEMOS 401 AUTH`, `MEMOS UNREACHABLE`, and so on).
- **Optional BLE config.** A GATT service exposes the device status as JSON. It accepts the same settings from a phone or from a Web Bluetooth page.
- **Optional hourly sync.** The device wakes from deep sleep each hour, syncs the new notes, and sleeps again.

## Repository layout

```
memos/                 the Memos module (drop into firmware src/app/memos/)
  memos.h  memos.cpp      transport, health check, transcription, publish, tasks
config_store/          SD-backed config (drop into src/app/)
  config_store.h  config_store.cpp
ble_service/           OPTIONAL Bluetooth config and status (drop into src/app/)
shtc3/                 OPTIONAL temp/humidity sensor module used by ble_service
extras/                certs.h + secrets_inc.h + secrets.example.h (sketch root)
docs/
  memos_server_setup.md   server prep: token, AI transcription, HTTPS
  sync_model.md           what data moves where, tag rules, conflicts
INTEGRATION.md         the exact edits for the base firmware
```

## Installing into a Pala Note

You need your own copy of the Pala Note firmware. Then follow [`INTEGRATION.md`](INTEGRATION.md). You copy the drop-in files and apply the listed edits to `types.h`, `globals.h`, `notes`, `rtc`, `network`, `ui`, and `pala_note.ino`. Build with a large app partition: "Minimal SPIFFS 1.9MB APP" or "Huge APP (3MB No OTA)".

The instructions target the stock v1.x firmware layout. A newer stock revision can shift the anchor text of individual edits. The list *"APIs used from the base firmware"* at the bottom of `INTEGRATION.md` shows the symbols that must exist.

## Status

Feature-complete. I use it on my device every day, together with the desk clock, the button engine, and power hardening. Those parts stay outside the scope of this add-on.

## License

My code and docs in this repository use the **MIT License** (see [`LICENSE`](LICENSE)). This license does not cover the Pala Note base firmware, the case files, or the build guide. They remain © Paul Lagier. Memos itself is © its authors ([usememos.com](https://www.usememos.com/)).
