## @file device_http.py
## @brief Open a URL on the clock with the admin token: every /api, /debug, /files and /ota
## request needs it (a phone gets a session by scanning the QR code instead). The token
## comes from OTA_API_TOKEN in the environment, else from the gitignored secrets header.
## @version 1
"""Shared by the device tools: `from device_http import device_urlopen`."""

import os
import re
import urllib.request
from pathlib import Path

SECRETS_H = (
    Path(__file__).resolve().parent.parent / "components" / "secrets" / "secrets.h"
)


## @brief The admin token: OTA_API_TOKEN from the environment or the secrets header.
## @utility
## @version 1
## @return The token, or "" when there is none (the clock will answer 401).
def admin_token():
    token = os.environ.get("OTA_API_TOKEN", "")
    if not token and SECRETS_H.exists():
        match = re.search(r'#define\s+OTA_API_TOKEN\s+"([^"]*)"', SECRETS_H.read_text())
        token = match.group(1) if match else ""
    return token


## @brief urllib.request.urlopen with the admin token added to @p req (a URL or a Request).
## @utility
## @version 1
## @return The response (a context manager, as urlopen's).
def device_urlopen(req, timeout=None):
    if isinstance(req, str):
        req = urllib.request.Request(req)
    token = admin_token()
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    return urllib.request.urlopen(req, timeout=timeout)
