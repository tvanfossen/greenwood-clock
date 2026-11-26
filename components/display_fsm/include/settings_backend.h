/**
 * @file settings_backend.h
 * v2: optional upload (the phone sends a picture).
 * v3: leave is optional (the phone has no Back at its menu).
 * v4: no global settings (profiles set everything): get / set_* are gone; save_theme keeps
 *     a named theme.
 * v5: no animation preview entry.
 * v6: the clock's place and timezone (optional: Settings > Location).
 * @version 6
 * @brief What the settings screens (settings_app) need from the device they run for: read and
 *        change the settings, the lists to choose from, the schedule, the sun, and the few
 *        device-only screens. Two implementations: the clock's own (state_settings.cpp:
 *        NVS, SD, the display) and the phone panel's (tools/panel/settings_http.c: state
 *        fetched once, changes POSTed to the clock). Calls are made under the LVGL lock; an implementation that blocks
 *        (SD, NVS, network) releases it itself, and answers slow work through a callback.
 */
#pragma once

#include "lvgl.h"
#include "sched_engine.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_ITEM_LEN 96

/** One thing to choose: what is stored and what is shown. */
typedef struct {
    char value[SETTINGS_ITEM_LEN];   /**< "A:/backgrounds/x.png", "default", "Snowfall". */
    char label[SETTINGS_ITEM_LEN];   /**< "x", "Default", "Snowfall". */
} settings_choice_t;

/** The lists the screens choose from. */
typedef enum {
    SETTINGS_LIST_BACKGROUNDS = 0,
    SETTINGS_LIST_THEMES,
    SETTINGS_LIST_ANIMS,
} settings_list_t;

/** What a picture is uploaded as. */
typedef enum {
    SETTINGS_UPLOAD_BACKGROUND = 0,   /**< Cropped to fill 1024x600; joins the backgrounds. */
    SETTINGS_UPLOAD_PHOTO,            /**< Fitted into 1024x600; joins the photo album. */
} settings_upload_t;

/** The answer to a change that takes a while: whether it worked, and why not. */
typedef void (*settings_done_cb_t)(bool ok, const char *why, void *ctx);

#define SETTINGS_PLACE_LEN 48

/** Where the clock is: a short label, its coordinates, its IANA zone. */
typedef struct {
    char  label[SETTINGS_PLACE_LEN];   /**< "Chicago, IL"; "" when never set by address. */
    float lat, lon;
    char  tz[SETTINGS_PLACE_LEN];      /**< "America/Chicago"; "" when never chosen. */
} settings_location_t;

/** An address looked up: whether it matched (and why not), and the place found. */
typedef void (*settings_lookup_cb_t)(bool ok, const char *why, const settings_location_t *found,
                                     void *ctx);

/** A device's settings. Optional members may be NULL: their menu entry is left out. */
typedef struct {
    int  (*list)(settings_list_t which, settings_choice_t *out, int max);
    int  (*schedule_get)(sched_rule_t *out, int max);
    void (*schedule_set)(const sched_rule_t *r, int n, settings_done_cb_t done, void *ctx);
    const sched_sun_src_t *(*sun)(void);
    void (*now)(sched_date_t *day, int *min);   /**< Local date and minute. */
    /** Keep @p t (named t->name) as the theme file @p stem, replacing one of that stem. */
    void (*save_theme)(const ui_theme_t *t, const char *stem, settings_done_cb_t done, void *ctx);
    void (*about)(char *out, size_t n);
    lv_obj_t *(*wifi_create)(lv_obj_t *parent);      /**< Optional: the Wi-Fi screen. */
    lv_obj_t *(*phone_create)(lv_obj_t *parent);     /**< Optional: pair a phone (QR). */
    void (*upload)(settings_upload_t kind, settings_done_cb_t done, void *ctx);   /**< Optional: pick and send a picture. */
    void (*location_get)(settings_location_t *out);   /**< Optional: Settings > Location. */
    /** Look @p address up (once; the address is not kept); @p done hears the place found. */
    void (*location_lookup)(const char *address, settings_lookup_cb_t done, void *ctx);
    /** Keep @p loc (coordinates, label, zone) and apply it. */
    void (*location_set)(const settings_location_t *loc, settings_done_cb_t done, void *ctx);
    void (*reboot)(void);                            /**< Optional. */
    void (*leave)(void);                             /**< Optional: Back from the menu (none: no Back). */
} settings_backend_t;

#ifdef __cplusplus
}
#endif
