// components/display_fsm/src/particle_effects.c
//
// Layer 1: LVGL-native particle effects for weather conditions.
// Uses lv_anim for smooth, hardware-accelerated (PPA) particle motion.
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>

static const char *TAG = "particles";

#define MAX_PARTICLES 50

// ---- Pre-defined configs ----

const particle_config_t PARTICLE_RAIN = {
    .count = 30, .width = 2, .height = 12,
    .color = {.blue = 0xD9, .green = 0x90, .red = 0x4A},  // #4A90D9
    .opacity = LV_OPA_70,
    .fall_time_ms = 1500, .fall_time_variance_ms = 800,
    .delay_variance_ms = 2000,
    .horizontal_drift = false, .flash = false,
};

const particle_config_t PARTICLE_SNOW = {
    .count = 20, .width = 5, .height = 5,
    .color = {.blue = 0xFF, .green = 0xFF, .red = 0xFF},
    .opacity = LV_OPA_80,
    .fall_time_ms = 3500, .fall_time_variance_ms = 1500,
    .delay_variance_ms = 3000,
    .horizontal_drift = true, .flash = false,
};

const particle_config_t PARTICLE_DRIZZLE = {
    .count = 15, .width = 1, .height = 8,
    .color = {.blue = 0xE0, .green = 0xC0, .red = 0x80},  // light blue
    .opacity = LV_OPA_50,
    .fall_time_ms = 2000, .fall_time_variance_ms = 600,
    .delay_variance_ms = 2500,
    .horizontal_drift = false, .flash = false,
};

const particle_config_t PARTICLE_ICE = {
    .count = 12, .width = 3, .height = 3,
    .color = {.blue = 0xFF, .green = 0xFF, .red = 0xB0},  // cyan-ish
    .opacity = LV_OPA_90,
    .fall_time_ms = 1000, .fall_time_variance_ms = 400,
    .delay_variance_ms = 1500,
    .horizontal_drift = false, .flash = false,
};

const particle_config_t PARTICLE_CONFETTI = {
    .count = 40, .width = 6, .height = 6,
    .color = {0},  // overridden per-particle with random colors
    .opacity = LV_OPA_COVER,
    .fall_time_ms = 2000, .fall_time_variance_ms = 1000,
    .delay_variance_ms = 500,
    .horizontal_drift = true, .flash = false,
};

const particle_config_t PARTICLE_SPARKLE = {
    .count = 15, .width = 3, .height = 3,
    .color = {.blue = 0x60, .green = 0xD7, .red = 0xFF},  // gold
    .opacity = LV_OPA_COVER,
    .fall_time_ms = 1200, .fall_time_variance_ms = 400,
    .delay_variance_ms = 2000,
    .horizontal_drift = false, .flash = true,
};

// ---- Internal struct ----

struct particle_system_t {
    lv_obj_t *particles[MAX_PARTICLES];
    int       count;
    bool      paused;
};

static uint32_t rand_range(uint32_t max)
{
    return max > 0 ? (esp_random() % max) : 0;
}

static lv_color_t random_confetti_color(void)
{
    static const uint32_t colors[] = {
        0xFF4444, 0x44FF44, 0x4488FF, 0xFFFF44,
        0xFF44FF, 0x44FFFF, 0xFF8844, 0x88FF44,
    };
    return lv_color_hex(colors[esp_random() % 8]);
}

particle_system_t *particle_system_create(lv_obj_t *parent, const particle_config_t *cfg)
{
    if (!cfg) return NULL;

    int count = cfg->count;
    if (count > MAX_PARTICLES) count = MAX_PARTICLES;

    particle_system_t *ps = lv_malloc(sizeof(particle_system_t));
    if (!ps) {
        ESP_LOGE(TAG, "Failed to allocate particle_system_t");
        return NULL;
    }
    memset(ps, 0, sizeof(*ps));
    ps->count = count;

    int screen_w = 1024;
    int screen_h = 600;

    for (int i = 0; i < count; i++) {
        lv_obj_t *p = lv_obj_create(parent);
        ps->particles[i] = p;

        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, cfg->width, cfg->height);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        // Color
        lv_color_t color = cfg->color;
        if (cfg == &PARTICLE_CONFETTI) {
            color = random_confetti_color();
        }
        lv_obj_set_style_bg_color(p, color, 0);
        lv_obj_set_style_bg_opa(p, cfg->opacity, 0);
        lv_obj_set_style_radius(p, cfg->width > 3 ? 2 : 1, 0);

        // Starting position
        int start_x = rand_range(screen_w);
        int start_y = -(int)rand_range(200);

        lv_obj_set_pos(p, start_x, start_y);

        if (cfg->flash) {
            // Sparkle: opacity pulse in place (no falling)
            int fixed_x = rand_range(screen_w);
            int fixed_y = rand_range(screen_h);
            lv_obj_set_pos(p, fixed_x, fixed_y);

            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, p);
            lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
            lv_anim_set_time(&a, cfg->fall_time_ms + rand_range(cfg->fall_time_variance_ms));
            lv_anim_set_delay(&a, rand_range(cfg->delay_variance_ms));
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_playback_time(&a, cfg->fall_time_ms);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
            lv_anim_start(&a);
        } else {
            // Falling: animate Y from start_y to screen_h + margin
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, p);
            lv_anim_set_values(&a, start_y, screen_h + 20);
            lv_anim_set_time(&a, cfg->fall_time_ms + rand_range(cfg->fall_time_variance_ms));
            lv_anim_set_delay(&a, rand_range(cfg->delay_variance_ms));
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_path_cb(&a, lv_anim_path_linear);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_start(&a);

            // Horizontal drift for snow-like wobble
            if (cfg->horizontal_drift) {
                lv_anim_t h;
                lv_anim_init(&h);
                lv_anim_set_var(&h, p);
                int drift = 20 + rand_range(30);
                lv_anim_set_values(&h, start_x - drift, start_x + drift);
                lv_anim_set_time(&h, 1500 + rand_range(1000));
                lv_anim_set_repeat_count(&h, LV_ANIM_REPEAT_INFINITE);
                lv_anim_set_playback_time(&h, 1500 + rand_range(1000));
                lv_anim_set_path_cb(&h, lv_anim_path_ease_in_out);
                lv_anim_set_exec_cb(&h, (lv_anim_exec_xcb_t)lv_obj_set_x);
                lv_anim_start(&h);
            }
        }
    }

    ESP_LOGI(TAG, "ParticleSystem created: %d particles", count);
    return ps;
}

void particle_system_destroy(particle_system_t *ps)
{
    if (!ps) return;
    for (int i = 0; i < ps->count; i++) {
        if (ps->particles[i]) {
            lv_anim_del(ps->particles[i], NULL);  // stop all anims on this var
            lv_obj_del(ps->particles[i]);
        }
    }
    lv_free(ps);
    ESP_LOGI(TAG, "ParticleSystem destroyed");
}

void particle_system_pause(particle_system_t *ps)
{
    if (!ps || ps->paused) return;
    ps->paused = true;
    for (int i = 0; i < ps->count; i++) {
        if (ps->particles[i]) {
            lv_obj_add_flag(ps->particles[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void particle_system_resume(particle_system_t *ps)
{
    if (!ps || !ps->paused) return;
    ps->paused = false;
    for (int i = 0; i < ps->count; i++) {
        if (ps->particles[i]) {
            lv_obj_clear_flag(ps->particles[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}
