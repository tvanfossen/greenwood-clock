# Secure OTA Updates with HTTPS and Certificate Verification

**Status**: Backlog
**Priority**: High (Security)
**Estimated Effort**: Small (1-2 days)
**Created**: 2025-12-02

## Problem Statement

The current OTA implementation uses plain HTTP without any authentication or encryption. This creates several security risks:

**Current Setup:**
```
Device ----HTTP----> OTA Server (192.168.1.100:8000)
          (no encryption)
          (no authentication)
          (no integrity check)
```

**Security Issues:**
1. **No Encryption**: Firmware transmitted in plain text over WiFi
2. **No Authentication**: Any HTTP server on the network could serve malicious firmware
3. **No Integrity**: Firmware could be tampered with in transit (MITM attack)
4. **Warning in Logs**: `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` produces security warnings

**Current Warning:**
```
W (26965) esp_https_ota: Continuing with insecure option because CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP is set.
```

While acceptable for local development on a trusted network, this is **not suitable for production** or even home use if security is a concern.

## User Feedback

From OTA testing session (2025-12-02):
> "Honestly its not a bad idea to have a cert saved off locally for the server to use in authenticating OTAs, create a separate proposal for it, to resolve that warning"

## Proposed Solution

### 1. HTTPS OTA Server with Self-Signed Certificate

**Architecture:**

```
Device ----HTTPS----> OTA Server (192.168.1.100:8443)
      (TLS 1.2+)
      (Certificate validation)
      (Encrypted firmware)
```

**Components:**
1. **Self-signed CA certificate** - Created once, stored on device
2. **Server certificate** - Signed by CA, used by OTA server
3. **Updated OTA server** - Python HTTPS server with SSL/TLS
4. **Updated device code** - Certificate verification enabled

### 2. Certificate Generation

**One-time Setup (Development):**

Create a local Certificate Authority (CA):

```bash
#!/bin/bash
# tools/generate_ota_certs.sh

# Create CA private key
openssl genrsa -out ca_key.pem 2048

# Create CA certificate (valid 10 years)
openssl req -new -x509 -days 3650 -key ca_key.pem -out ca_cert.pem \
  -subj "/C=US/ST=State/L=City/O=Greenwood Clock/CN=Greenwood OTA CA"

# Create server private key
openssl genrsa -out server_key.pem 2048

# Create server certificate signing request
openssl req -new -key server_key.pem -out server_csr.pem \
  -subj "/C=US/ST=State/L=City/O=Greenwood Clock/CN=greenwood-ota"

# Sign server certificate with CA (valid 1 year)
openssl x509 -req -days 365 -in server_csr.pem \
  -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial \
  -out server_cert.pem

echo "Certificates created:"
echo "  CA Certificate:     ca_cert.pem     (embed in device firmware)"
echo "  Server Certificate: server_cert.pem (use in OTA server)"
echo "  Server Key:        server_key.pem  (use in OTA server)"
```

**Output Files:**
```
tools/certs/
├── ca_cert.pem          # Embedded in device firmware (public)
├── ca_key.pem           # Keep secret! (only for signing new certs)
├── server_cert.pem      # Used by OTA server (public)
└── server_key.pem       # Used by OTA server (keep secret!)
```

### 3. Embed CA Certificate in Firmware

**Convert certificate to C array:**

```bash
# tools/cert_to_c.py
#!/usr/bin/env python3
import sys

def cert_to_c_array(cert_path, output_path):
    with open(cert_path, 'rb') as f:
        cert_data = f.read()

    with open(output_path, 'w') as f:
        f.write('// Auto-generated CA certificate\n')
        f.write('// DO NOT EDIT - Generated from ca_cert.pem\n\n')
        f.write('const char ota_ca_cert_pem[] = \n')

        for i in range(0, len(cert_data), 16):
            chunk = cert_data[i:i+16]
            hex_str = ' '.join(f'0x{b:02x},' for b in chunk)
            f.write(f'    {hex_str}\n')

        f.write(';\n')
        f.write(f'const size_t ota_ca_cert_pem_len = {len(cert_data)};\n')

if __name__ == '__main__':
    cert_to_c_array('tools/certs/ca_cert.pem',
                   'components/ota/ota_ca_cert.h')
```

**Or use ESP-IDF's built-in approach:**

```c
// components/ota/ota_ca_cert.h
extern const uint8_t ota_ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t ota_ca_cert_pem_end[]   asm("_binary_ca_cert_pem_end");
```

```cmake
# components/ota/CMakeLists.txt
target_add_binary_data(${COMPONENT_LIB} "certs/ca_cert.pem" TEXT)
```

