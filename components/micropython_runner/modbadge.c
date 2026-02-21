/*
 * Badge Native Module for MicroPython
 * Provides hardware access APIs for Python apps
 */

#include "py/mpconfig.h"
#include "py/runtime.h"
#include "py/obj.h"
#include "mp_bridge.h"
#include "esp_log.h"

static const char *TAG = "modbadge";

// ========== Display Functions ==========

static mp_obj_t badge_display_clear(mp_obj_t color_obj) {
    uint16_t color = mp_obj_get_int(color_obj);
    
    mp_display_cmd_t cmd = {
        .type = MP_DISPLAY_CLEAR,
        .color = color
    };
    
    esp_err_t ret = mp_bridge_send_display_cmd(&cmd);
    if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Failed to send display command");
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(badge_display_clear_obj, badge_display_clear);

static mp_obj_t badge_display_pixel(mp_obj_t x_obj, mp_obj_t y_obj, mp_obj_t color_obj) {
    mp_display_cmd_t cmd = {
        .type = MP_DISPLAY_PIXEL,
        .x = mp_obj_get_int(x_obj),
        .y = mp_obj_get_int(y_obj),
        .color = mp_obj_get_int(color_obj)
    };
    
    esp_err_t ret = mp_bridge_send_display_cmd(&cmd);
    if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Failed to send display command");
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(badge_display_pixel_obj, badge_display_pixel);

static mp_obj_t badge_display_text(size_t n_args, const mp_obj_t *args) {
    // args: x, y, text, color
    if (n_args != 4) {
        mp_raise_TypeError("text() takes 4 arguments");
    }
    
    const char *text = mp_obj_str_get_str(args[2]);
    size_t text_len = strlen(text);
    
    if (text_len >= sizeof(((mp_display_cmd_t*)0)->text)) {
        mp_raise_ValueError("Text too long (max 127 chars)");
    }
    
    mp_display_cmd_t cmd = {
        .type = MP_DISPLAY_TEXT,
        .x = mp_obj_get_int(args[0]),
        .y = mp_obj_get_int(args[1]),
        .color = mp_obj_get_int(args[3])
    };
    strncpy(cmd.text, text, sizeof(cmd.text) - 1);
    
    esp_err_t ret = mp_bridge_send_display_cmd(&cmd);
    if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Failed to send display command");
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(badge_display_text_obj, 4, 4, badge_display_text);

static mp_obj_t badge_display_show(void) {
    mp_display_cmd_t cmd = {
        .type = MP_DISPLAY_SHOW
    };
    
    esp_err_t ret = mp_bridge_send_display_cmd(&cmd);
    if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Failed to send display command");
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(badge_display_show_obj, badge_display_show);

// Display module dict
static const mp_rom_map_elem_t badge_display_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&badge_display_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&badge_display_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&badge_display_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_show), MP_ROM_PTR(&badge_display_show_obj) },
};
static MP_DEFINE_CONST_DICT(badge_display_locals_dict, badge_display_locals_dict_table);

static const mp_obj_type_t badge_display_type = {
    { &mp_type_type },
    .name = MP_QSTR_display,
    .locals_dict = (mp_obj_dict_t *)&badge_display_locals_dict,
};

// ========== LED Functions ==========

static mp_obj_t badge_leds_set(mp_obj_t index_obj, mp_obj_t r_obj, mp_obj_t g_obj, mp_obj_t b_obj) {
    mp_led_cmd_t cmd = {
        .index = mp_obj_get_int(index_obj),
        .r = mp_obj_get_int(r_obj),
        .g = mp_obj_get_int(g_obj),
        .b = mp_obj_get_int(b_obj)
    };
    
    esp_err_t ret = mp_bridge_send_led_cmd(&cmd);
    if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Failed to send LED command");
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(badge_leds_set_obj, 4, 4, badge_leds_set);

// LEDs module dict
static const mp_rom_map_elem_t badge_leds_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_set), MP_ROM_PTR(&badge_leds_set_obj) },
};
static MP_DEFINE_CONST_DICT(badge_leds_locals_dict, badge_leds_locals_dict_table);

static const mp_obj_type_t badge_leds_type = {
    { &mp_type_type },
    .name = MP_QSTR_leds,
    .locals_dict = (mp_obj_dict_t *)&badge_leds_locals_dict,
};

// ========== Button Functions ==========

static mp_obj_t badge_buttons_is_pressed(mp_obj_t mask_obj) {
    uint8_t button_mask = mp_obj_get_int(mask_obj);
    
    mp_button_event_t event;
    // Non-blocking check
    esp_err_t ret = mp_bridge_recv_button_event(&event, 0);
    
    if (ret == ESP_OK && event.pressed && (event.button_mask & button_mask)) {
        return mp_const_true;
    }
    
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_1(badge_buttons_is_pressed_obj, badge_buttons_is_pressed);

// Buttons module dict
static const mp_rom_map_elem_t badge_buttons_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_is_pressed), MP_ROM_PTR(&badge_buttons_is_pressed_obj) },
};
static MP_DEFINE_CONST_DICT(badge_buttons_locals_dict, badge_buttons_locals_dict_table);

static const mp_obj_type_t badge_buttons_type = {
    { &mp_type_type },
    .name = MP_QSTR_buttons,
    .locals_dict = (mp_obj_dict_t *)&badge_buttons_locals_dict,
};

// ========== Utility Functions ==========

static mp_obj_t badge_delay_ms(mp_obj_t ms_obj) {
    int ms = mp_obj_get_int(ms_obj);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(badge_delay_ms_obj, badge_delay_ms);

// ========== Badge Module ==========

static const mp_rom_map_elem_t badge_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_badge) },
    { MP_ROM_QSTR(MP_QSTR_display), MP_ROM_PTR(&badge_display_type) },
    { MP_ROM_QSTR(MP_QSTR_leds), MP_ROM_PTR(&badge_leds_type) },
    { MP_ROM_QSTR(MP_QSTR_buttons), MP_ROM_PTR(&badge_buttons_type) },
    { MP_ROM_QSTR(MP_QSTR_delay_ms), MP_ROM_PTR(&badge_delay_ms_obj) },
};
static MP_DEFINE_CONST_DICT(badge_module_globals, badge_module_globals_table);

const mp_obj_module_t badge_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&badge_module_globals,
};

// Register module
MP_REGISTER_MODULE(MP_QSTR_badge, badge_module);
