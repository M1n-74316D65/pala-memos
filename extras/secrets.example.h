#ifndef SECRETS_H
#define SECRETS_H

/* Copy to secrets.h (gitignored) and fill in. If secrets.h is missing,
 * firmware falls back to this file so a clean clone still compiles.
 *
 * TLS defaults to ISRG Root X1 (Let's Encrypt). For another CA, put a
 * PEM at /notes/ca.pem on the SD card.
 *
 * If a real secrets.h was ever committed, rotate the Wi-Fi password and
 * Memos PAT. Removing the file does not erase git history.
 */
#define WIFI_SSID            "YOUR_WIFI_SSID"
#define WIFI_PASS            "YOUR_WIFI_PASSWORD"
#define MEMOS_INSTANCE_URL   "https://memos.example.com"
#define MEMOS_ACCESS_TOKEN   "your_pat_token_here"
#define MEMOS_VISIBILITY     "PRIVATE"

#endif
