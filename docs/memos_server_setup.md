# Preparing your Memos server

What the device needs from your Memos server, and how to set it up.

## Requirements

- You need a Memos server that you control. It can be self-hosted (Docker, binary)
  or hosted.
- The integration uses the v1 API:
  - `GET`, `PATCH`, and `POST /api/v1/memos` for the health check, the task sync,
    and publishing.
  - `POST /api/v1/ai:transcribe` to turn WAV files into text.
- The server must be reachable from the device Wi-Fi network. A LAN address is
  fine (`http://192.168.x.x:5230`).

## Access token (PAT)

1. In Memos, open **Settings → Access Tokens**.
2. Create a token. Copy it.
3. Enter the token on the device `/setup` page. You can also put it in `secrets.h`
   before the first flash.

The device sends the token as `Authorization: Bearer <token>`. Each call uses your
Memos user account. New notes belong to you and default to `PRIVATE` visibility.

## AI transcription

The integration sends audio to the Memos AI endpoint
`POST /api/v1/ai:transcribe`. Your Memos server must offer AI transcription. This
depends on the Memos version. Configure an AI backend in the Memos admin settings
if your version asks for one.

If the health check passes and transcription fails, check the AI endpoint first.
Notes with a failed transcription stay on the device. The next sync tries them
again. Nothing is lost.

## HTTPS notes

- The firmware verifies TLS with **ISRG Root X1** (Let's Encrypt). This covers the
  common Caddy, nginx, and Traefik setups.
- For a custom CA or a self-signed certificate, put the CA certificate (PEM) on the
  SD card at `/notes/ca.pem`. The firmware prefers this file over the built-in root.
- Plain HTTP on a trusted LAN also works (`http://host:port`).
- A base path works too. The URL parser handles `https://example.com/memos`.

## Recommended verification

1. On a desktop, send `GET https://your-memos/api/v1/memos?pageSize=1` with the
   Bearer token. Expect a `200` JSON response.
2. Configure the device only after this test passes. The device health check uses
   the same request. If curl passes, the device health check should pass too.
