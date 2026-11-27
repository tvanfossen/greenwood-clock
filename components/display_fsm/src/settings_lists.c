/**
 * @file settings_lists.c
 * @version 1
 * @brief The device's choices (see settings_lists.h).
 */
#include "settings_lists.h"
#include "theme_store.h"
#include "esp_attr.h"
#include "clock_anim_pref.h"
#include "sdcard.h"

#include "esp_log.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "settings_lists";

#define BG_DIR "/sdcard/backgrounds"

/**
 * @brief Where @p name's picture extension (.png or .gif) starts.
 * @version 1
 * @utility
 * @return The extension, or NULL when it is not a background picture.
 */
static const char *picture_ext(const char *name)
{
    const char *ext = strrchr(name, '.');
    bool ok = ext && (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".gif") == 0);
    return ok ? ext : NULL;
}

/**
 * @brief The pictures in /sdcard/backgrounds ("A:/backgrounds/x.png", "x").
 * @version 1
 * @utility
 * @return How many.
 */
static int backgrounds(settings_choice_t *out, int max)
{
    DIR *dir = sdcard_is_mounted() ? opendir(BG_DIR) : NULL;
    int n = 0;
    struct dirent *e;
    while (dir && (e = readdir(dir)) != NULL && n < max) {
        const char *ext = e->d_type == DT_DIR ? NULL : picture_ext(e->d_name);
        if (!ext) continue;
        snprintf(out[n].value, sizeof(out[n].value), "A:/backgrounds/%s", e->d_name);
        snprintf(out[n].label, sizeof(out[n].label), "%.*s", (int)(ext - e->d_name), e->d_name);
        n++;
    }
    if (dir) closedir(dir);
    else ESP_LOGW(TAG, "%s: not readable", BG_DIR);
    return n;
}

/**
 * @brief The theme files (stem, display name).
 * v2: the scan in PSRAM (24 themes).
 * @version 2
 * @utility
 * @return How many.
 */
static int themes(settings_choice_t *out, int max)
{
    EXT_RAM_BSS_ATTR static theme_entry_t t[THEME_STORE_MAX];   // PSRAM: off the LVGL task's stack
    int n = theme_store_scan(t);
    n = n < max ? n : max;
    for (int i = 0; i < n; i++) {
        snprintf(out[i].value, sizeof(out[i].value), "%s", t[i].stem);
        snprintf(out[i].label, sizeof(out[i].label), "%s", t[i].name);
    }
    return n;
}

/**
 * @brief Fill a list.
 * @version 1
 * @req REQ-UI-001
 * @return How many.
 */
int settings_lists_fill(settings_list_t which, settings_choice_t *out, int max)
{
    int n = 0;
    if (which == SETTINGS_LIST_BACKGROUNDS) {
        n = backgrounds(out, max);
    } else if (which == SETTINGS_LIST_THEMES) {
        n = themes(out, max);
    } else {
        for (; n < CLOCK_ANIM_COUNT && n < max; n++) {
            snprintf(out[n].value, sizeof(out[n].value), "%s", clock_anim_name(CLOCK_ANIM_MENU_ORDER[n]));
            snprintf(out[n].label, sizeof(out[n].label), "%s", out[n].value);
        }
    }
    return n;
}
