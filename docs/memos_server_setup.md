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
`POST /api/v1/ai:transcribe`. Your Memos server must offer AI transcription.
This requires **Memos v0.27.0 or later**.

Memos delegates transcription to an external AI provider. You configure the
provider in the Memos admin settings under **AI**. Two provider types are
supported:

| Provider | Default model | How it works |
|----------|---------------|--------------|
| **OpenAI** | `whisper-1` | Dedicated speech-to-text endpoint (`/audio/transcriptions`) |
| **Gemini** | `gemini-2.5-flash` | Multimodal audio LLM (`generateContent`) |

### OpenAI provider (Whisper and compatible)

Set the provider type to `OPENAI` and enter your API key. The default model is
`whisper-1`. You can also use these OpenAI models:

- `gpt-4o-transcribe`
- `gpt-4o-mini-transcribe`
- `gpt-4o-transcribe-diarize`

The OpenAI provider also works with **any OpenAI-compatible endpoint**. Set a
custom endpoint URL in the provider config. Known compatible services:

- **Groq Whisper** (`https://api.groq.com/openai/v1`)
- **Self-hosted faster-whisper** (any endpoint that follows the OpenAI
  transcription API contract)
- **Azure OpenAI Whisper**

### Gemini provider

Set the provider type to `GEMINI` and enter your Google AI API key. The default
model is `gemini-2.5-flash`. You can also use `gemini-2.5-pro`.

Gemini handles audio through its multimodal generation API, not a dedicated
transcription endpoint. Memos transcodes WebM/Opus to WAV before sending. The
device already sends WAV, so no transcoding happens on the Memos side.

### Transcription settings

You can set these optional fields in the Memos AI settings:

- **Language** — an ISO 639-1 code (for example, `en`, `de`, `fr`) to hint the
  spoken language. Leave empty for auto-detect.
- **Prompt** — a spelling or vocabulary hint for the model. This helps with
  names, technical terms, or domain-specific words.

### Troubleshooting

If the health check passes and transcription fails, check the AI provider first.
Common causes:

- No AI provider configured in Memos admin settings.
- Wrong API key or expired key.
- The provider endpoint is unreachable from the Memos server.
- Audio size exceeds 25 MiB (the Memos limit).

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
