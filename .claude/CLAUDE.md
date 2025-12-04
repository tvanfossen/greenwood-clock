# Claude Code Instructions for Greenwood Clock Project

## Project Overview

This is an ESP32-P4 based smart clock with touchscreen UI, weather display, OTA updates, and WiFi connectivity.

**Hardware**: ESP32-P4 Function EV Board with 1024x600 MIPI DSI display and GT911 touchscreen

**Framework**: ESP-IDF v5.5

## Important Guidelines

### DO NOT Build the Project

**The user will run builds themselves.**

❌ **DO NOT** run `idf.py build` unless explicitly requested
❌ **DO NOT** run builds "to test" or "to verify"
✅ **DO** make code changes and let the user build
✅ **DO** answer questions about build errors if they occur

**Why**: Builds take time and resources. The user has their own build workflow and will compile when ready.

### Code Changes

When making changes to source code:
- ✅ Make the changes directly
- ✅ Explain what changed and why
- ✅ Note any files modified
- ❌ Don't run builds to "verify" the changes

### Testing and Verification

- User will test on actual hardware
- User will report results and errors
- Respond to error messages and logs with fixes
- Don't assume success - wait for user confirmation

### Build-Related Tasks

**Only build if the user explicitly says:**
- "Build this"
- "Run idf.py build"
- "Compile the firmware"
- "Make a build for testing"

**Don't build for:**
- "Fix this bug" → Just fix the code
- "Add this feature" → Just add the code
- "Update this component" → Just update the code
- "Does this look right?" → Review the code, don't build

## Project Structure

```
greenwood-clock/
├── components/
│   ├── astronomy/       # Moon phase, astronomy data
│   ├── fonts/           # Custom LVGL fonts (Nunito)
│   ├── images/          # Splash screen, assets
│   ├── lottie/          # Lottie animation support
│   ├── network/         # WiFi, SNTP, scanning
│   ├── ota/             # OTA firmware updates
│   ├── secrets/         # API keys (not in git)
│   ├── settings/        # NVS-based persistent settings
│   ├── time_sync/       # Timezone, time utilities
│   ├── ui/              # LVGL UI, screen manager
│   └── weather/         # Weather API integration
├── main/                # Main application
├── spiffs/              # SPIFFS partition (animations, fonts)
├── tools/               # Build/OTA tools
└── .claude/             # Claude Code agent workspace
    └── AGENTS/
        └── PROPOSALS/   # Feature proposals
            ├── BACKLOG/ # Not started
            ├── STAGED/  # In progress or completed
            └── COMPLETED/ # Fully tested and verified
```

## Key Technologies

- **LVGL v9.3**: UI framework (60 FPS, double buffering enabled)
- **ESP-IDF v5.5**: Development framework
- **SNTP**: Time synchronization (multi-server with fallback)
- **NVS**: Non-volatile storage for settings
- **OTA**: Over-the-air firmware updates (HTTP currently, HTTPS proposed)
- **SPIFFS**: File system for assets

## Common Tasks

### Adding New Components

1. Create directory in `components/`
2. Add `CMakeLists.txt` with dependencies
3. Create `.h` and `.c` files
4. Update dependent components' CMakeLists.txt
5. User will build and test

### Modifying UI

- Main clock screen: `components/ui/ui.c`
- Settings screens: `components/ui/screen_manager.c`
- Screen navigation: `screen_manager_push()`, `screen_manager_pop()`
- Animations: 350ms duration, 60 FPS refresh

### Updating Settings

- Structure: `components/settings/settings.h` - `clock_settings_t`
- Storage: `components/settings/settings.c` - NVS-based
- Default values in `settings_load()` when NVS empty

### Network Operations

- WiFi init: `network_init_infrastructure()` (no credentials) or `network_init()` (with credentials)
- Scanning: `network_scan()`
- Connection: `network_connect()`
- SNTP: Auto-starts on WiFi connection via event handlers

## Current Configuration

### LVGL Settings
- Refresh period: 16ms (60 FPS)
- Double buffering: Enabled
- Buffer size: 1024 × 50 pixels
- Direct mode: Enabled

### Display
- Resolution: 1024x600
- Interface: MIPI DSI
- Touchscreen: GT911 (I2C)

### Partitions
- Factory: 4 MB
- OTA_0: 4 MB
- OTA_1: 4 MB
- Storage (SPIFFS): 3 MB

### Network
- WiFi: STA mode
- SNTP: 4-server pool with exponential backoff
- OTA: HTTP (port 8000), HTTPS proposed

## Proposals System

Located in `.claude/AGENTS/PROPOSALS/`

### Directories

- **BACKLOG/**: New ideas, not started
- **STAGED/**: Implemented but not fully tested on hardware
- **COMPLETED/**: Tested and verified working

### Creating Proposals

When user requests a feature:
1. Create detailed markdown in `BACKLOG/`
2. Include: problem, solution, implementation phases, testing, risks
3. Reference related proposals
4. Wait for user approval before implementing

### Proposal Lifecycle

```
BACKLOG → (user approves) → Implementation → STAGED → (user tests) → COMPLETED
```

**Important**: Don't move proposals to COMPLETED until user confirms hardware testing passed.

## Git Workflow

- Main branch: `main`
- Commits: User handles commits
- You may: Read git status, show diffs, explain changes
- You may not: Create commits (unless explicitly asked)

## Error Handling

When user reports errors:
1. Read the full error log
2. Identify root cause
3. Explain the issue clearly
4. Provide fix with code changes
5. Let user rebuild and test

## Security Considerations

- API keys: Never commit to git (use `components/secrets/`)
- OTA: Currently HTTP (insecure, local dev only)
- WiFi: Credentials stored in NVS (encrypted partition recommended for production)
- See `secure-ota-https.md` proposal for production security

## Useful Commands (User Runs These)

```bash
# Build
idf.py build

# Flash via USB
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# OTA server (HTTP)
python tools/ota_server.py

# Clean build
idf.py fullclean && idf.py build
```

## Current Status

**Last Hardware Test**: 2025-12-02

**Working**:
- ✅ Touch input (GT911, I2C)
- ✅ Swipe up gesture → settings
- ✅ WiFi scanning and connection
- ✅ Settings persistence (NVS)
- ✅ Brightness control
- ✅ Screen transitions (60 FPS, double buffered)
- ✅ OTA updates (HTTP)

**Tested, Needs Refinement**:
- ⚠️ Animation smoothness (improved with double buffering, may need more tuning)
- ⚠️ NTP sync after WiFi connect via settings (fixed, needs hardware retest)

**Not Yet Tested**:
- ❓ OTA with new HTTPS changes
- ❓ Weather API (network-dependent)
- ❓ Long-term stability

**Known Issues**:
- None currently reported

## Active Proposals

Recent proposals in BACKLOG:
1. `advanced-ui-customization.md` - Text colors, fonts, layout options
2. `background-image-selector.md` - Image gallery with thumbnails, opacity control
3. `debug-log-viewer.md` - On-screen log viewer with export to SD
4. `sd-card-integration.md` - SD card support for storage expansion
5. `secure-ota-https.md` - HTTPS OTA with certificate verification

## Questions?

If unclear about project structure, build process, or specific components:
1. Read relevant source files
2. Check existing proposals
3. Ask user for clarification
4. Don't make assumptions - verify first

---

**Remember**: Code changes yes, builds no (unless asked). User controls the build process.