### 4. Updated OTA Server (HTTPS)

**Enhanced Python server:**

```python
#!/usr/bin/env python3
# tools/ota_server_https.py

import http.server
import ssl
import os
import sys

DEFAULT_PORT = 8443
DEFAULT_FIRMWARE_PATH = "build/greenwood-clock.bin"
CERT_PATH = "tools/certs/server_cert.pem"
KEY_PATH = "tools/certs/server_key.pem"

class OTAHandler(http.server.SimpleHTTPRequestHandler):
    firmware_path = DEFAULT_FIRMWARE_PATH

    def do_GET(self):
        if self.path == '/greenwood-clock.bin':
            self.serve_firmware()
        else:
            self.send_error(404)

    def do_HEAD(self):
        if self.path == '/greenwood-clock.bin':
            if os.path.exists(self.firmware_path):
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', os.path.getsize(self.firmware_path))
                self.end_headers()
        else:
            self.send_error(404)

    def serve_firmware(self):
        if not os.path.exists(self.firmware_path):
            self.send_error(404, "Firmware not found")
            return

        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', os.path.getsize(self.firmware_path))
        self.end_headers()

        with open(self.firmware_path, 'rb') as f:
            chunk_size = 4096
            while chunk := f.read(chunk_size):
                self.wfile.write(chunk)

        print(f"[INFO] Served firmware to {self.client_address[0]}")

def main():
    # Check if certificates exist
    if not os.path.exists(CERT_PATH) or not os.path.exists(KEY_PATH):
        print("ERROR: SSL certificates not found!")
        print(f"  Expected: {CERT_PATH}")
        print(f"            {KEY_PATH}")
        print("\nRun: bash tools/generate_ota_certs.sh")
        sys.exit(1)

    if not os.path.exists(DEFAULT_FIRMWARE_PATH):
        print(f"WARNING: Firmware not found at {DEFAULT_FIRMWARE_PATH}")
        print("Build firmware first: idf.py build")

    # Create HTTPS server
    server = http.server.HTTPServer(('0.0.0.0', DEFAULT_PORT), OTAHandler)

    # Wrap with SSL/TLS
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(CERT_PATH, KEY_PATH)
    context.minimum_version = ssl.TLSVersion.TLSv1_2  # Require TLS 1.2+

    server.socket = context.wrap_socket(server.socket, server_side=True)

    print("=" * 70)
    print("Greenwood Clock Secure OTA Server (HTTPS)")
    print("=" * 70)
    print(f"Firmware:    {DEFAULT_FIRMWARE_PATH}")
    print(f"Size:        {os.path.getsize(DEFAULT_FIRMWARE_PATH):,} bytes" if os.path.exists(DEFAULT_FIRMWARE_PATH) else "N/A")
    print(f"Port:        {DEFAULT_PORT}")
    print(f"Protocol:    HTTPS (TLS 1.2+)")
    print(f"Certificate: {CERT_PATH}")
    print(f"")
    print(f"OTA URL:     https://192.168.1.100:{DEFAULT_PORT}")
    print("=" * 70)
    print("\nWaiting for OTA requests... (Ctrl+C to stop)")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n\nShutting down server...")
        server.shutdown()

if __name__ == '__main__':
    main()
```

### 5. Updated Device OTA Code

**Modified `components/ota/ota.c`:**

```c
#include "ota.h"
#include "ota_ca_cert.h"  // Embedded CA certificate
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include <string.h>

static const char* TAG = "ota";

esp_err_t ota_perform_update(const char* server_url,
                              ota_progress_cb_t progress_cb,
                              void* user_data) {
    if (server_url == NULL) {
        set_error("Server URL is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Build firmware URL (HTTPS)
    char url[256];
    snprintf(url, sizeof(url), "%s%s", server_url, OTA_FIRMWARE_PATH);

    ESP_LOGI(TAG, "Starting secure OTA update from: %s", url);

    // Configure HTTP client with certificate verification
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = OTA_READ_TIMEOUT_MS,
        .keep_alive_enable = true,
        .buffer_size = OTA_BUFFER_SIZE,

        // SECURITY: Enable certificate verification
        .cert_pem = (const char*)ota_ca_cert_pem_start,
        .cert_len = ota_ca_cert_pem_end - ota_ca_cert_pem_start,

        // Require valid certificate
        .skip_cert_common_name_check = false,  // Changed from true!
    };

    // Rest of OTA code unchanged...
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        set_error("OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    // ... download and flash logic ...
}
```

**Modified `sdkconfig`:**

