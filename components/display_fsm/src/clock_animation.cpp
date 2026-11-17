/**
 * @file clock_animation.cpp
 * @version 1
 * @brief ClockAnimation base (lifecycle, locking, logging) + hummingbird / snowfall.
 */
#include "clock_animation.h"
#include "display_fsm.h"
#include "snowfall_view.h"
#include "leaf_view.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include <string.h>

static const char *TAG = "clock_anim";

/**
 * Base class: owns the created/visible state machine, the LVGL lock discipline
 * and all logging. Subclasses implement only the three widget-level hooks.
 */
class ClockAnimation {
public:
    /**
     * @brief Construct with a display name used in logs and menus.
     * @version 1
     * @utility
     * @return n/a (constructor)
     */
    explicit ClockAnimation(const char *name) : name_(name) {}
    virtual ~ClockAnimation() = default;

    /**
     * @brief Create on first use, then show. Idempotent.
     * @version 1
     * @req REQ-ANIM-001
     */
    void show(lv_obj_t *scr)
    {
        if (!created_ && !create(scr)) return;
        if (visible_) return;
        lvgl_port_lock(0);
        on_show();
        lvgl_port_unlock();
        visible_ = true;
        ESP_LOGI(TAG, "%s: shown", name_);
    }

    /**
     * @brief Hide and pause if currently visible. Idempotent.
     * @version 1
     * @req REQ-ANIM-001
     */
    void hide()
    {
        if (!created_ || !visible_) return;
        lvgl_port_lock(0);
        on_hide();
        lvgl_port_unlock();
        visible_ = false;
        ESP_LOGI(TAG, "%s: hidden", name_);
    }

    /**
     * @brief Animation display name.
     * @version 1
     * @utility
     * @return Static name string.
     */
    const char *name() const { return name_; }

protected:
    /**
     * @brief Off-lock preparation (allocation, I/O). Default: nothing to prepare.
     * @version 1
     * @utility
     * @return true to continue with on_create().
     */
    virtual bool on_prepare() { return true; }
    /** Create widgets; called with the LVGL lock held. Return false on failure. */
    virtual bool on_create(lv_obj_t *scr) = 0;
    /**
     * @brief Post-create work (e.g. queue a file load). Default: nothing.
     * @version 1
     * @utility
     */
    virtual void on_created() {}
    /**
     * @brief Undo on_prepare() after a failed on_create(). Default: nothing.
     * @version 1
     * @utility
     */
    virtual void on_abort() {}
    virtual void on_show() = 0;  /**< LVGL lock held. */
    virtual void on_hide() = 0;  /**< LVGL lock held. */

private:
    /**
     * @brief Run the prepare → locked create → created sequence with heap logging.
     * @version 1
     * @dg_internal
     * @return true when the animation was created.
     */
    bool create(lv_obj_t *scr)
    {
        size_t spiram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        bool ok = scr && on_prepare();
        if (ok) {
            lvgl_port_lock(0);
            ok = on_create(scr);
            lvgl_port_unlock();
            if (!ok) on_abort();
        }
        if (ok) on_created();
        created_ = ok;
        ESP_LOGI(TAG, "%s: create %s (SPIRAM %u -> %u, internal %u -> %u)", name_,
                 ok ? "OK" : "FAILED", (unsigned)spiram_before,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)internal_before,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        return ok;
    }

    const char *name_;
    bool created_ = false;
    bool visible_ = false;
};

// ---------------------------------------------------------------------------
// Hummingbird (Lottie)
// ---------------------------------------------------------------------------

#if LV_USE_LOTTIE
class HummingbirdAnimation : public ClockAnimation {
public:
    /**
     * @brief Construct the Lottie hummingbird (widget created lazily on first show).
     * @version 1
     * @utility
     * @return n/a (constructor)
     */
    HummingbirdAnimation() : ClockAnimation("Hummingbird") {}

protected:
    static constexpr int32_t kW = 200;
    static constexpr int32_t kH = 200;
    static constexpr int32_t kFps = 20;
    static constexpr const char *kPath = "/sdcard/lottie/hummingbird.json";

