/**
 * @file settings_backend.h
 * @version 1
 * @brief What the settings screens (settings_app) need from the device they run for: read and
 *        change the settings, the lists to choose from, the schedule, the sun, and the few
 *        device-only screens. Two implementations: the clock's own (settings_backend_esp.c:
 *        NVS, SD, the display) and the phone panel's (state fetched once, changes POSTed to
 *        the clock). Calls are made under the LVGL lock; an implementation that blocks
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

/** The global settings the screens show and change. */
typedef struct {
    int  brightness;                   /**< 10..100 %. */
    char background[SCHED_IMAGE_LEN];  /**< LVGL path. */
    char theme[SCHED_THEME_LEN];       /**< Theme file stem. */
    char anim[SCHED_ANIM_LEN];         /**< Clock animation name. */
} settings_values_t;

/** The answer to a change that takes a while: whether it worked, and why not. */
typedef void (*settings_done_cb_t)(bool ok, const char *why, void *ctx);

/** A device's settings. Optional members may be NULL: their menu entry is left out. */
typedef struct {
    void (*get)(settings_values_t *out);
    int  (*list)(settings_list_t which, settings_choice_t *out, int max);
    void (*set_brightness)(int pct);
    void (*set_background)(const char *path);
    void (*set_theme)(const char *stem);
    void (*set_anim)(const char *name);
    int  (*schedule_get)(sched_rule_t *out, int max);
    void (*schedule_set)(const sched_rule_t *r, int n, settings_done_cb_t done, void *ctx);
    const sched_sun_src_t *(*sun)(void);
    void (*now)(sched_date_t *day, int *min);   /**< Local date and minute. */
    void (*save_theme)(const ui_theme_t *t, settings_done_cb_t done, void *ctx);
    void (*about)(char *out, size_t n);
    lv_obj_t *(*wifi_create)(lv_obj_t *parent);      /**< Optional: the Wi-Fi screen. */
    lv_obj_t *(*phone_create)(lv_obj_t *parent);     /**< Optional: pair a phone (QR). */
    lv_obj_t *(*preview_create)(lv_obj_t *parent);   /**< Optional: animation preview. */
    void (*reboot)(void);                            /**< Optional. */
    void (*leave)(void);                             /**< Back from the menu. */
} settings_backend_t;

#ifdef __cplusplus
}
#endif
