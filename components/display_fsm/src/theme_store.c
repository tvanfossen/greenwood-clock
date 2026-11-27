/**
 * @file theme_store.c
 * v2: theme_store_save (the on-screen editor's themes).
 * @version 2
 * @brief Theme files on the SD card: the cached list of installed themes and loading one.
 */

#include "theme_store.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "theme_store";

#define THEME_FILE_MAX 4096   // a theme file is a few hundred bytes

EXT_RAM_BSS_ATTR static theme_entry_t s_entries[THEME_STORE_MAX];   // PSRAM: internal RAM is short
static int           s_count = 0;

/**
 * @brief Read @p path whole into a NUL-terminated heap buffer, up to THEME_FILE_MAX bytes.
 * @version 1
 * @utility
 * @return The text (free it), or NULL (logged).
 */
static char *read_small_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf = f ? calloc(1, THEME_FILE_MAX + 1) : NULL;
    size_t n = buf ? fread(buf, 1, THEME_FILE_MAX, f) : 0;
    if (f) fclose(f);
    if (buf && (n == 0 || n == THEME_FILE_MAX)) {
        ESP_LOGW(TAG, "%s: %s", path, n ? "larger than 4 KB" : "empty");
        free(buf);
        buf = NULL;
    }
    if (!f) ESP_LOGW(TAG, "%s: cannot open", path);
    return buf;
}

/**
 * @brief Read and parse theme @p stem.
 * @version 1
 * @req REQ-UI-003
 * @return Whether it loaded.
 */
bool theme_store_load(const char *stem, ui_theme_t *out)
{
    bool ok = false;
    if (bg_theme_name_valid(stem)) {
        char path[64];
        snprintf(path, sizeof(path), THEME_STORE_DIR "/%s.json", stem);
        char *json = read_small_file(path);
        ok = json && ui_theme_parse(json, out);
        if (json && !ok) ESP_LOGW(TAG, "%s: not a valid theme file", path);
        free(json);
    } else {
        ESP_LOGW(TAG, "load: \"%s\" is not a theme name", stem ? stem : "(null)");
    }
    return ok;
}

/**
 * @brief The stem of @p file when it is "<valid theme name>.json".
 * @version 1
 * @utility
 * @return Whether it is (@p stem set only then).
 */
static bool theme_stem(const char *file, char stem[BG_THEME_LEN])
{
    size_t n = strlen(file);
    bool ok = n > 5 && n - 5 < BG_THEME_LEN && strcmp(file + n - 5, ".json") == 0;
    if (ok) {
        memcpy(stem, file, n - 5);
        stem[n - 5] = '\0';
        ok = bg_theme_name_valid(stem);
    }
    return ok;
}

/**
 * @brief List the valid theme files of THEME_STORE_DIR into @p out (no cache, no lock).
 * @version 1
 * @req REQ-UI-003
 * @return How many (0 when the folder is missing, logged).
 */
int theme_store_scan(theme_entry_t out[THEME_STORE_MAX])
{
    int n = 0;
    DIR *d = opendir(THEME_STORE_DIR);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL && n < THEME_STORE_MAX) {
        ui_theme_t th;
        if (theme_stem(e->d_name, out[n].stem) && theme_store_load(out[n].stem, &th)) {
            strlcpy(out[n].name, th.name, sizeof(out[n].name));
            n++;
        }
    }
    if (d) closedir(d);
    else ESP_LOGW(TAG, "rescan: %s missing — only the built-in theme", THEME_STORE_DIR);
    return n;
}

/**
 * @brief Re-read the list of valid theme files (off-lock), then swap it into the cache under
 *        the LVGL lock, where the menus read it.
 * v2: its scratch list in PSRAM (24 themes).
 * @version 2
 * @req REQ-UI-003
 */
void theme_store_rescan(void)
{
    EXT_RAM_BSS_ATTR static theme_entry_t fresh[THEME_STORE_MAX];   // static, PSRAM: off the stack and internal RAM
    int n = theme_store_scan(fresh);
    lvgl_port_lock(0);
    memcpy(s_entries, fresh, sizeof(fresh));
    s_count = n;
    lvgl_port_unlock();
    ESP_LOGI(TAG, "rescan: %d theme(s) in %s", n, THEME_STORE_DIR);
}

/**
 * @brief Number of cached themes.
 * @version 1
 * @req REQ-UI-003
 * @return 0..THEME_STORE_MAX.
 */
int theme_store_count(void)
{
    return s_count;
}

/**
 * @brief Cached theme @p i.
 * @version 1
 * @req REQ-UI-003
 * @return The entry, or NULL when out of range.
 */
const theme_entry_t *theme_store_entry(int i)
{
    return (i >= 0 && i < s_count) ? &s_entries[i] : NULL;
}

/**
 * @brief The display name of theme @p stem from the cache.
 * @version 1
 * @req REQ-UI-003
 * @return The name, or @p stem when it is not cached.
 */
const char *theme_store_name(const char *stem)
{
    const char *name = stem;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].stem, stem) == 0) name = s_entries[i].name;
    }
    return name;
}

/**
 * @brief Write @p t to THEME_STORE_DIR/<stem>.json (replacing it). Off-lock (SD I/O).
 * @version 1
 * @req REQ-UI-004
 * @return Whether it was written (failures logged).
 */
bool theme_store_save(const char *stem, const ui_theme_t *t)
{
    char json[1024];
    char path[64];
    bool ok = bg_theme_name_valid(stem) && ui_theme_to_json(t, json, sizeof(json));
    FILE *f = NULL;
    if (ok) {
        snprintf(path, sizeof(path), THEME_STORE_DIR "/%s.json", stem);
        f = fopen(path, "wb");
        ok = f && fwrite(json, 1, strlen(json), f) == strlen(json);
    }
    if (f) ok = (fclose(f) == 0) && ok;
    if (ok) ESP_LOGI(TAG, "saved theme '%s' (%s)", stem, t->name);
    else ESP_LOGE(TAG, "save theme '%s' failed", stem ? stem : "(null)");
    return ok;
}