    /**
     * @brief Allocate and zero the SPIRAM render buffer (uncleared SPIRAM shows as noise).
     * @version 1
     * @dg_internal
     * @return true on success.
     */
    bool on_prepare() override
    {
        size_t sz = (size_t)lv_draw_buf_width_to_stride(kW, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED) * kH;
        buf_ = heap_caps_aligned_alloc(64, sz, MALLOC_CAP_SPIRAM);
        if (!buf_) {
            ESP_LOGE(TAG, "Hummingbird: SPIRAM alloc failed (%u B)", (unsigned)sz);
            return false;
        }
        memset(buf_, 0, sz);
        return true;
    }

    /**
     * @brief Create the lottie widget bound to the render buffer.
     * @version 1
     * @dg_internal
     * @return true on success.
     */
    bool on_create(lv_obj_t *scr) override
    {
        widget_ = lv_lottie_create(scr);
        bool ok = widget_ && !lv_lottie_render_failed(widget_);
        if (!ok) {
            ESP_LOGE(TAG, "Hummingbird: lv_lottie_create %s", widget_ ? "render task failed" : "failed");
            if (widget_) lv_obj_delete(widget_);
            widget_ = nullptr;
            return false;
        }
        lv_obj_set_size(widget_, kW, kH);
        lv_lottie_set_buffer(widget_, kW, kH, buf_);
        lv_obj_align(widget_, LV_ALIGN_BOTTOM_LEFT, 16, -16);
        lv_obj_add_flag(widget_, LV_OBJ_FLAG_HIDDEN);
        return true;
    }

    /**
     * @brief Free the render buffer after a failed create.
     * @version 1
     * @dg_internal
     */
    void on_abort() override
    {
        heap_caps_free(buf_);
        buf_ = nullptr;
    }

    /**
     * @brief Queue the JSON parse on the shared loader task (needs a 64 KB stack).
     * @version 1
     * @dg_internal
     */
    void on_created() override
    {
        lottie_load_job_t job = {};
        job.widget = widget_;
        job.target_fps = kFps;
        strlcpy(job.path, kPath, sizeof(job.path));
        display_fsm_load_lottie(&job);
    }

    /**
     * @brief Unhide and resume at the target frame rate.
     * @version 1
     * @dg_internal
     */
    void on_show() override
    {
        lv_obj_clear_flag(widget_, LV_OBJ_FLAG_HIDDEN);
        lv_lottie_resume(widget_, kFps);
    }

    /**
     * @brief Pause rendering and hide.
     * @version 1
     * @dg_internal
     */
    void on_hide() override
    {
        lv_lottie_pause(widget_);
        lv_obj_add_flag(widget_, LV_OBJ_FLAG_HIDDEN);
    }

private:
    void *buf_ = nullptr;
    lv_obj_t *widget_ = nullptr;
};
#endif /* LV_USE_LOTTIE */

// ---------------------------------------------------------------------------
// Physics particle effects (snowfall, falling leaves) — see CITATIONS.md
// ---------------------------------------------------------------------------

/** The three view functions every particle effect exposes. */
struct ParticleEffectOps {
    void *(*create)(lv_obj_t *parent, int count);
    void (*set_running)(void *view, bool running);
};

/** One ClockAnimation for every particle effect; effects differ only in their view ops. */
class ParticleAnimation : public ClockAnimation {
public:
    /**
     * @brief Bind a named particle effect with its view functions and particle count.
     * @version 1
     * @utility
     * @return n/a (constructor)
     */
    ParticleAnimation(const char *name, ParticleEffectOps ops, int count)
        : ClockAnimation(name), ops_(ops), count_(count) {}

protected:
    /**
     * @brief Create the effect's layer (starts hidden).
     * @version 1
     * @dg_internal
     * @return true on success.
     */
    bool on_create(lv_obj_t *scr) override
    {
        view_ = ops_.create(scr, count_);
        return view_ != nullptr;
    }

    /**
     * @brief Start the physics timer and unhide.
     * @version 1
     * @dg_internal
     */
    void on_show() override { ops_.set_running(view_, true); }

    /**
     * @brief Freeze the physics timer and hide.
     * @version 1
     * @dg_internal
     */
    void on_hide() override { ops_.set_running(view_, false); }

private:
    ParticleEffectOps ops_;
    int count_;
    void *view_ = nullptr;
};

