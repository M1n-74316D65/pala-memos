# Preparing your Memos server

What the device needs from your Memos instance, and how to set it up.

## Requirements

- A **Memos** instance you control — self-hosted (Docker, binary, …) or a hosted
  one. The integration talks to the v1 API:
  - `GET/PATCH/POST /api/v1/memos` — health check, task sync, publishing
  - `POST /api/v1/ai:transcribe` — WAV → text
- The instance must be reachable from the device's Wi-Fi network
  (LAN IP/hostname is fine — `http://192.168.x.x:5230` works).

## Access token (PAT)

1. In Memos, open **Settings → Access Tokens** (or My Account → Access Tokens).
2. Create a token and copy it.
3. Put it in the device's `/setup` page (or in `secrets.h` before first flash).

The device sends it as `Authorization: Bearer <token>`. Everything runs under
your own user, so published notes are yours and default to `PRIVATE` visibility.

## AI transcription

Note transcription uses the Memos **AI API** (`/api/v1/ai:transcribe`), so the
instance needs AI transcription available/enabled (this depends on your Memos
version — recent releases include the transcription endpoint; configure the AI
backend in the Memos admin settings if your version asks for one).

If a sync shows `MEMOS 404 URL` or fails only at the transcription step while
the health check and task sync pass, the AI endpoint is the first place to look.
Untranscribed notes stay on the device and retry at the next sync — nothing is
lost.

## HTTPS notes

- The firmware verifies TLS against **ISRG Root X1** (Let's Encrypt), which
  covers typical Caddy/nginx/Traefik setups with automatic certificates.
- **Custom CA / self-signed cert:** put that CA's certificate (PEM) on the SD
  card at `/notes/ca.pem` — the firmware prefers it over the built-in root.
- **Plain HTTP** on a trusted LAN works too (`http://host:port`).

A **base path** is supported: `https://example.com/memos` (proxying a sub-path)
is handled by the URL parser.

## Recommended quickly-verifiable setup

1. `GET https://your-memos/api/v1/memos?pageSize=1` with the Bearer token from a
   desktop → expect `200` JSON.
2. Only then configure the device. The device's sync does exactly this same call
   as its health check, so if curl passes, the device health check should too.
