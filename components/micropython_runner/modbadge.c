/*
 * MicroPython native 'badge' module for D26 Badge
 *
 * Provides: badge.display, badge.leds, badge.buttons, badge.exit(), badge.delay_ms()
 * All display/LED commands go through mp_bridge queues to CPU0.
 * Button state is read from the button bridge queue.
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "mp_bridge.h"
#include <string.h>

/* ───────────────────── badge.display ───────────────────── */

/* badge.display.clear(color=0) */
static mp_obj_t badge_display_clear(size_t n_args, const mp_obj_t *args) {
    uint16_t color = (n_args > 0) ? mp_obj_get_int(args[0]) : 0;
    mp_display_cmd_t cmd = { .type = MP_DISP_CMD_CLEAR };
    cmd.params.clear.color = color;
    mp_bridge_send_display_cmd(&cmd, 100);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(badge_display_clear_obj, 0, 1, badge_display_clear);

/* badge.display.text(x, y, text, color=0xFFFF) */
static mp_obj_t badge_display_text(size_t n_args, const mp_obj_t *args) {
    int16_t x = mp_obj_get_int(args[0]);
    int16_t y = mp_obj_get_int(args[1]);
    const char *text = mp_obj_str_get_str(args[2]);
    uint16_t color = (n_args > 3) ? mp_obj_get_int(args[3]) : 0xFFFF;

    mp_display_cmd_t cmd = { .type = MP_DISP_CMD_TEXT };
    cmd.params.text.x = x;
    cmd.params.text.y = y;
    cmd.params.text.color = color;
    strncpy(cmd.params.text.text, text, sizeof(cmd.params.text.text) - 1);
    cmd.params.text.text[sizeof(cmd.params.text.text) - 1] = '\0';
    mp_bridge_send_display_cmd(&cmd, 100);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(badge_display_text_obj, 3, 4, badge_display_text);

/* badge.display.rect(x, y, w, h, color, fill=True) */
static mp_obj_t badge_display_rect(size_t n_args, const mp_obj_t *args) {
    mp_display_cmd_t cmd = { .type = MP_DISP_CMD_RECT };
    cmd.params.rect.x = mp_obj_get_int(args[0]);
    cmd.params.rect.y = mp_obj_get_int(args[1]);
    cmd.params.rect.w = mp_obj_get_int(args[2]);
    cmd.params.rect.h = mp_obj_get_int(args[3]);
    cmd.params.rect.color = mp_obj_get_int(args[4]);
    cmd.params.rect.fill = (n_args > 5) ? mp_obj_is_true(args[5]) : true;
    mp_bridge_send_display_cmd(&cmd, 100);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(badge_display_rect_obj, 5, 6, badge_display_rect);

/* badge.display.pixel(x, y, color) */
static mp_obj_t badge_display_pixel(mp_obj_t x_obj, mp_obj_t y_obj, mp_obj_t color_obj) {
    mp_display_cmd_t cmd = { .type = MP_DISP_CMD_PIXEL };
    cmd.params.pixel.x = mp_obj_get_int(x_obj);
    cmd.params.pixel.y = mp_obj_get_int(y_obj);
    cmd.params.pixel.color = mp_obj_get_int(color_obj);
    mp_bridge_send_display_cmd(&cmd, 100);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(badge_display_pixel_obj, badge_display_pixel);

/* badge.display.show() */
static mp_obj_t badge_display_show(void) {
    mp_display_cmd_t cmd = { .type = MP_DISP_CMD_SHOW };
    mp_bridge_send_display_cmd(&cmd, 100);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(badge_display_show_obj, badge_display_show);

/* display sub-module dict */
static const mp_rom_map_elem_t badge_display_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&badge_display_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_text),  MP_ROM_PTR(&badge_display_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect),  MP_ROM_PTR(&badge_display_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&badge_display_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_show),  MP_ROM_PTR(&badge_display_show_obj) },
};
static MP_DEFINE_CONST_DICT(badge_display_locals_dict, badge_display_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    badge_display_type,
    MP_QSTR_display,
    MP_TYPE_FLAG_NONE,
    locals_dict, &badge_display_locals_dict
);

static const mp_obj_base_t badge_display_obj = { &badge_display_type };

/* ───────────────────── badge.leds ───────────────────── */

/* badge.leds.set(index, r, g, b) */
static mp_obj_t badge_leds_set(size_t n_args, const mp_obj_t *args) {
    mp_led_cmd_t cmd;
    cmd.index = mp_obj_get_int(args[0]);
    cmd.r = mp_obj_get_int(args[1]);
    cmd.g = mp_obj_get_int(args[2]);
    cmd.b = mp_obj_get_int(args[3]);
    mp_bridge_send_led_cmd(&cmd, 100);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(badge_leds_set_obj, 4, 4, badge_leds_set);

/* badge.leds.fill(r, g, b) */
static mp_obj_t badge_leds_fill(mp_obj_t r_obj, mp_obj_t g_obj, mp_obj_t b_obj) {
    mp_led_cmd_t cmd;
    cmd.index = 0xFF; /* 0xFF = fill all */
    cmd.r = mp_obj_get_int(r_obj);
    cmd.g = mp_obj_get_int(g_obj);
    cmd.b = mp_obj_get_int(b_obj);
    mp_bridge_send_led_cmd(&cmd, 100);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(badge_leds_fill_obj, badge_leds_fill);

static const mp_rom_map_elem_t badge_leds_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_set),  MP_ROM_PTR(&badge_leds_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&badge_leds_fill_obj) },
};
static MP_DEFINE_CONST_DICT(badge_leds_locals_dict, badge_leds_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    badge_leds_type,
    MP_QSTR_leds,
    MP_TYPE_FLAG_NONE,
    locals_dict, &badge_leds_locals_dict
);

static const mp_obj_base_t badge_leds_obj = { &badge_leds_type };

/* ───────────────────── badge.buttons ───────────────────── */

/* badge.buttons.get() - returns dict of button states */
static mp_obj_t badge_buttons_get(void) {
    mp_button_event_t ev;
    /* Non-blocking read */
    uint8_t mask = 0;
    while (mp_bridge_recv_button_event(&ev, 0) == ESP_OK) {
        if (ev.pressed) mask |= ev.button_mask;
    }

    mp_obj_t dict = mp_obj_new_dict(8);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_up),     mp_obj_new_bool(mask & 0x01));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_down),   mp_obj_new_bool(mask & 0x02));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_left),   mp_obj_new_bool(mask & 0x04));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_right),  mp_obj_new_bool(mask & 0x08));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_a),      mp_obj_new_bool(mask & 0x10));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_b),      mp_obj_new_bool(mask & 0x20));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_select), mp_obj_new_bool(mask & 0x40));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_start),  mp_obj_new_bool(mask & 0x80));

    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(badge_buttons_get_obj, badge_buttons_get);

