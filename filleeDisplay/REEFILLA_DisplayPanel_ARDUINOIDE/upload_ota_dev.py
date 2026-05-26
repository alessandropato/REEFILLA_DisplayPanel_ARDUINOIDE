#!/usr/bin/env python3
"""Carica il firmware compilato su GitHub Pages (repo ESP32_OTA_ALINO).

Percorso atteso del binario:
  build/esp32.esp32.waveshare_esp32_s3_touch_lcd_4/REEFILLA_DisplayPanel_ARDUINOIDE.ino.bin

Destinazione su GitHub:
  reefilla-display-fw/firmware.bin
  reefilla-display-fw/version.json
"""

import base64, json, os, re, subprocess, sys
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError

ROOT = Path(__file__).parent

# ── Configurazione ────────────────────────────────────────────────────────────

GITHUB_OWNER  = "alessandropato"
GITHUB_REPO   = "ESP32_OTA_ALINO"
GITHUB_BRANCH = "main"
GITHUB_DIR    = "reefilla-display-fw"

BIN_PATH = ROOT / "build/esp32.esp32.waveshare_esp32_s3_touch_lcd_4/REEFILLA_DisplayPanel_ARDUINOIDE.ino.bin"

# ── Token ─────────────────────────────────────────────────────────────────────

def _get_token():
    if t := os.environ.get("GITHUB_TOKEN"):
        return t
    r = subprocess.run(
        ["git", "credential", "fill"],
        input="protocol=https\nhost=github.com\n\n",
        capture_output=True, text=True, cwd=ROOT,
    )
    for line in r.stdout.splitlines():
        if line.startswith("password="):
            return line.split("=", 1)[1].strip()
    sys.exit("Nessuna credenziale GitHub trovata. Esegui 'git push' una volta per salvarle.")

GITHUB_TOKEN = _get_token()

HEADERS = {
    "Authorization": f"Bearer {GITHUB_TOKEN}",
    "Accept": "application/vnd.github+json",
    "X-GitHub-Api-Version": "2022-11-28",
    "Content-Type": "application/json",
}

# ── GitHub API ────────────────────────────────────────────────────────────────

def gh_sha(path):
    url = f"https://api.github.com/repos/{GITHUB_OWNER}/{GITHUB_REPO}/contents/{path}?ref={GITHUB_BRANCH}"
    try:
        with urlopen(Request(url, headers=HEADERS)) as resp:
            return json.loads(resp.read()).get("sha")
    except HTTPError:
        return None

def gh_put(path, data, message):
    body = {
        "message": message,
        "content": base64.b64encode(data).decode(),
        "branch": GITHUB_BRANCH,
    }
    sha = gh_sha(path)
    if sha:
        body["sha"] = sha
    url = f"https://api.github.com/repos/{GITHUB_OWNER}/{GITHUB_REPO}/contents/{path}"
    req = Request(url, data=json.dumps(body).encode(), headers=HEADERS, method="PUT")
    with urlopen(req) as resp:
        resp.read()
    print(f"  -> {path}")

# ── Leggi versione da config.h ────────────────────────────────────────────────

def read_version():
    text = (ROOT / "config.h").read_text()
    m = re.search(r'#define\s+DISPLAY_FIRMWARE_VERSION\s+"([^"]+)"', text)
    if not m:
        sys.exit("DISPLAY_FIRMWARE_VERSION non trovata in config.h")
    return m.group(1)

# ── Main ──────────────────────────────────────────────────────────────────────

version = read_version()
print(f"Versione: {version}")

if not BIN_PATH.exists():
    sys.exit(f"Binario non trovato: {BIN_PATH}\nCompila prima con Arduino IDE.")

print(f"Binario: {BIN_PATH} ({BIN_PATH.stat().st_size:,} byte)")

commit_msg = f"ota reefilla-display v{version}"

print(f"\n[UPLOAD] {GITHUB_DIR}/")
gh_put(f"{GITHUB_DIR}/version.json",
       json.dumps({"version": version}).encode(),
       commit_msg)
gh_put(f"{GITHUB_DIR}/firmware.bin",
       BIN_PATH.read_bytes(),
       commit_msg)

print(f"\nDone. URL aggiornamento:")
print(f"  https://alessandropato.github.io/{GITHUB_REPO}/{GITHUB_DIR}/version.json")
print(f"  https://alessandropato.github.io/{GITHUB_REPO}/{GITHUB_DIR}/firmware.bin")