```
# Disable insecure HTTP OTA
# CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP is not set

# Enable secure HTTPS OTA
CONFIG_ESP_HTTPS_OTA_VERIFY_SERVER=y
```

### 6. Settings Screen Update

**Update OTA server URL to HTTPS:**

```c
// components/ui/screen_manager.c
static char ota_server_url[128] = "https://192.168.1.100:8443";  // Changed port
```

**UI Changes:**
- Display "HTTPS" badge in OTA settings screen
- Show certificate validity status
- Show "Secure Update" indicator during OTA

## Implementation Phases

### Phase 1: Certificate Infrastructure (0.5 days)
- Create certificate generation script
- Generate CA and server certificates
- Add certificate embedding to build system
- Test certificate loading

### Phase 2: HTTPS OTA Server (0.5 days)
- Implement Python HTTPS server
- Add SSL/TLS configuration
- Test with curl/browser
- Document server usage

### Phase 3: Device Integration (0.5 days)
- Embed CA certificate in firmware
- Update OTA code for HTTPS
- Remove HTTP fallback
- Test end-to-end HTTPS OTA

### Phase 4: Polish & Documentation (0.5 days)
- Update OTA_USAGE.md
- Add certificate renewal guide
- Add troubleshooting section
- Test with multiple devices

## Technical Considerations

### Certificate Validity

**CA Certificate:**
- Validity: 10 years
- Purpose: Sign server certificates
- Storage: Embedded in firmware, also kept offline for renewals

**Server Certificate:**
- Validity: 1 year
- Purpose: Authenticate OTA server
- Renewal: Generate new cert annually, device doesn't need update (CA still valid)

**Certificate Renewal Workflow:**
```bash
# Generate new server cert (after 1 year)
openssl genrsa -out server_key_new.pem 2048
openssl req -new -key server_key_new.pem -out server_csr_new.pem \
  -subj "/C=US/ST=State/L=City/O=Greenwood Clock/CN=greenwood-ota"
openssl x509 -req -days 365 -in server_csr_new.pem \
  -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial \
  -out server_cert_new.pem

# Replace old server cert
mv server_cert_new.pem server_cert.pem
mv server_key_new.pem server_key.pem

# No firmware update needed!
```

### Performance Impact

**Memory:**
- CA certificate: ~1.5 KB embedded in firmware
- TLS handshake: ~4 KB RAM during OTA
- Total: ~5.5 KB (negligible)

**Speed:**
- TLS handshake: ~200-500ms (one-time)
- Download: Same speed as HTTP (encrypted on-the-fly)
- **Total OTA time increase: <1 second**

### Security Benefits

**vs HTTP:**
- ✅ Encrypted firmware transmission
- ✅ Server authentication (prevents MITM)
- ✅ Certificate pinning (only trust our CA)
- ✅ TLS 1.2+ (modern crypto)

**Attack Resistance:**
- **MITM Attack**: Prevented (certificate validation)
- **Rogue OTA Server**: Prevented (only accepts our CA-signed certs)
- **Firmware Tampering**: Detected (TLS integrity checks)
- **Eavesdropping**: Prevented (encryption)

### Certificate Storage Security

**CA Private Key (`ca_key.pem`):**
- ⚠️ **CRITICAL**: Keep secure offline
- Used only for signing new server certificates
- If compromised: Attacker can create rogue OTA servers
- **Mitigation**: Store on encrypted USB drive, not on development machine

**Server Private Key (`server_key.pem`):**
- ⚠️ **IMPORTANT**: Keep secure
- Used by OTA server
- If compromised: Attacker can impersonate OTA server
- **Mitigation**: Restrict file permissions (`chmod 600`)

**CA Certificate (`ca_cert.pem`):**
- ✅ **PUBLIC**: Safe to share
- Embedded in all devices
- Used to verify server certificates

## Testing Plan

### Unit Testing
- Certificate loading from flash
- TLS handshake with valid cert
- TLS handshake with invalid cert (should fail)
- Certificate expiration check

### Integration Testing
1. **Valid HTTPS OTA**:
   - Start HTTPS server with valid cert
   - Initiate OTA from device
   - Verify encrypted download
   - Verify successful flash and reboot

2. **Invalid Certificate Tests**:
   - Expired certificate → OTA should fail
   - Wrong CA → OTA should fail
   - Self-signed (not our CA) → OTA should fail

3. **Network Security Tests**:
   - MITM proxy → OTA should fail
   - HTTP server (not HTTPS) → OTA should fail

