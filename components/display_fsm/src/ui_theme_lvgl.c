/**
 * @file ui_theme_lvgl.c
 * @version 1
 * @brief The OKLCH theme as an LVGL theme: stock widgets (containers, buttons, lists,
 *        sliders, switches, rollers, text areas, keyboards, message boxes, scrollbars) draw
 *        with the theme's roles instead of LVGL's built-in palette.
 *
 * Layered on LVGL's default theme (kept for its geometry, padding and transitions; given the
 * accent as its primary colour), with a child theme whose styles override every colour the
 * default sets on those widgets. ui_theme_lvgl_refresh() recolours the styles in place after
 * a theme change and asks every object to restyle. Caller holds the LVGL lock throughout.
 */

#include "ui_theme_lvgl.h"
#include "ui_theme.h"
#include "esp_log.h"

static const char *TAG = "ui_theme_lvgl";

static lv_theme_t   *s_theme = NULL;
static lv_display_t *s_disp  = NULL;

static lv_style_t s_surface;      // containers, lists, rollers, keyboards, message boxes
static lv_style_t s_button;       // buttons and keyboard keys
static lv_style_t s_active;       // pressed / checked / selected
static lv_style_t s_list_item;    // list buttons
static lv_style_t s_list_header;  // list section text
static lv_style_t s_track;        // bar / slider / switch background
static lv_style_t s_indicator;    // bar / slider fill, switch when on
static lv_style_t s_knob;         // slider and switch knob
static lv_style_t s_input;        // text area
static lv_style_t s_cursor;       // text area cursor
static lv_style_t s_placeholder;  // text area placeholder
static lv_style_t s_backdrop;     // message-box modal backdrop
static lv_style_t s_scrollbar;    // every scrollbar

/** One theme rule: objects of @c cls (exactly, or any subclass) get @c style at @c sel. */
typedef struct {
    const lv_obj_class_t *cls;
    bool                  exact;
    lv_style_t           *style;
    lv_style_selector_t   sel;
} theme_rule_t;

// Later rules take precedence over earlier ones on the same object.
static const theme_rule_t RULES[] = {
    { &lv_obj_class,          true,  &s_surface,     LV_PART_MAIN },
    { &lv_obj_class,          false, &s_scrollbar,   LV_PART_SCROLLBAR },
    { &lv_button_class,       false, &s_button,      LV_PART_MAIN },
    { &lv_button_class,       false, &s_active,      LV_PART_MAIN | LV_STATE_PRESSED },
    { &lv_button_class,       false, &s_active,      LV_PART_MAIN | LV_STATE_CHECKED },
    { &lv_list_class,         true,  &s_surface,     LV_PART_MAIN },
    { &lv_list_button_class,  true,  &s_list_item,   LV_PART_MAIN },
    { &lv_list_button_class,  true,  &s_active,      LV_PART_MAIN | LV_STATE_PRESSED },
    { &lv_list_text_class,    true,  &s_list_header, LV_PART_MAIN },
    { &lv_bar_class,          true,  &s_track,       LV_PART_MAIN },
    { &lv_bar_class,          true,  &s_indicator,   LV_PART_INDICATOR },
    { &lv_slider_class,       true,  &s_track,       LV_PART_MAIN },
    { &lv_slider_class,       true,  &s_indicator,   LV_PART_INDICATOR },
    { &lv_slider_class,       true,  &s_knob,        LV_PART_KNOB },
    { &lv_switch_class,       true,  &s_track,       LV_PART_MAIN },
    { &lv_switch_class,       true,  &s_indicator,   LV_PART_INDICATOR | LV_STATE_CHECKED },
    { &lv_switch_class,       true,  &s_knob,        LV_PART_KNOB },
    { &lv_roller_class,       true,  &s_surface,     LV_PART_MAIN },
    { &lv_roller_class,       true,  &s_active,      LV_PART_SELECTED },
    { &lv_textarea_class,     true,  &s_input,       LV_PART_MAIN },
    { &lv_textarea_class,     true,  &s_cursor,      LV_PART_CURSOR },
    { &lv_textarea_class,     true,  &s_placeholder, LV_PART_TEXTAREA_PLACEHOLDER },
    { &lv_keyboard_class,     true,  &s_surface,     LV_PART_MAIN },
    { &lv_keyboard_class,     true,  &s_button,      LV_PART_ITEMS },
    { &lv_keyboard_class,     true,  &s_active,      LV_PART_ITEMS | LV_STATE_PRESSED },
    { &lv_keyboard_class,     true,  &s_active,      LV_PART_ITEMS | LV_STATE_CHECKED },
    { &lv_msgbox_class,       true,  &s_surface,     LV_PART_MAIN },
    { &lv_msgbox_backdrop_class, true, &s_backdrop,  LV_PART_MAIN },
};
#define RULE_COUNT (sizeof(RULES) / sizeof(RULES[0]))