/**
 * @brief Type-erased snowfall_view_create.
 * @version 1
 * @dg_internal
 * @return The view.
 */
static void *snow_create(lv_obj_t *p, int n) { return snowfall_view_create(p, n); }

/**
 * @brief Type-erased snowfall_view_set_running.
 * @version 1
 * @dg_internal
 */
static void snow_run(void *v, bool r) { snowfall_view_set_running((snowfall_view_t *)v, r); }

/**
 * @brief Type-erased leaf_view_create.
 * @version 1
 * @dg_internal
 * @return The view.
 */
static void *leaf_create(lv_obj_t *p, int n) { return leaf_view_create(p, n); }

/**
 * @brief Type-erased leaf_view_set_running.
 * @version 1
 * @dg_internal
 */
static void leaf_run(void *v, bool r) { leaf_view_set_running((leaf_view_t *)v, r); }

// ---------------------------------------------------------------------------
// Registry + C API
// ---------------------------------------------------------------------------

#if LV_USE_LOTTIE
static HummingbirdAnimation s_hummingbird;
#endif
/** Flake / leaf counts: dense enough to read, sparse enough to stay gentle. */
static ParticleAnimation s_snowfall("Snowfall", {snow_create, snow_run}, 140);
static ParticleAnimation s_leaves("Falling Leaves", {leaf_create, leaf_run}, 18);
static ClockAnimation *s_active = nullptr;
static lv_obj_t *s_scr = nullptr;

/**
 * @brief Map a preference value to its animation instance.
 * @version 2
 * @dg_internal
 * @return Instance, or nullptr for CLOCK_ANIM_NONE / unavailable.
 */
static ClockAnimation *instance_for(clock_anim_t anim)
{
    ClockAnimation *inst = nullptr;
#if LV_USE_LOTTIE
    if (anim == CLOCK_ANIM_HUMMINGBIRD) inst = &s_hummingbird;
#endif
    if (anim == CLOCK_ANIM_SNOWFALL) inst = &s_snowfall;
    if (anim == CLOCK_ANIM_LEAVES) inst = &s_leaves;
    return inst;
}

/**
 * @brief Bind to the screen and show the stored choice.
 * @version 1
 * @req REQ-ANIM-001
 */
extern "C" void clock_animation_init(lv_obj_t *scr)
{
    s_scr = scr;
    clock_animation_select(clock_anim_pref_get());
    clock_animation_show();
}

/**
 * @brief Switch the active animation, hiding the previous one.
 * @version 2
 * @req REQ-ANIM-001
 */
extern "C" void clock_animation_select(clock_anim_t anim)
{
    // Reachable from the HTTP API during boot_await_launch, BEFORE LVGL exists: taking the
    // LVGL lock then asserts (seen: PANIC in await_launch). Until init binds the screen there
    // is nothing to hide, and init re-reads the stored preference, so just log and return.
    if (!s_scr) {
        ESP_LOGI(TAG, "select %s before display init — applied at init", clock_anim_name(anim));
        return;
    }
    lvgl_port_lock(0);  // also called from httpd; serialises s_active with the FSM task
    ClockAnimation *next = instance_for(anim);
    if (s_active && s_active != next) s_active->hide();
    s_active = next;
    lvgl_port_unlock();
    ESP_LOGI(TAG, "selected: %s", clock_anim_name(anim));
}

/**
 * @brief Show the active animation (no-op for None or before init).
 * @version 2
 * @req REQ-ANIM-001
 */
extern "C" void clock_animation_show(void)
{
    if (!s_scr) return;  // display not initialised (see clock_animation_select)
    lvgl_port_lock(0);
    if (s_active) s_active->show(s_scr);
    lvgl_port_unlock();
}

/**
 * @brief Hide the active animation (no-op for None or before init).
 * @version 2
 * @req REQ-ANIM-001
 */
extern "C" void clock_animation_hide(void)
{
    if (!s_scr) return;  // display not initialised (see clock_animation_select)
    lvgl_port_lock(0);
    if (s_active) s_active->hide();
    lvgl_port_unlock();
}