### Manual Testing on Hardware
- Generate certificates on development machine
- Start HTTPS OTA server
- Trigger OTA from device
- Verify no warnings in logs
- Verify successful update
- Test certificate renewal (1-year expiry simulation)

## User Experience

### Workflow: Secure OTA Update (User)

**One-time Setup:**
1. Run certificate generation script (developer does this once)
2. Certificates embedded in firmware automatically

**Regular OTA Updates:**
1. Developer builds new firmware
2. Starts HTTPS OTA server: `python tools/ota_server_https.py`
3. User swipes up to settings on device
4. Taps "Software Update"
5. Taps "Check for Update"
6. Sees "Secure Update" indicator (lock icon)
7. Progress bar shows encrypted download
8. Device reboots with new firmware

**Time: Same as HTTP OTA (~30-60 seconds)**

### Workflow: Certificate Renewal (Annual)

1. Developer generates new server cert (1-year expiry)
2. Restarts OTA server with new cert
3. Devices continue working (CA cert still valid for 10 years)
4. No firmware updates needed

**Time: 2 minutes**

## Success Metrics

- HTTPS OTA completes successfully
- No security warnings in device logs
- Certificate validation works (rejects invalid certs)
- TLS handshake adds <500ms to OTA time
- Certificate renewal works without firmware update
- Documentation clear enough for non-crypto developers

## Migration from HTTP

### For Existing Devices

**Option 1: One-time USB Flash**
- Flash firmware with embedded CA cert
- All future updates via HTTPS OTA

**Option 2: Hybrid Transition**
- Keep HTTP OTA enabled temporarily
- Use HTTP to push HTTPS-enabled firmware
- Disable HTTP in next update

**Recommendation: Option 1** (clean break, more secure)

### Configuration Changes

```diff
- CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
+ # CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP is not set

- static char ota_server_url[128] = "http://192.168.1.100:8000";
+ static char ota_server_url[128] = "https://192.168.1.100:8443";
```

## Future Enhancements

### Phase 5: Firmware Signing
- Sign firmware with private key
- Verify signature before flashing
- Prevents flashing of unsigned/tampered firmware

### Phase 6: Mutual TLS (mTLS)
- Device also has a certificate
- Server verifies device identity
- Prevents unauthorized devices from downloading firmware

### Phase 7: Automatic Certificate Updates
- Distribute new CA cert via OTA
- Graceful certificate rotation
- Zero-downtime cert updates

## Dependencies

- OpenSSL (for certificate generation)
- Python 3.6+ with SSL support
- ESP-IDF HTTPS OTA component
- ESP-IDF mbedTLS (for TLS on device)

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| CA key compromise | Critical - all security lost | Store CA key offline, encrypted USB drive |
| Certificate expires | OTA stops working | Set 1-year renewal reminders, monitor expiry |
| TLS handshake fails | OTA fails | Add retry with exponential backoff |
| Certificate too large | Flash overflow | Use EC certificates (smaller), or increase partition |
| Wrong certificate embedded | All OTA fails | Add certificate validation in build process |

## References

- ESP-IDF HTTPS OTA: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_https_ota.html
- OpenSSL Certificate Generation: https://www.openssl.org/docs/man1.1.1/man1/openssl-req.html
- TLS 1.2 Specification: https://tools.ietf.org/html/rfc5246
- Related proposal: `local-network-ota.md`

## Open Questions

1. Should we support Let's Encrypt certificates for production?
   - **Recommendation**: No, self-signed is simpler for local network

2. What should happen if certificate expires during OTA?
   - **Recommendation**: Fail gracefully, show error message, don't brick device

3. Should we add certificate pinning (hash-based)?
   - **Recommendation**: Phase 5 enhancement, not MVP

4. How to handle clock not set (breaks certificate validation)?
   - **Recommendation**: Require SNTP sync before OTA, fallback to NVS time

## Approval Checklist

- [ ] Certificate generation script tested
- [ ] HTTPS server tested with real certificates
- [ ] Device can validate certificates correctly
- [ ] Performance impact acceptable (<500ms handshake)
- [ ] Security reviewed (CA key storage, cert renewal)
- [ ] Documentation complete (setup, usage, renewal)
- [ ] Migration plan from HTTP validated
- [ ] Certificate expiry monitoring planned

---

**Next Steps After Approval:**
1. Create certificate generation script
2. Generate development certificates
3. Implement HTTPS OTA server
4. Embed CA certificate in firmware
5. Update device OTA code for HTTPS
6. Test end-to-end secure OTA
7. Update OTA_USAGE.md with HTTPS instructions
8. Disable HTTP OTA fallback