/**
 * @brief Child-theme apply callback: add every matching rule's style to @p obj.
 * @version 1
 * @callback
 */
static void apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    (void)th;
    for (size_t i = 0; i < RULE_COUNT; i++) {
        const theme_rule_t *r = &RULES[i];
        bool match = r->exact ? lv_obj_check_type(obj, r->cls) : lv_obj_has_class(obj, r->cls);
        if (match) lv_obj_add_style(obj, r->style, r->sel);
    }
}

/**
 * @brief Set @p st to a filled style: @p fill at full opacity with text legible on it.
 * @version 1
 * @utility
 */
static void set_fill(lv_style_t *st, ui_role_t fill)
{
    lv_color_t c = ui_theme_color(fill);
    lv_style_set_bg_color(st, c);
    lv_style_set_bg_opa(st, LV_OPA_COVER);
    lv_style_set_text_color(st, ui_theme_text_on(UI_ROLE_TEXT, c));
}

/**
 * @brief Write the current theme's colours into every style.
 * @version 1
 * @utility
 */
static void recolor_styles(void)
{
    lv_color_t border = ui_theme_color(UI_ROLE_BORDER);
    lv_color_t shadow = ui_theme_tone(0.08f);

    set_fill(&s_surface, UI_ROLE_SURFACE_RAISED);
    lv_style_set_border_color(&s_surface, border);
    set_fill(&s_button, UI_ROLE_SURFACE_INSET);
    lv_style_set_border_color(&s_button, border);
    lv_style_set_shadow_color(&s_button, shadow);
    set_fill(&s_active, UI_ROLE_ACCENT);
    set_fill(&s_list_item, UI_ROLE_SURFACE_RAISED);
    lv_style_set_border_color(&s_list_item, border);
    set_fill(&s_list_header, UI_ROLE_SURFACE_INSET);
    lv_style_set_text_color(&s_list_header,
                            ui_theme_text_on(UI_ROLE_TEXT_MUTED,
                                             ui_theme_color(UI_ROLE_SURFACE_INSET)));
    set_fill(&s_track, UI_ROLE_SURFACE_INSET);
    set_fill(&s_indicator, UI_ROLE_ACCENT);
    set_fill(&s_knob, UI_ROLE_ACCENT);
    lv_style_set_shadow_color(&s_knob, shadow);
    set_fill(&s_input, UI_ROLE_SURFACE_INSET);
    lv_style_set_border_color(&s_input, border);
    lv_style_set_border_color(&s_cursor, ui_theme_color(UI_ROLE_ACCENT));
    lv_style_set_text_color(&s_placeholder,
                            ui_theme_text_on(UI_ROLE_TEXT_FAINT,
                                             ui_theme_color(UI_ROLE_SURFACE_INSET)));
    lv_style_set_bg_color(&s_backdrop, ui_theme_color(UI_ROLE_SCREEN));
    lv_style_set_bg_opa(&s_backdrop, LV_OPA_50);
    lv_style_set_bg_color(&s_scrollbar, border);
}

/**
 * @brief (Re)initialise LVGL's default theme, the parent, from the current theme's accent
 *        and polarity (it is a singleton: re-init updates it in place).
 * @version 1
 * @utility
 * @return The default theme.
 */
static lv_theme_t *init_base(void)
{
    return lv_theme_default_init(s_disp, ui_theme_color(UI_ROLE_ACCENT),
                                 ui_theme_color(UI_ROLE_ACCENT_ALT), ui_theme_is_dark(),
                                 LV_FONT_DEFAULT);
}

/**
 * @brief Install the themed LVGL theme on @p disp (once; later calls only recolour).
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_lvgl_init(lv_display_t *disp)
{
    if (s_theme) {
        ui_theme_lvgl_refresh();
        return;
    }
    s_disp = disp;
    lv_style_t *all[] = { &s_surface, &s_button, &s_active, &s_list_item, &s_list_header,
                          &s_track, &s_indicator, &s_knob, &s_input, &s_cursor,
                          &s_placeholder, &s_backdrop, &s_scrollbar };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) lv_style_init(all[i]);
    recolor_styles();

    lv_theme_t *base = init_base();
    s_theme = lv_theme_create();
    lv_theme_copy(s_theme, base);
    lv_theme_set_parent(s_theme, base);
    lv_theme_set_apply_cb(s_theme, apply_cb);
    lv_display_set_theme(disp, s_theme);
    ESP_LOGI(TAG, "LVGL theme installed (%s)", ui_theme_is_dark() ? "dark" : "light");
}

/**
 * @brief Recolour the themed styles (and the default theme under them) from the current
 *        theme, and restyle every object.
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_lvgl_refresh(void)
{
    if (!s_theme) return;
    recolor_styles();
    init_base();
    lv_obj_report_style_change(NULL);
    ESP_LOGI(TAG, "LVGL theme recoloured (%s)", ui_theme_is_dark() ? "dark" : "light");
}