/* badge.buttons.wait(timeout_ms=0) - block until button press, 0=forever */
static mp_obj_t badge_buttons_wait(size_t n_args, const mp_obj_t *args) {
    uint32_t timeout = (n_args > 0) ? mp_obj_get_int(args[0]) : 0;
    if (timeout == 0) timeout = portMAX_DELAY;

    mp_button_event_t ev;
    if (mp_bridge_recv_button_event(&ev, timeout) == ESP_OK) {
        return mp_obj_new_int(ev.button_mask);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(badge_buttons_wait_obj, 0, 1, badge_buttons_wait);

static const mp_rom_map_elem_t badge_buttons_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_get),  MP_ROM_PTR(&badge_buttons_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&badge_buttons_wait_obj) },
};
static MP_DEFINE_CONST_DICT(badge_buttons_locals_dict, badge_buttons_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    badge_buttons_type,
    MP_QSTR_buttons,
    MP_TYPE_FLAG_NONE,
    locals_dict, &badge_buttons_locals_dict
);

static const mp_obj_base_t badge_buttons_obj = { &badge_buttons_type };

/* ───────────────────── badge module top-level ───────────────────── */

/* badge.exit() */
static mp_obj_t badge_exit_func(void) {
    /* Signal the runner that the app wants to quit */
    extern volatile bool mp_app_exit_requested;
    mp_app_exit_requested = true;
    /* Raise SystemExit to break out of the Python app */
    mp_raise_type(&mp_type_SystemExit);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(badge_exit_obj, badge_exit_func);

/* badge.delay_ms(ms) */
static mp_obj_t badge_delay_ms(mp_obj_t ms_obj) {
    mp_int_t ms = mp_obj_get_int(ms_obj);
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(badge_delay_ms_obj, badge_delay_ms);

/* Module dict */
static const mp_rom_map_elem_t badge_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),  MP_ROM_QSTR(MP_QSTR_badge) },
    { MP_ROM_QSTR(MP_QSTR_display),   MP_ROM_PTR(&badge_display_obj) },
    { MP_ROM_QSTR(MP_QSTR_leds),      MP_ROM_PTR(&badge_leds_obj) },
    { MP_ROM_QSTR(MP_QSTR_buttons),   MP_ROM_PTR(&badge_buttons_obj) },
    { MP_ROM_QSTR(MP_QSTR_exit),      MP_ROM_PTR(&badge_exit_obj) },
    { MP_ROM_QSTR(MP_QSTR_delay_ms),  MP_ROM_PTR(&badge_delay_ms_obj) },
};
static MP_DEFINE_CONST_DICT(badge_module_globals, badge_module_globals_table);

/* Module definition */
const mp_obj_module_t badge_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&badge_module_globals,
};

/* Register with MicroPython via MICROPY_PORT_BUILTIN_MODULES */
MP_REGISTER_MODULE(MP_QSTR_badge, badge_module);
