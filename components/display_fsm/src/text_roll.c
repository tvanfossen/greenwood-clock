/**
 * @file text_roll.c
 * @version 1
 * @brief Odometer-style text change (see text_roll.h): one animation, 0..2*HALF; the first
 *        half slides and fades the old text out, the swap happens at HALF, the second half
 *        slides and fades the new text in.
 */
#include "text_roll.h"

#define HALF     256
#define ROLL_DY  40      /* px the text travels on each side of rest */

static struct {
    lv_obj_t         *objs[TEXT_ROLL_MAX_OBJS];
    int               n;
    int32_t           dir;       /* +1 forward (out upwards), -1 back */
    bool              swapped;
    bool              running;
    text_roll_swap_cb swap;
    void             *user;
} s_roll;

/**
 * @brief Offset every rolled object by @p dy and set its opacity to @p opa.
 * @version 1
 * @utility
 */
static void roll_place(int32_t dy, lv_opa_t opa)
{
    for (int i = 0; i < s_roll.n; i++) {
        lv_obj_set_style_translate_y(s_roll.objs[i], dy, 0);
        lv_obj_set_style_opa(s_roll.objs[i], opa, 0);
    }
}

/**
 * @brief Put the objects at rest and let the caller set the new text (once per roll).
 * @version 1
 * @utility
 */
static void roll_swap(void)
{
    if (s_roll.swapped) return;
    s_roll.swapped = true;
    roll_place(0, LV_OPA_COVER);   // at rest, so the caller's layout (shadow offsets) is right
    if (s_roll.swap) s_roll.swap(s_roll.user);
}

/**
 * @brief lv_anim exec callback: place the objects for step @p v (0..2*HALF).
 * @version 1
 * @callback
 */
static void roll_exec_cb(void *var, int32_t v)
{
    LV_UNUSED(var);
    if (v < HALF) {
        roll_place(-s_roll.dir * ROLL_DY * v / HALF, (lv_opa_t)(LV_OPA_COVER * (HALF - v) / HALF));
        return;
    }
    roll_swap();
    int32_t u = 2 * HALF - v;   // HALF -> 0: distance still to travel
    roll_place(s_roll.dir * ROLL_DY * u / HALF, (lv_opa_t)(LV_OPA_COVER * (HALF - u) / HALF));
}

/**
 * @brief lv_anim completed callback: objects at rest, roll over.
 * @version 1
 * @callback
 */
static void roll_done_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    roll_swap();
    roll_place(0, LV_OPA_COVER);
    s_roll.running = false;
}

/**
 * @brief Jump a running roll to its end (swapping first if it has not yet). No-op when idle.
 * @version 1
 * @req REQ-RADAR-003
 */
void text_roll_finish(void)
{
    if (!s_roll.running) return;
    lv_anim_delete(&s_roll, roll_exec_cb);   // no completed_cb on delete: finish here
    roll_done_cb(NULL);
}

/**
 * @brief Roll @p objs (up to TEXT_ROLL_MAX_OBJS) over @p ms, starting after @p delay_ms:
 *        forward (@p back false) slides the old text up and the new in from below. A roll
 *        still running is finished first.
 * v2: start delay; travel 28 -> 40 px.
 * @version 2
 * @req REQ-RADAR-003
 */
void text_roll_start(lv_obj_t *const objs[], int n, bool back, uint32_t delay_ms, uint32_t ms,
                     text_roll_swap_cb swap, void *user)
{
    text_roll_finish();
    s_roll.n = LV_MIN(n, TEXT_ROLL_MAX_OBJS);
    for (int i = 0; i < s_roll.n; i++) s_roll.objs[i] = objs[i];
    s_roll.dir = back ? -1 : 1;
    s_roll.swap = swap;
    s_roll.user = user;
    s_roll.swapped = false;
    s_roll.running = true;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_roll);
    lv_anim_set_values(&a, 0, 2 * HALF);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_early_apply(&a, false);   // old text stays at rest through the delay
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, roll_exec_cb);
    lv_anim_set_completed_cb(&a, roll_done_cb);
    lv_anim_start(&a);
}

/**
 * @brief True while a roll runs.
 * @version 1
 * @req REQ-RADAR-003
 * @return Whether a roll is in flight.
 */
bool text_roll_running(void)
{
    return s_roll.running;
}
