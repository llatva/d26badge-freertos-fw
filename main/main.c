/*
 * Disobey Badge 2025 – FreeRTOS LED Menu Firmware
 * ================================================
 *
 * Architecture overview:
 *
 *   app_main() (CPU0)
 *     ├── Initialise drivers: ST7789, SK6812, buttons
 *     ├── Spawn input_task  – reads button events from ISR queue
 *     ├── Spawn display_task – owns the SPI bus; draws menu on request
 *     └── Spawn led_task    – drives SK6812 LEDs based on active mode
 *
 *  Shared state:
 *   - g_btn_queue   : ISR → input_task (btn_event_t)
 *   - g_disp_queue  : input_task → display_task (disp_cmd_t)
 *   - g_led_mode    : atomically updated int; led_task polls it
 *
 *  CPU affinity:
 *   - All tasks are pinned to CPU0 (PRO_CPU_NUM).
 *   - CPU1 is left free for future MicroPython VM.
 *
 * LED modes (menu items):
 *   0 – All Off
 *   1 – Red Solid
 *   2 – Green Solid
 *   3 – Blue Solid
 *   4 – Rainbow Cycle
 *   5 – Badge Identity  (DISOBEY colour wheel)
 *
 */

#include <math.h>
#include <stdlib.h>
#include "st7789.h"
#include "sk6812.h"
#include "buttons.h"
#include "menu_ui.h"
#include "menu_icons.h"
#include "version.h"
#include "audio_spectrum_screen.h"
#include "text_input_screen.h"
#include "badge_settings.h"
#include "idle_screen.h"
#include "ui_test_screen.h"
#include "sensor_readout_screen.h"
#include "signal_strength_screen.h"
#include "wlan_spectrum_screen.h"
#include "wlan_list_screen.h"
#include "ui_test_screen.h"
#include "about_screen.h"
#include "color_select_screen.h" /* New */
#include "audio.h"              /* Added for VU meter mode */
#include "hacky_bird.h"         /* Hacky Bird game */
#include "space_shooter.h"      /* Space Shooter game */
#include "snake.h"              /* Snake game */
#include "micropython_runner.h"  /* MicroPython integration */
#include "pyapps_fs.h"          /* Python apps filesystem */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/time.h>

#define TAG "main"

/* ── Display dimensions ───────────────────────────────────────────────────── */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 170

/* ── Queue sizes ─────────────────────────────────────────────────────────── */
#define BTN_QUEUE_LEN  16
#define DISP_QUEUE_LEN  4

/* ── LED mode ─────────────────────────────────────────────────────────────── */
typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_RED,
    LED_MODE_GREEN,
    LED_MODE_BLUE,
    LED_MODE_RAINBOW,
    LED_MODE_IDENTITY,
    LED_MODE_ACCENT,    /* Breathing with user accent color (wide swing) */
    LED_MODE_DISCO,     /* Fast random colors */
    LED_MODE_POLICE,    /* Red/Blue strobe on sides */
    LED_MODE_RELAX,     /* Slow smooth color morphing */
    LED_MODE_ROTATE,    /* Color rotating around the frame */
    LED_MODE_CHASE,     /* Single lit LED chasing */
    LED_MODE_MORPH,     /* Slow morph between colors */
    LED_MODE_BREATH_CYC,/* Breathing while color cycling */
    LED_MODE_FLAME,     /* Simulated flames on sides */
    LED_MODE_VU,        /* VU meter mode (MIC ON!) */
    LED_MODE_COUNT
} led_mode_t;

/* ── Application state ──────────────────────────────────────────────────── */
typedef enum {
    APP_STATE_IDLE = 0,
    APP_STATE_MENU = 1,
    APP_STATE_AUDIO_SPECTRUM = 2,
    APP_STATE_SETTINGS = 3,
    APP_STATE_SENSOR_READOUT = 4,
    APP_STATE_SIGNAL_STRENGTH = 5,
    APP_STATE_WLAN_SPECTRUM = 6,
    APP_STATE_WLAN_LIST = 7,
    APP_STATE_UI_TEST = 8,
    APP_STATE_ABOUT = 9,
    APP_STATE_COLOR_SELECT,
    APP_STATE_TEXT_COLOR_SELECT,
    APP_STATE_HACKY_BIRD,
    APP_STATE_SPACE_SHOOTER,
    APP_STATE_SNAKE,
    APP_STATE_PYTHON_DEMO,
    APP_STATE_TIME_DATE_SET,
} app_state_t;

static atomic_int g_app_state = APP_STATE_IDLE;

/* ── Display command / Redraw helper ────────────────────────────────────── */
typedef enum {
    DISP_CMD_REDRAW_FULL,
    DISP_CMD_REDRAW_ITEM,
} disp_cmd_type_t;

typedef struct {
    disp_cmd_type_t type;
} disp_cmd_t;

static QueueHandle_t  g_disp_queue;

static inline void request_redraw(disp_cmd_type_t type) {
    disp_cmd_t c = { .type = type };
    if (g_disp_queue) {
        xQueueSend(g_disp_queue, &c, pdMS_TO_TICKS(10));
    }
}

/* ── Shared globals ──────────────────────────────────────────────────────── */
static QueueHandle_t  g_btn_queue;
static atomic_int     g_led_mode = LED_MODE_ACCENT;
static menu_t         g_menu;           /* Main menu */
static menu_t         g_tools_menu;     /* Tools submenu */
static menu_t         g_diag_menu;      /* Diagnostics submenu */
static menu_t         g_settings_menu;  /* Settings submenu */
static menu_t         g_led_menu;       /* LED Animation submenu */
static menu_t         g_games_menu;     /* Games submenu */
static menu_t         g_dev_menu;       /* Development submenu */
static menu_t        *g_current_menu;   /* Pointer to current menu */
static audio_spectrum_screen_t g_audio_screen;
static text_input_screen_t g_text_input_screen;
static ui_test_screen_t g_ui_test_screen;
static sensor_readout_screen_t g_sensor_screen;
static signal_strength_screen_t g_signal_screen;
static wlan_spectrum_screen_t g_wlan_spectrum_screen;
static wlan_list_screen_t g_wlan_list_screen;
static color_select_screen_t g_color_screen;  /* New color select screen */
static bool g_hacky_bird_game_over = false;   /* Flag for game over state */

/* ── Forward declarations ────────────────────────────────────────────────── */
static void action_led_off(void);
static void action_led_rainbow(void);
static void action_led_identity(void);
static void action_led_accent(void);   /* New action for accent color */
static void action_audio_spectrum(void);
static void action_settings(void);
static void action_ui_test(void);
static void action_sensor_readout(void);
static void action_signal_strength(void);
static void action_wlan_spectrum(void);
static void action_wlan_list(void);
static void action_about(void);
static void action_color_select(void);  /* New action for color select */
static void action_hacky_bird(void);    /* Hacky Bird game */
static void action_space_shooter(void); /* Space Shooter game */
static void action_snake(void);         /* Snake game */
static void action_python_demo(void);   /* Python demo */
static void action_time_date_set(void); /* Time/date setting */

/* ── Menu action callbacks ───────────────────────────────────────────────── */
static void action_led_off(void)      { atomic_store(&g_led_mode, LED_MODE_OFF);      }
static void action_led_rainbow(void)  { atomic_store(&g_led_mode, LED_MODE_RAINBOW);  }
static void action_led_identity(void) { atomic_store(&g_led_mode, LED_MODE_IDENTITY); }
static void action_led_accent(void)   { atomic_store(&g_led_mode, LED_MODE_ACCENT);    }
static void action_led_disco(void)    { atomic_store(&g_led_mode, LED_MODE_DISCO);    }
static void action_led_police(void)   { atomic_store(&g_led_mode, LED_MODE_POLICE);   }
static void action_led_relax(void)    { atomic_store(&g_led_mode, LED_MODE_RELAX);    }
static void action_led_rotate(void)   { atomic_store(&g_led_mode, LED_MODE_ROTATE);   }
static void action_led_chase(void)    { atomic_store(&g_led_mode, LED_MODE_CHASE);    }
static void action_led_morph(void)    { atomic_store(&g_led_mode, LED_MODE_MORPH);    }
static void action_led_breath_cyc(void) { atomic_store(&g_led_mode, LED_MODE_BREATH_CYC); }
static void action_led_flame(void)     { atomic_store(&g_led_mode, LED_MODE_FLAME);     }
static void action_led_vu(void)        { atomic_store(&g_led_mode, LED_MODE_VU);        }

static void action_about(void) {
    ESP_LOGI(TAG, "Launching About Screen...");
    atomic_store(&g_app_state, APP_STATE_ABOUT);
}

static void action_audio_spectrum(void) {
    ESP_LOGI(TAG, "Launching Audio Spectrum Analyzer...");
    atomic_store(&g_app_state, APP_STATE_AUDIO_SPECTRUM);
    audio_spectrum_screen_init(&g_audio_screen);
    audio_spectrum_task_start(&g_audio_screen);
}

static void action_settings(void) {
    ESP_LOGI(TAG, "Launching Settings – Nickname Editor...");
    atomic_store(&g_app_state, APP_STATE_SETTINGS);
    text_input_init(&g_text_input_screen, "Nickname (Max 10):", 11);
    text_input_set_text(&g_text_input_screen, settings_get_nickname());
}

static void action_ui_test(void) {
    ESP_LOGI(TAG, "Launching UI Test Screen...");
    atomic_store(&g_app_state, APP_STATE_UI_TEST);
    ui_test_screen_init(&g_ui_test_screen);
}

static void action_sensor_readout(void) {
    ESP_LOGI(TAG, "Launching Sensor Readout...");
    atomic_store(&g_app_state, APP_STATE_SENSOR_READOUT);
    sensor_readout_screen_init(&g_sensor_screen);
}

static void action_signal_strength(void) {
    ESP_LOGI(TAG, "Launching Signal Strength Display...");
    atomic_store(&g_app_state, APP_STATE_SIGNAL_STRENGTH);
    signal_strength_screen_init(&g_signal_screen);
}

static void action_wlan_spectrum(void) {
    ESP_LOGI(TAG, "Launching WLAN Spectrum Analyzer...");
    atomic_store(&g_app_state, APP_STATE_WLAN_SPECTRUM);
    wlan_spectrum_screen_init(&g_wlan_spectrum_screen);
    wlan_spectrum_screen_start_scan(&g_wlan_spectrum_screen);
}

static void action_wlan_list(void) {
    ESP_LOGI(TAG, "Launching WLAN Networks List...");
    atomic_store(&g_app_state, APP_STATE_WLAN_LIST);
    wlan_list_screen_init(&g_wlan_list_screen);
    wlan_list_screen_start_scan(&g_wlan_list_screen);
}

static void action_color_select(void) {
    ESP_LOGI(TAG, "Launching Accent Color Selector...");
    color_select_screen_init(&g_color_screen, settings_get_accent_color(), "Accent Color");
    atomic_store(&g_app_state, APP_STATE_COLOR_SELECT);
    request_redraw(DISP_CMD_REDRAW_FULL);
}

static void action_text_color_select(void) {
    ESP_LOGI(TAG, "Launching Text Color Selector...");
    color_select_screen_init(&g_color_screen, settings_get_text_color(), "Text Color");
    atomic_store(&g_app_state, APP_STATE_TEXT_COLOR_SELECT);
    request_redraw(DISP_CMD_REDRAW_FULL);
}

static void action_hacky_bird(void) {
    ESP_LOGI(TAG, "Launching Hacky Bird...");
    atomic_store(&g_app_state, APP_STATE_HACKY_BIRD);
    g_hacky_bird_game_over = false;
    hacky_bird_init();
    request_redraw(DISP_CMD_REDRAW_FULL);
}

static void action_space_shooter(void) {
    ESP_LOGI(TAG, "Launching Space Shooter...");
    atomic_store(&g_app_state, APP_STATE_SPACE_SHOOTER);
    space_shooter_init();
    request_redraw(DISP_CMD_REDRAW_FULL);
}

static void action_snake(void) {
    ESP_LOGI(TAG, "Launching Snake...");
    atomic_store(&g_app_state, APP_STATE_SNAKE);
    snake_init();
    request_redraw(DISP_CMD_REDRAW_FULL);
}

/* ── Python demo ─────────────────────────────────────────────────────────── */

/*
 * Multi-page interactive MicroPython demo with real stdout capture.
 * LEFT/RIGHT switch between demos, B exits.
 * Each demo runs a Python script and displays the actual captured output.
 */

#define PY_CAPTURE_SIZE  2048   /* stdout capture buffer size */
#define PY_NUM_DEMOS     6

static const char *PY_DEMO_TITLES[PY_NUM_DEMOS] = {
    "Fibonacci",
    "Prime Sieve",
    "Classes & OOP",
    "Generators",
    "Mandelbrot",
    "Badge Info",
};

static const char *PY_DEMO_SCRIPTS[PY_NUM_DEMOS] = {
    /* 0: Fibonacci – recursive + iterative with timing */
    "import time\n"
    "def fib_recursive(n):\n"
    "    if n <= 1: return n\n"
    "    return fib_recursive(n-1) + fib_recursive(n-2)\n"
    "\n"
    "def fib_iter(n):\n"
    "    a, b = 0, 1\n"
    "    for _ in range(n):\n"
    "        a, b = b, a + b\n"
    "    return a\n"
    "\n"
    "print('Fibonacci Sequence')\n"
    "print('~' * 30)\n"
    "seq = [fib_iter(i) for i in range(15)]\n"
    "print('F(0..14):', seq)\n"
    "print()\n"
    "t0 = time.ticks_ms()\n"
    "r = fib_recursive(20)\n"
    "dt = time.ticks_diff(time.ticks_ms(), t0)\n"
    "print('F(20) recursive:', r)\n"
    "print('  Time:', dt, 'ms')\n"
    "t0 = time.ticks_ms()\n"
    "r = fib_iter(100)\n"
    "dt = time.ticks_diff(time.ticks_ms(), t0)\n"
    "print('F(100) iterative:', r)\n"
    "print('  Time:', dt, 'ms')\n"
    "print()\n"
    "# Golden ratio approximation\n"
    "a, b = fib_iter(30), fib_iter(29)\n"
    "print('Golden ratio ~=', a / b)\n"
    ,

    /* 1: Prime Sieve */
    "def sieve(limit):\n"
    "    is_prime = [True] * (limit + 1)\n"
    "    is_prime[0] = is_prime[1] = False\n"
    "    for i in range(2, int(limit**0.5) + 1):\n"
    "        if is_prime[i]:\n"
    "            for j in range(i*i, limit+1, i):\n"
    "                is_prime[j] = False\n"
    "    return [i for i in range(limit+1) if is_prime[i]]\n"
    "\n"
    "import time\n"
    "print('Sieve of Eratosthenes')\n"
    "print('~' * 30)\n"
    "t0 = time.ticks_ms()\n"
    "primes = sieve(1000)\n"
    "dt = time.ticks_diff(time.ticks_ms(), t0)\n"
    "print('Primes up to 1000:', len(primes))\n"
    "print('First 20:', primes[:20])\n"
    "print('Last 10: ', primes[-10:])\n"
    "print('Time:', dt, 'ms')\n"
    "print()\n"
    "# Twin primes (use set for fast lookup)\n"
    "prime_set = set(primes)\n"
    "twins = [(p, p+2) for p in primes if p+2 in prime_set]\n"
    "print('Twin primes:', len(twins))\n"
    "print('First 8:', twins[:8])\n"
    ,

    /* 2: Classes & OOP */
    "class Vector:\n"
    "    def __init__(self, x, y):\n"
    "        self.x = x\n"
    "        self.y = y\n"
    "    def __add__(self, o):\n"
    "        return Vector(self.x+o.x, self.y+o.y)\n"
    "    def __mul__(self, s):\n"
    "        return Vector(self.x*s, self.y*s)\n"
    "    def mag(self):\n"
    "        return (self.x**2 + self.y**2)**0.5\n"
    "    def __repr__(self):\n"
    "        return 'Vec(%g,%g)' % (self.x, self.y)\n"
    "\n"
    "class Particle:\n"
    "    def __init__(self, pos, vel):\n"
    "        self.pos = pos\n"
    "        self.vel = vel\n"
    "    def step(self, dt):\n"
    "        self.pos = self.pos + self.vel * dt\n"
    "    def __repr__(self):\n"
    "        return 'P@%s v=%s' % (self.pos, self.vel)\n"
    "\n"
    "print('Classes & OOP')\n"
    "print('~' * 30)\n"
    "v1 = Vector(3, 4)\n"
    "v2 = Vector(1, -2)\n"
    "print('v1 =', v1, ' |v1| =', '%.2f' % v1.mag())\n"
    "print('v2 =', v2)\n"
    "print('v1+v2 =', v1 + v2)\n"
    "print('v1*3  =', v1 * 3)\n"
    "print()\n"
    "p = Particle(Vector(0,0), Vector(10,5))\n"
    "print('Simulating particle:')\n"
    "for i in range(5):\n"
    "    p.step(0.1)\n"
    "    print(' t=%.1f  %s' % ((i+1)*0.1, p))\n"
    ,

    /* 3: Generators & Functional */
    "def countdown(n):\n"
    "    while n > 0:\n"
    "        yield n\n"
    "        n -= 1\n"
    "\n"
    "def take(gen, n):\n"
    "    result = []\n"
    "    for x in gen:\n"
    "        result.append(x)\n"
    "        if len(result) >= n:\n"
    "            break\n"
    "    return result\n"
    "\n"
    "def collatz(n):\n"
    "    seq = [n]\n"
    "    while n != 1:\n"
    "        n = n // 2 if n % 2 == 0 else 3 * n + 1\n"
    "        seq.append(n)\n"
    "    return seq\n"
    "\n"
    "print('Generators & Functional')\n"
    "print('~' * 30)\n"
    "print('Countdown:', list(countdown(5)))\n"
    "print()\n"
    "# Map/filter/reduce\n"
    "nums = list(range(1, 11))\n"
    "sq   = list(map(lambda x: x**2, nums))\n"
    "evn  = list(filter(lambda x: x%2==0, sq))\n"
    "print('Numbers:', nums)\n"
    "print('Squared:', sq)\n"
    "print('Even sq:', evn)\n"
    "print()\n"
    "# Collatz conjecture\n"
    "for start in [7, 27]:\n"
    "    c = collatz(start)\n"
    "    print('Collatz(%d): %d steps' % (start, len(c)))\n"
    "    print(' ', c[:12], '...' if len(c)>12 else '')\n"
    ,

    /* 4: Mandelbrot (ASCII art) */
    "print('Mandelbrot Set')\n"
    "print('~' * 30)\n"
    "W, H = 38, 9\n"
    "chars = ' .:-=+*#%@'\n"
    "for row in range(H):\n"
    "    y0 = (row / H) * 2.4 - 1.2\n"
    "    line = ''\n"
    "    for col in range(W):\n"
    "        x0 = (col / W) * 3.5 - 2.5\n"
    "        x, y, it = 0.0, 0.0, 0\n"
    "        while x*x + y*y < 4 and it < 30:\n"
    "            x, y = x*x - y*y + x0, 2*x*y + y0\n"
    "            it += 1\n"
    "        line += chars[min(it * len(chars) // 31, len(chars)-1)]\n"
    "    print(line)\n"
    "print()\n"
    "print('x: [-2.5, 1.0]  y: [-1.2, 1.2]')\n"
    "print('30 iterations, 38x9 chars')\n"
    ,

    /* 5: Badge Info */
    "import sys\n"
    "import gc\n"
    "print('Badge System Info')\n"
    "print('~' * 30)\n"
    "print('MicroPython:', sys.version)\n"
    "print('Platform:   ', sys.platform)\n"
    "print('Byte order: ', sys.byteorder)\n"
    "print('Max int:    ', sys.maxsize)\n"
    "print()\n"
    "gc.collect()\n"
    "free = gc.mem_free()\n"
    "used = gc.mem_alloc()\n"
    "total = free + used\n"
    "pct = used * 100 // total\n"
    "print('Memory:')\n"
    "print('  Total: %d bytes' % total)\n"
    "print('  Used:  %d bytes (%d%%)' % (used, pct))\n"
    "print('  Free:  %d bytes' % free)\n"
    "bar_w = 20\n"
    "filled = used * bar_w // total\n"
    "print('  [' + '#'*filled + '.'*(bar_w-filled) + ']')\n"
    "print()\n"
    "# Feature test\n"
    "features = []\n"
    "try:\n"
    "    1+1j\n"
    "    features.append('complex')\n"
    "except: pass\n"
    "try:\n"
    "    {1,2,3}\n"
    "    features.append('set')\n"
    "except: pass\n"
    "try:\n"
    "    b'\\x00'\n"
    "    features.append('bytes')\n"
    "except: pass\n"
    "features.append('generators')\n"
    "features.append('closures')\n"
    "print('Features:', ', '.join(features))\n"
    "print('Modules: ', list(sys.modules.keys()))\n"
    ,
};

/* LED colour themes per demo */
static const sk6812_color_t PY_DEMO_COLORS[PY_NUM_DEMOS] = {
    {  0, 120,  60},  /* Fibonacci:  teal    */
    {120,  80,   0},  /* Primes:     amber   */
    { 80,   0, 120},  /* Classes:    purple  */
    {  0,  60, 120},  /* Generators: blue    */
    {120,   0,  40},  /* Mandelbrot: crimson */
    {  0, 100,  20},  /* Badge info: green   */
};

/* Helper: count lines in a string */
static int count_lines(const char *s)
{
    int n = 0;
    while (*s) { if (*s == '\n') n++; s++; }
    return n;
}

/* Helper: get pointer to line N (0-based) in a string */
static const char *get_line(const char *s, int line_idx, int *out_len)
{
    int cur = 0;
    while (cur < line_idx && *s) {
        if (*s == '\n') cur++;
        s++;
    }
    const char *start = s;
    while (*s && *s != '\n') s++;
    *out_len = (int)(s - start);
    return start;
}

/* Task wrapper: interactive multi-demo Python showcase */
static void python_demo_task(void *arg)
{
    ESP_LOGI(TAG, "Python demo task started");

    char *capture_buf = heap_caps_malloc(PY_CAPTURE_SIZE, MALLOC_CAP_8BIT);
    if (!capture_buf) {
        ESP_LOGE(TAG, "Failed to allocate capture buffer");
        atomic_store(&g_app_state, APP_STATE_MENU);
        vTaskDelete(NULL);
        return;
    }

    int demo_idx = 0;
    int scroll_offset = 0;
    bool needs_run = true;
    bool needs_draw = true;
    int rc = 0;
    int total_lines = 0;

    /* Display lines: 170px height. Title=16px(scale2) + 2px gap + status=16px = 34px top.
     * Bottom: "< B:exit >" = 16px. Usable: 170 - 34 - 16 = 120px => 7 lines at 16px */
    const int DISPLAY_LINES = 7;
    const int OUTPUT_Y_START = 36;

    while (atomic_load(&g_app_state) == APP_STATE_PYTHON_DEMO) {
        /* Run the current demo if needed */
        if (needs_run) {
            needs_run = false;
            scroll_offset = 0;

            /* Draw running screen */
            st7789_fill(0x0000);
            st7789_draw_string(10, 10, PY_DEMO_TITLES[demo_idx], 0x07E0, 0x0000, 2);

            /* Show demo index */
            char idx_str[16];
            snprintf(idx_str, sizeof(idx_str), "[%d/%d]", demo_idx + 1, PY_NUM_DEMOS);
            st7789_draw_string(320 - 8 * strlen(idx_str) - 4, 14, idx_str, 0x7BEF, 0x0000, 1);

            st7789_draw_string(10, 40, "Running...", 0xFFE0, 0x0000, 1);

            /* Set LED colour for this demo */
            sk6812_color_t col = PY_DEMO_COLORS[demo_idx];
            for (int i = 0; i < SK6812_LED_COUNT; i++) {
                uint8_t bright = (i * 255) / SK6812_LED_COUNT;
                sk6812_set(i, sk6812_scale(col, bright / 3 + 20));
            }
            sk6812_show();

            /* Run with capture */
            mp_hal_capture_start(capture_buf, PY_CAPTURE_SIZE);
            rc = micropython_run_code(PY_DEMO_SCRIPTS[demo_idx]);
            mp_hal_capture_stop();

            total_lines = count_lines(capture_buf);
            if (total_lines == 0 && capture_buf[0]) total_lines = 1;
            needs_draw = true;
        }

        /* Draw output */
        if (needs_draw) {
            needs_draw = false;

            st7789_fill(0x0000);

            /* Title bar */
            st7789_draw_string(10, 2, PY_DEMO_TITLES[demo_idx], 0x07E0, 0x0000, 2);
            char idx_str[16];
            snprintf(idx_str, sizeof(idx_str), "[%d/%d]", demo_idx + 1, PY_NUM_DEMOS);
            st7789_draw_string(320 - 8 * strlen(idx_str) - 4, 6, idx_str, 0x7BEF, 0x0000, 1);

            /* Status */
            if (rc == 0) {
                char status[32];
                snprintf(status, sizeof(status), "OK  %d lines", total_lines);
                st7789_draw_string(10, 20, status, 0x07E0, 0x0000, 1);
            } else {
                st7789_draw_string(10, 20, "ERROR - check serial", 0xF800, 0x0000, 1);
            }

            /* Output lines */
            for (int i = 0; i < DISPLAY_LINES; i++) {
                int line_idx = scroll_offset + i;
                if (line_idx >= total_lines) break;

                int len;
                const char *line = get_line(capture_buf, line_idx, &len);

                /* Truncate to screen width (40 chars at scale 1) */
                char line_buf[41];
                int show = (len > 40) ? 40 : len;
                memcpy(line_buf, line, show);
                line_buf[show] = '\0';

                uint16_t color = 0xFFFF;  /* white */
                /* Highlight title lines (starting with ~) in cyan */
                if (show > 0 && line_buf[0] == '~') color = 0x07FF;

                st7789_draw_string(4, OUTPUT_Y_START + i * 16, line_buf, color, 0x0000, 1);
            }

            /* Scroll indicator */
            if (total_lines > DISPLAY_LINES) {
                int bar_h = 120 * DISPLAY_LINES / total_lines;
                if (bar_h < 8) bar_h = 8;
                int bar_y = OUTPUT_Y_START;
                if (total_lines > DISPLAY_LINES) {
                    bar_y = OUTPUT_Y_START + (120 - bar_h) * scroll_offset / (total_lines - DISPLAY_LINES);
                }
                /* Draw thin scrollbar on right edge */
                for (int y = bar_y; y < bar_y + bar_h && y < OUTPUT_Y_START + 120; y++) {
                    st7789_draw_string(316, y, "|", 0x4208, 0x0000, 1);
                }
            }

            /* Bottom nav bar */
            st7789_draw_string(4, 156, "<", 0xFFE0, 0x0000, 1);
            st7789_draw_string(100, 156, "B:exit", 0xF800, 0x0000, 1);
            st7789_draw_string(220, 156, "U/D:scroll", 0x7BEF, 0x0000, 1);
            st7789_draw_string(310, 156, ">", 0xFFE0, 0x0000, 1);
        }

        /* Poll buttons */
        if (buttons_is_pressed(BTN_LEFT)) {
            demo_idx = (demo_idx - 1 + PY_NUM_DEMOS) % PY_NUM_DEMOS;
            needs_run = true;
            while (buttons_is_pressed(BTN_LEFT)) vTaskDelay(pdMS_TO_TICKS(30));
        }
        if (buttons_is_pressed(BTN_RIGHT)) {
            demo_idx = (demo_idx + 1) % PY_NUM_DEMOS;
            needs_run = true;
            while (buttons_is_pressed(BTN_RIGHT)) vTaskDelay(pdMS_TO_TICKS(30));
        }
        if (buttons_is_pressed(BTN_UP)) {
            if (scroll_offset > 0) { scroll_offset--; needs_draw = true; }
            vTaskDelay(pdMS_TO_TICKS(120));
        }
        if (buttons_is_pressed(BTN_DOWN)) {
            if (scroll_offset < total_lines - DISPLAY_LINES) { scroll_offset++; needs_draw = true; }
            vTaskDelay(pdMS_TO_TICKS(120));
        }
        if (buttons_is_pressed(BTN_B)) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Clean up */
    heap_caps_free(capture_buf);
    sk6812_clear();

    ESP_LOGI(TAG, "Python demo exiting");
    atomic_store(&g_app_state, APP_STATE_MENU);
    request_redraw(DISP_CMD_REDRAW_FULL);
    vTaskDelete(NULL);
}

static void action_python_demo(void) {
    ESP_LOGI(TAG, "Launching Python Demo...");
    atomic_store(&g_app_state, APP_STATE_PYTHON_DEMO);

    /* Spawn a task with 32KB stack (MicroPython needs ≥16KB + capture overhead).
     * ESP-IDF xTaskCreatePinnedToCore takes stack size in bytes directly. */
    xTaskCreatePinnedToCore(python_demo_task, "py_demo", 32768,
                            NULL, 5, NULL, PRO_CPU_NUM);
}

/* ── Time/Date Setting ───────────────────────────────────────────────────── */

/*
 * Interactive time/date editor.
 *
 * Fields:  HOUR  MINUTE  YEAR  MONTH  DAY
 * LEFT/RIGHT  – move cursor between fields
 * UP/DOWN     – increment/decrement the selected field
 * A           – confirm and apply
 * B           – cancel and go back
 */

#define TD_NUM_FIELDS  5
#define TD_FIELD_HOUR  0
#define TD_FIELD_MIN   1
#define TD_FIELD_YEAR  2
#define TD_FIELD_MON   3
#define TD_FIELD_DAY   4

static int      s_td_fields[TD_NUM_FIELDS];     /* current values       */
static int      s_td_cursor;                     /* selected field 0..4  */
static bool     s_td_needs_draw;

static const char *TD_LABELS[TD_NUM_FIELDS] = {
    "Hour", "Min", "Year", "Month", "Day"
};

/* Days in month (handles leap year for year in s_td_fields[TD_FIELD_YEAR]) */
static int td_days_in_month(int mon, int year) {
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (mon < 1 || mon > 12) return 31;
    int d = dim[mon - 1];
    if (mon == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        d = 29;
    return d;
}

static void td_clamp(void) {
    if (s_td_fields[TD_FIELD_HOUR] < 0)  s_td_fields[TD_FIELD_HOUR] = 23;
    if (s_td_fields[TD_FIELD_HOUR] > 23) s_td_fields[TD_FIELD_HOUR] = 0;
    if (s_td_fields[TD_FIELD_MIN]  < 0)  s_td_fields[TD_FIELD_MIN]  = 59;
    if (s_td_fields[TD_FIELD_MIN]  > 59) s_td_fields[TD_FIELD_MIN]  = 0;
    if (s_td_fields[TD_FIELD_YEAR] < 2024) s_td_fields[TD_FIELD_YEAR] = 2030;
    if (s_td_fields[TD_FIELD_YEAR] > 2030) s_td_fields[TD_FIELD_YEAR] = 2024;
    if (s_td_fields[TD_FIELD_MON]  < 1)  s_td_fields[TD_FIELD_MON]  = 12;
    if (s_td_fields[TD_FIELD_MON]  > 12) s_td_fields[TD_FIELD_MON]  = 1;
    int max_day = td_days_in_month(s_td_fields[TD_FIELD_MON], s_td_fields[TD_FIELD_YEAR]);
    if (s_td_fields[TD_FIELD_DAY] < 1)       s_td_fields[TD_FIELD_DAY] = max_day;
    if (s_td_fields[TD_FIELD_DAY] > max_day) s_td_fields[TD_FIELD_DAY] = 1;
}

static void td_draw(void) {
    st7789_fill(0x0000);

    /* Title */
    st7789_draw_string(60, 4, "Set Time & Date", 0x07E0, 0x0000, 2);

    /* Decorative line */
    st7789_fill_rect(0, 38, 320, 1, 0x4208);

    /* ── Time row: HH : MM ── */
    char buf[8];

    /* Hour */
    uint16_t h_fg = (s_td_cursor == TD_FIELD_HOUR) ? 0x0000 : 0xFFFF;
    uint16_t h_bg = (s_td_cursor == TD_FIELD_HOUR) ? 0xFFE0 : 0x0000;
    snprintf(buf, sizeof(buf), "%02d", s_td_fields[TD_FIELD_HOUR]);
    st7789_draw_string(80, 50, buf, h_fg, h_bg, 4);

    /* Colon */
    st7789_draw_string(146, 50, ":", 0xFFFF, 0x0000, 4);

    /* Minute */
    uint16_t m_fg = (s_td_cursor == TD_FIELD_MIN) ? 0x0000 : 0xFFFF;
    uint16_t m_bg = (s_td_cursor == TD_FIELD_MIN) ? 0xFFE0 : 0x0000;
    snprintf(buf, sizeof(buf), "%02d", s_td_fields[TD_FIELD_MIN]);
    st7789_draw_string(176, 50, buf, m_fg, m_bg, 4);

    /* ── Date row: YYYY-MM-DD ── */
    int y_x = 32;

    /* Year */
    uint16_t y_fg = (s_td_cursor == TD_FIELD_YEAR) ? 0x0000 : 0xB7E0;
    uint16_t y_bg = (s_td_cursor == TD_FIELD_YEAR) ? 0x07FF : 0x0000;
    snprintf(buf, sizeof(buf), "%04d", s_td_fields[TD_FIELD_YEAR]);
    st7789_draw_string(y_x, 118, buf, y_fg, y_bg, 2);

    st7789_draw_string(y_x + 64, 118, "-", 0xB7E0, 0x0000, 2);

    /* Month */
    uint16_t mo_fg = (s_td_cursor == TD_FIELD_MON) ? 0x0000 : 0xB7E0;
    uint16_t mo_bg = (s_td_cursor == TD_FIELD_MON) ? 0x07FF : 0x0000;
    snprintf(buf, sizeof(buf), "%02d", s_td_fields[TD_FIELD_MON]);
    st7789_draw_string(y_x + 80, 118, buf, mo_fg, mo_bg, 2);

    st7789_draw_string(y_x + 112, 118, "-", 0xB7E0, 0x0000, 2);

    /* Day */
    uint16_t d_fg = (s_td_cursor == TD_FIELD_DAY) ? 0x0000 : 0xB7E0;
    uint16_t d_bg = (s_td_cursor == TD_FIELD_DAY) ? 0x07FF : 0x0000;
    snprintf(buf, sizeof(buf), "%02d", s_td_fields[TD_FIELD_DAY]);
    st7789_draw_string(y_x + 128, 118, buf, d_fg, d_bg, 2);

    /* Field label */
    st7789_draw_string(y_x + 176, 122, TD_LABELS[s_td_cursor], 0x7BEF, 0x0000, 1);

    /* Navigation hints */
    st7789_draw_string(4, 152, "U/D:adjust", 0x7BEF, 0x0000, 1);
    st7789_draw_string(110, 152, "A:set", 0x07E0, 0x0000, 1);
    st7789_draw_string(200, 152, "B:cancel", 0xF800, 0x0000, 1);
}

static void action_time_date_set(void) {
    ESP_LOGI(TAG, "Launching Time/Date Setting...");

    /* Read current system time into fields */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    s_td_fields[TD_FIELD_HOUR] = t->tm_hour;
    s_td_fields[TD_FIELD_MIN]  = t->tm_min;
    s_td_fields[TD_FIELD_YEAR] = t->tm_year + 1900;
    s_td_fields[TD_FIELD_MON]  = t->tm_mon + 1;
    s_td_fields[TD_FIELD_DAY]  = t->tm_mday;
    s_td_cursor = TD_FIELD_HOUR;
    s_td_needs_draw = true;

    atomic_store(&g_app_state, APP_STATE_TIME_DATE_SET);
    request_redraw(DISP_CMD_REDRAW_FULL);
}

/* ── Rainbow helper ──────────────────────────────────────────────────────── */
static sk6812_color_t wheel(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85)  return (sk6812_color_t){ 255 - pos * 3,   0,           pos * 3   };
    if (pos < 170) { pos -= 85;  return (sk6812_color_t){ 0,           pos * 3,   255 - pos * 3 }; }
    pos -= 170;    return (sk6812_color_t){ pos * 3,         255 - pos * 3, 0           };
}

/* ── LED task ────────────────────────────────────────────────────────────── */
/*
 * DISOBEY identity palette: magenta/pink, white, black.
 * 8 LEDs: 4 magenta alternating with 4 white, fading in/out.
 */
#define IDENTITY_STEPS 64
static const sk6812_color_t DISOBEY_A = {255,  0, 200};  /* hot magenta */
static const sk6812_color_t DISOBEY_B = {255,255, 255};  /* white       */

static void led_task(void *arg) {
    (void)arg;
    uint32_t phase = 0;

    while (1) {
        led_mode_t mode = (led_mode_t)atomic_load(&g_led_mode);

        switch (mode) {
        case LED_MODE_OFF:
            sk6812_clear();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_RED:
            sk6812_fill(sk6812_scale(SK6812_RED, 40));
            sk6812_show();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_GREEN:
            sk6812_fill(sk6812_scale(SK6812_GREEN, 40));
            sk6812_show();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_BLUE:
            sk6812_fill(sk6812_scale(SK6812_BLUE, 40));
            sk6812_show();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_RAINBOW:
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                sk6812_set(i, sk6812_scale(wheel((i * 32 + phase) & 0xFF), 127));
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(30));
            break;

        case LED_MODE_IDENTITY: {
            /* 
             * DISOBEY Dynamic Identity:
             * Cycles through 4 sub-animations every ~5 seconds.
             */
            uint16_t sub_phase = (uint16_t)(phase & 1023);
            uint8_t sub_mode   = (sub_phase >> 8);  /* 0, 1, 2, or 3 */
            uint8_t t8         = (uint8_t)(sub_phase & 0xFF);

            if (sub_mode == 0) {
                /* Sub-mode 0: Breathing alternating colors (classic) */
                float t = (sinf((float)t8 * 0.05f) + 1.0f) / 2.0f;
                uint8_t bri = (uint8_t)(t * 50.0f + 10.0f);
                for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                    sk6812_color_t c = (i % 2 == 0) ? DISOBEY_A : DISOBEY_B;
                    sk6812_set(i, sk6812_scale(c, bri));
                }
            } else if (sub_mode == 1) {
                /* Sub-mode 1: Rotating Magenta block on White background */
                uint8_t p = (t8 / 16) % SK6812_LED_COUNT;
                for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                    if (i == p) {
                        sk6812_set(i, sk6812_scale(DISOBEY_A, 60));
                    } else {
                        sk6812_set(i, sk6812_scale(DISOBEY_B, 20));
                    }
                }
            } else if (sub_mode == 2) {
                /* Sub-mode 2: Disobey Scanner (Magenta scanner) */
                int pos = (t8 / 10) % (SK6812_LED_COUNT * 2);
                if (pos >= SK6812_LED_COUNT) pos = (SK6812_LED_COUNT * 2 - 1) - pos;
                for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                    if (i == pos) {
                        sk6812_set(i, sk6812_scale(DISOBEY_A, 60));
                    } else {
                        sk6812_set(i, sk6812_scale(DISOBEY_B, 10));
                    }
                }
            } else {
                /* Sub-mode 3: Stroboscopic Magenta/White swap (alternating sides) */
                bool flip = (t8 % 20 < 10);
                uint8_t half = SK6812_LED_COUNT / 2;
                for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                    if (i < half) {
                        sk6812_set(i, sk6812_scale(flip ? DISOBEY_A : DISOBEY_B, 50));
                    } else {
                        sk6812_set(i, sk6812_scale(flip ? DISOBEY_B : DISOBEY_A, 50));
                    }
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case LED_MODE_ACCENT: {
            /* Breathing effect with the selected accent color (wide swing) */
            uint16_t c16 = settings_get_accent_color();
            /* Convert RGB565 to RGB888 base color */
            uint8_t r5 = (c16 >> 11) & 0x1F;
            uint8_t g6 = (c16 >> 5)  & 0x3F;
            uint8_t b5 = c16 & 0x1F;
            sk6812_color_t base = {
                (uint8_t)((r5 << 3) | (r5 >> 2)),
                (uint8_t)((g6 << 2) | (g6 >> 4)),
                (uint8_t)((b5 << 3) | (b5 >> 2))
            };

            /* Sine breathing: (sin(t) + 1) / 2 */
            float s = (sinf((float)phase * 0.05f) + 1.0f) / 2.0f;
            /* Wide swing: 1 to 90 brightness (approx half of previous max) */
            uint8_t bri = (uint8_t)(s * 89.0f + 1.0f);
            
            sk6812_fill(sk6812_scale(base, bri));
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        }

        case LED_MODE_DISCO: {
            /* Rapid flashing and random colors / patterns (disco style) */
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                if (((phase >> 1) + i) % 3 == 0) {
                    sk6812_set(i, sk6812_scale(wheel((phase * 16 + i * 16) & 0xFF), 127));
                } else {
                    sk6812_set(i, SK6812_BLACK);
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(60));
            break;
        }

        case LED_MODE_POLICE: {
            /* Alternating red/blue strobes on sides (disco house look) */
            uint8_t half = SK6812_LED_COUNT / 2;
            bool left_on = (phase % 10 < 5);
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                if (i < half) {
                    sk6812_set(i, left_on ? sk6812_scale(SK6812_RED, 127)   : SK6812_BLACK);
                } else {
                    sk6812_set(i, left_on ? SK6812_BLACK : sk6812_scale(SK6812_BLUE, 127));
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(80));
            break;
        }

        case LED_MODE_RELAX: {
            /* Slow, smooth smooth pulse transitions between relaxing colors */
            float s = (sinf((float)phase * 0.02f) + 1.0f) / 2.0f;
            sk6812_color_t color1 = {100, 0, 150}; // Mauve/Purple
            sk6812_color_t color2 = {0, 150, 120}; // Teal/Green
            sk6812_color_t blend = {
                (uint8_t)(color1.r * (1.0f - s) + color2.r * s),
                (uint8_t)(color1.g * (1.0f - s) + color2.g * s),
                (uint8_t)(color1.b * (1.0f - s) + color2.b * s)
            };
            sk6812_fill(sk6812_scale(blend, 25)); // Even gentler (half of 50)
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }

        case LED_MODE_ROTATE: {
            /* Rotating block of color */
            uint8_t p = (phase / 4) % SK6812_LED_COUNT;
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                uint8_t dist = (i >= p) ? (i - p) : (SK6812_LED_COUNT + i - p);
                if (dist < 3) {
                    sk6812_set(i, sk6812_scale(wheel((phase * 4) & 0xFF), (3 - dist) * 30));
                } else {
                    sk6812_set(i, SK6812_BLACK);
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(40));
            break;
        }

        case LED_MODE_CHASE: {
            /* Single bright LED chasing with a tail */
            uint8_t pos = phase % SK6812_LED_COUNT;
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                if (i == pos) {
                    sk6812_set(i, sk6812_scale(SK6812_WHITE, 100));
                } else {
                    /* dim tail */
                    uint8_t prev = (pos + SK6812_LED_COUNT - 1) % SK6812_LED_COUNT;
                    uint8_t pprev = (pos + SK6812_LED_COUNT - 2) % SK6812_LED_COUNT;
                    if (i == prev) sk6812_set(i, sk6812_scale(SK6812_BLUE, 50));
                    else if (i == pprev) sk6812_set(i, sk6812_scale(SK6812_BLUE, 20));
                    else sk6812_set(i, SK6812_BLACK);
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(60));
            break;
        }

        case LED_MODE_MORPH: {
            /* Super slow hue morph of everything at once */
            sk6812_color_t color = sk6812_scale(wheel(phase), 40);
            sk6812_fill(color);
            sk6812_show();
            if ((phase % 4) == 0) phase++; /* Slower update */
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }

        case LED_MODE_BREATH_CYC: {
            /* Breathing with color cycling */
            float s = (sinf((float)phase * 0.05f) + 1.0f) / 2.0f;
            uint8_t bri = (uint8_t)(s * 80.0f + 5.0f);
            sk6812_fill(sk6812_scale(wheel(phase * 2), bri));
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        }

        case LED_MODE_FLAME: {
            /* Better flame: bright core at bottom, flickering dimming tops, relaxed movement */
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                uint8_t v_pos = i % 6; // Assume 0 is bottom, 5 is top for each bar
                
                /* 1. Base intensity: fades out as we go up */
                float base = 1.0f - ((float)v_pos * 0.15f); // 1.0, 0.85, 0.7... 0.25
                
                /* 2. Slow 'draft' effect using sine for organic sway */
                float draft = (sinf((float)phase * 0.04f + (float)v_pos * 0.3f) + 1.0f) * 0.12f;
                
                /* 3. High-frequency flicker: becomes dominant and 'unstable' at the tips */
                float instability = (float)(rand() % 100) / 100.0f;
                float flicker_amt = instability * (0.05f + (float)v_pos * 0.12f);
                
                float intensity = base + draft - flicker_amt;
                if (intensity < 0.05f) intensity = 0.0f; // Allow tips to fully extinguish
                if (intensity > 1.0f)  intensity = 1.0f;

                uint8_t h = (uint8_t)(intensity * 100.0f);
                
                /* Color mapping: 
                   - Hot (high intensity): Orange/Yellow
                   - Cooling (mid/low): Deep Red
                   - Coolest (bottom/random): Dim Red or Off */
                sk6812_color_t fire;
                fire.r = h;
                fire.g = (h > 35) ? (h - 35) : 0; // Greener (more orange) when hotter
                fire.b = (h > 85) ? (h - 85) : 0; // Touches of white at the very core
                
                sk6812_set(i, sk6812_scale(fire, 60));
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(50)); // Relaxed update rate (20 FPS)
            break;
        }

        case LED_MODE_VU: {
            /* Simple VU meter visualizing mic level into two 6-LED bars */
            static audio_sample_t samples[AUDIO_FFT_SIZE]; // Move off stack
            size_t n = audio_read_samples(samples);
            if (n > 0) {
                /* Calculate RMS level (root mean square) */
                int64_t sum_sq = 0;
                for (size_t i = 0; i < n; i++) {
                    int32_t s = (int32_t)samples[i];
                    sum_sq += (int64_t)s * s;
                }
                float rms = sqrtf((float)sum_sq / (float)n);
                
                /* DEBUG: Log the RMS value periodically (integer only to save stack) */
                static uint32_t last_log = 0;
                if (phase - last_log > 50) {
                    ESP_LOGI("VU", "RMS level: %d", (int)rms);
                    last_log = phase;
                }

                /* Map level to 0-6 LEDs. */
                int level = (int)((rms - 10.0f) / 100.0f);
                if (level < 0) level = 0;
                if (level > 6) level = 6;

                sk6812_clear();
                for (int i = 0; i < level; i++) {
                    sk6812_color_t color;
                    if (i < 3)      color = SK6812_GREEN;
                    else if (i < 5) color = (sk6812_color_t){140, 100, 0}; // Orange
                    else            color = SK6812_RED;
                    
                    sk6812_set(i, sk6812_scale(color, 80));
                    sk6812_set(i + 6, sk6812_scale(color, 80));
                }
                sk6812_show();
            }
            phase++;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        default:
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── Display task ────────────────────────────────────────────────────────── */
static void display_task(void *arg) {
    (void)arg;
    disp_cmd_t cmd;
    app_state_t last_state = APP_STATE_IDLE;

    /* Allow other tasks to initialize */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Initial draw: idle screen with nickname */
    idle_screen_draw(settings_get_nickname());

    while (1) {
        app_state_t state = (app_state_t)atomic_load(&g_app_state);

        if (state == APP_STATE_IDLE) {
            /* Idle mode: display nickname, respond slowly */
            if (last_state != APP_STATE_IDLE) {
                /* Transitioning to idle state - reset display state */
                idle_screen_reset();
                last_state = APP_STATE_IDLE;
            }
            /* Call draw every loop, but it will skip if time hasn't changed */
            idle_screen_draw(settings_get_nickname());
            vTaskDelay(pdMS_TO_TICKS(500));  /* Check every 500ms for time change */
        } else if (state == APP_STATE_MENU) {
            /* Menu mode: respond to queue messages */
            if (last_state != APP_STATE_MENU) {
                menu_draw(g_current_menu, true);
                last_state = APP_STATE_MENU;
            }
            if (xQueueReceive(g_disp_queue, &cmd, pdMS_TO_TICKS(30)) == pdTRUE) {
                bool full = (cmd.type == DISP_CMD_REDRAW_FULL);
                menu_draw(g_current_menu, full);
            }
            /* Queue receive already delays for 30ms if empty */
        } else if (state == APP_STATE_AUDIO_SPECTRUM) {
            /* Audio spectrum mode: continuous rendering */
            audio_spectrum_screen_draw(&g_audio_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_SETTINGS) {
            /* Settings mode: text input screen */
            text_input_draw(&g_text_input_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_UI_TEST) {
            /* UI test mode: continuous rendering, polls buttons internally */
            ui_test_screen_draw(&g_ui_test_screen);
            if (ui_test_screen_wants_exit(&g_ui_test_screen)) {
                ESP_LOGI(TAG, "Exiting UI test screen");
                ui_test_screen_clear();
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_SENSOR_READOUT) {
            /* Sensor readout mode: continuous rendering */
            sensor_readout_screen_draw(&g_sensor_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_SIGNAL_STRENGTH) {
            /* Signal strength mode: continuous rendering */
            signal_strength_screen_draw(&g_signal_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_WLAN_SPECTRUM) {
            /* WLAN spectrum mode: continuous rendering */
            wlan_spectrum_screen_draw(&g_wlan_spectrum_screen);
            vTaskDelay(pdMS_TO_TICKS(100));  /* 10 FPS - slower updates for WiFi scanning */
        } else if (state == APP_STATE_WLAN_LIST) {
            /* WLAN networks list mode: continuous rendering */
            wlan_list_screen_draw(&g_wlan_list_screen);
            vTaskDelay(pdMS_TO_TICKS(100));  /* 10 FPS */
        } else if (state == APP_STATE_ABOUT) {
            /* About screen: completely static redraw only once */
            if (last_state != APP_STATE_ABOUT) {
                about_screen_draw();
                last_state = APP_STATE_ABOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (state == APP_STATE_COLOR_SELECT || state == APP_STATE_TEXT_COLOR_SELECT) {
            /* Color select: respond to queue messages */
            if (last_state != state) {
                color_select_screen_draw(&g_color_screen);
                last_state = state;
            }
            if (xQueueReceive(g_disp_queue, &cmd, pdMS_TO_TICKS(30)) == pdTRUE) {
                color_select_screen_draw(&g_color_screen);
            }
        } else if (state == APP_STATE_HACKY_BIRD) {
            /* Hacky Bird game: continuous rendering */
            if (!g_hacky_bird_game_over) {
                /* Check if flap button is currently pressed */
                bool flap = buttons_is_pressed(BTN_A) || buttons_is_pressed(BTN_STICK);
                
                /* Update game state */
                hacky_bird_update(flap);
                
                /* Check if game ended */
                if (!hacky_bird_is_active()) {
                    g_hacky_bird_game_over = true;
                    
                    /* Draw game over screen */
                    uint16_t score = hacky_bird_get_score();
                    st7789_fill(0x5D1F);  // Sky blue
                    st7789_draw_string(SCREEN_WIDTH/2 - 40, SCREEN_HEIGHT/2 - 30, 
                                     "GAME OVER", 0xFFFF, 0x5D1F, 2);
                    
                    char score_str[32];
                    snprintf(score_str, sizeof(score_str), "Score: %d", score);
                    st7789_draw_string(SCREEN_WIDTH/2 - 40, SCREEN_HEIGHT/2, 
                                     score_str, 0xFFFF, 0x5D1F, 2);
                    
                    st7789_draw_string(SCREEN_WIDTH/2 - 70, SCREEN_HEIGHT/2 + 30, 
                                     "Press any key", 0xFFFF, 0x5D1F, 1);
                }
                
                /* Draw game state */
                if (!g_hacky_bird_game_over) {
                    hacky_bird_draw();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(16));  /* ~60 FPS for smooth gameplay */
        } else if (state == APP_STATE_SPACE_SHOOTER) {
            /* Space Shooter game: continuous rendering */
            /* Get button states */
            bool move_left = buttons_is_pressed(BTN_LEFT) || buttons_is_pressed(BTN_STICK);
            bool move_right = buttons_is_pressed(BTN_RIGHT);
            bool shoot = buttons_is_pressed(BTN_A);
            
            /* Update game state */
            space_shooter_update(move_left, move_right, shoot);
            
            /* Draw game state */
            space_shooter_draw();
            
            vTaskDelay(pdMS_TO_TICKS(16));  /* ~60 FPS for smooth gameplay */
        } else if (state == APP_STATE_SNAKE) {
            /* Snake game: variable speed based on game state */
            static uint32_t last_update = 0;
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t delay = snake_get_speed_delay();
            
            if (now - last_update >= delay) {
                /* Handle direction input */
                if (buttons_is_pressed(BTN_UP)) {
                    snake_set_direction(SNAKE_DIR_UP);
                } else if (buttons_is_pressed(BTN_DOWN)) {
                    snake_set_direction(SNAKE_DIR_DOWN);
                } else if (buttons_is_pressed(BTN_LEFT)) {
                    snake_set_direction(SNAKE_DIR_LEFT);
                } else if (buttons_is_pressed(BTN_RIGHT)) {
                    snake_set_direction(SNAKE_DIR_RIGHT);
                }
                
                /* Update game logic */
                snake_update();
                
                /* LED effect when food is eaten */
                if (snake_ate_food_this_frame()) {
                    // Flash green on all LEDs briefly
                    sk6812_color_t green = {0, 255, 0};
                    for (int i = 0; i < 10; i++) {
                        sk6812_set(i, green);
                    }
                    sk6812_show();
                    
                    // Clear LEDs after a brief moment (handled by next frame)
                    vTaskDelay(pdMS_TO_TICKS(50));
                    sk6812_clear();
                }
                
                /* Draw game state */
                snake_draw();
                
                last_update = now;
            }
            
            vTaskDelay(pdMS_TO_TICKS(16));  /* Check input at 60 FPS */
        } else if (state == APP_STATE_PYTHON_DEMO) {
            /* Python demo: rendering is done by python_demo_task, just wait */
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (state == APP_STATE_TIME_DATE_SET) {
            /* Time/date setting: redraw on request */
            if (s_td_needs_draw) {
                s_td_needs_draw = false;
                td_draw();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            /* Unknown state: fallback to idle */
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── Input task ──────────────────────────────────────────────────────────── */
static void input_task(void *arg) {
    (void)arg;
    btn_event_t ev;

    while (1) {
        if (xQueueReceive(g_btn_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        if (ev.type != BTN_PRESSED) continue;

        app_state_t state = (app_state_t)atomic_load(&g_app_state);

        if (state == APP_STATE_IDLE) {
            /* Idle mode: any button enters menu */
            ESP_LOGI(TAG, "Entering menu from idle");
            atomic_store(&g_app_state, APP_STATE_MENU);
            request_redraw(DISP_CMD_REDRAW_FULL);
        } else if (state == APP_STATE_AUDIO_SPECTRUM) {
            /* In audio spectrum mode: handle button actions */
            if (ev.id == BTN_B) {
                /* B button: toggle max hold */
                audio_spectrum_toggle_max_hold(&g_audio_screen);
            } else if (ev.id == BTN_SELECT || ev.id == BTN_A || ev.id == BTN_STICK ||
                ev.id == BTN_UP || ev.id == BTN_DOWN || ev.id == BTN_LEFT || 
                ev.id == BTN_RIGHT || ev.id == BTN_START) {
                /* Any other button: exit spectrum mode */
                ESP_LOGI(TAG, "Exiting audio spectrum");
                audio_spectrum_screen_exit();
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_SETTINGS) {
            /* Settings mode: text input button handling */
            text_input_handle_button(&g_text_input_screen, ev.id);
            
            /* If user pressed SELECT/A to confirm */
            if ((ev.id == BTN_A || ev.id == BTN_SELECT) && !text_input_is_editing(&g_text_input_screen)) {
                ESP_LOGI(TAG, "Settings confirmed: %s", text_input_get_text(&g_text_input_screen));
                settings_set_nickname(text_input_get_text(&g_text_input_screen));
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_UI_TEST) {
            /* UI test polls buttons internally via buttons_is_pressed().
             * All button events are intentionally ignored here so every
             * button can be tested without side-effects.
             * Exit is detected in the draw loop via wants_exit flag. */
        } else if (state == APP_STATE_SENSOR_READOUT) {
            /* Sensor readout mode: any button exits back to menu */
            if (ev.id == BTN_SELECT || ev.id == BTN_A || ev.id == BTN_STICK ||
                ev.id == BTN_UP || ev.id == BTN_DOWN || ev.id == BTN_LEFT || ev.id == BTN_RIGHT) {
                ESP_LOGI(TAG, "Exiting sensor readout");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_SIGNAL_STRENGTH) {
            /* Signal strength mode: any button exits back to menu */
            if (ev.id == BTN_SELECT || ev.id == BTN_A || ev.id == BTN_STICK ||
                ev.id == BTN_UP || ev.id == BTN_DOWN || ev.id == BTN_LEFT || ev.id == BTN_RIGHT) {
                ESP_LOGI(TAG, "Exiting signal strength display");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_WLAN_SPECTRUM) {
            /* WLAN spectrum mode: B exits */
            if (ev.id == BTN_B || ev.id == BTN_LEFT) {
                ESP_LOGI(TAG, "Exiting WLAN spectrum analyzer");
                wlan_spectrum_screen_exit();
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_WLAN_LIST) {
            /* WLAN list mode: UP/DOWN scroll, B exits */
            if (ev.id == BTN_UP || ev.id == BTN_DOWN) {
                wlan_list_screen_handle_button(&g_wlan_list_screen, ev.id);
            } else if (ev.id == BTN_B || ev.id == BTN_LEFT) {
                ESP_LOGI(TAG, "Exiting WLAN networks list");
                wlan_list_screen_exit();
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_ABOUT) {
            /* About screen mode: any button exits back to menu */
            if (ev.id == BTN_SELECT || ev.id == BTN_A || ev.id == BTN_STICK || ev.id == BTN_B ||
                ev.id == BTN_UP || ev.id == BTN_DOWN || ev.id == BTN_LEFT || ev.id == BTN_RIGHT) {
                ESP_LOGI(TAG, "Exiting about screen");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_COLOR_SELECT || state == APP_STATE_TEXT_COLOR_SELECT) {
            /* Color select: handle button actions */
            color_select_screen_handle_button(&g_color_screen, ev.id);
            
            /* If user pressed B (mapped to some back behavior) */
            if (ev.id == BTN_B || ev.id == BTN_LEFT) {
                ESP_LOGI(TAG, "Exiting color selector");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            } else if (color_select_screen_is_confirmed(&g_color_screen)) {
                /* Confirmed color selection */
                uint16_t color = color_select_screen_get_color(&g_color_screen);
                if (state == APP_STATE_COLOR_SELECT) {
                    ESP_LOGI(TAG, "Saving new accent color: 0x%04X", color);
                    settings_set_accent_color(color);
                } else {
                    ESP_LOGI(TAG, "Saving new text color: 0x%04X", color);
                    settings_set_text_color(color);
                }
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            } else {
                /* Update selection display */
                request_redraw(DISP_CMD_REDRAW_ITEM);
            }
        } else if (state == APP_STATE_HACKY_BIRD) {
            /* Hacky Bird game: handle button actions */
            if (g_hacky_bird_game_over) {
                /* Game over: any button exits back to menu */
                ESP_LOGI(TAG, "Exiting Hacky Bird (final score: %d)", hacky_bird_get_score());
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
                sk6812_clear();  // Turn off LEDs
            } else {
                /* Game active: A or STICK to flap, B to exit */
                if (ev.id == BTN_B) {
                    ESP_LOGI(TAG, "Exiting Hacky Bird (user quit)");
                    atomic_store(&g_app_state, APP_STATE_MENU);
                    request_redraw(DISP_CMD_REDRAW_FULL);
                    sk6812_clear();  // Turn off LEDs
                }
                /* Flap will be handled in display_task on each frame */
            }
        } else if (state == APP_STATE_SPACE_SHOOTER) {
            /* Space Shooter game: B to exit */
            if (ev.id == BTN_B) {
                ESP_LOGI(TAG, "Exiting Space Shooter (final score: %lu)", space_shooter_get_score());
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
            /* Movement and shooting are handled continuously in display_task */
        } else if (state == APP_STATE_SNAKE) {
            /* Snake game: B to exit */
            if (ev.id == BTN_B) {
                ESP_LOGI(TAG, "Exiting Snake (final score: %lu)", snake_get_score());
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
            /* Direction input is handled continuously in display_task */
        } else if (state == APP_STATE_PYTHON_DEMO) {
            /* Python demo handles its own input via polling – ignore queue events */
        } else if (state == APP_STATE_TIME_DATE_SET) {
            /* Time/date setting: UP/DOWN adjust field, LEFT/RIGHT move cursor, A confirm, B cancel */
            if (ev.id == BTN_UP) {
                s_td_fields[s_td_cursor]++;
                td_clamp();
                s_td_needs_draw = true;
            } else if (ev.id == BTN_DOWN) {
                s_td_fields[s_td_cursor]--;
                td_clamp();
                s_td_needs_draw = true;
            } else if (ev.id == BTN_LEFT) {
                s_td_cursor = (s_td_cursor - 1 + TD_NUM_FIELDS) % TD_NUM_FIELDS;
                s_td_needs_draw = true;
            } else if (ev.id == BTN_RIGHT) {
                s_td_cursor = (s_td_cursor + 1) % TD_NUM_FIELDS;
                s_td_needs_draw = true;
            } else if (ev.id == BTN_A || ev.id == BTN_START) {
                /* Apply the new time */
                struct tm new_time = {0};
                new_time.tm_hour = s_td_fields[TD_FIELD_HOUR];
                new_time.tm_min  = s_td_fields[TD_FIELD_MIN];
                new_time.tm_sec  = 0;
                new_time.tm_year = s_td_fields[TD_FIELD_YEAR] - 1900;
                new_time.tm_mon  = s_td_fields[TD_FIELD_MON] - 1;
                new_time.tm_mday = s_td_fields[TD_FIELD_DAY];
                new_time.tm_isdst = -1;
                time_t t = mktime(&new_time);
                struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                ESP_LOGI(TAG, "System time set to %04d-%02d-%02d %02d:%02d",
                         s_td_fields[TD_FIELD_YEAR], s_td_fields[TD_FIELD_MON],
                         s_td_fields[TD_FIELD_DAY], s_td_fields[TD_FIELD_HOUR],
                         s_td_fields[TD_FIELD_MIN]);
                /* Reset idle screen so it picks up the new time */
                idle_screen_reset();
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            } else if (ev.id == BTN_B) {
                ESP_LOGI(TAG, "Time/date setting cancelled");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else {
            /* Menu mode */
            switch (ev.id) {
            case BTN_UP:
                menu_navigate_up(g_current_menu);
                request_redraw(DISP_CMD_REDRAW_ITEM);
                break;

            case BTN_DOWN:
                menu_navigate_down(g_current_menu);
                request_redraw(DISP_CMD_REDRAW_ITEM);
                break;

            case BTN_LEFT:
                if (g_current_menu->grid_mode) {
                    /* Grid mode: navigate left within the grid */
                    menu_navigate_left(g_current_menu);
                    request_redraw(DISP_CMD_REDRAW_ITEM);
                } else {
                    /* List mode: back to parent or idle */
                    if (menu_back(&g_current_menu)) {
                        ESP_LOGI(TAG, "Navigated back to parent menu");
                        request_redraw(DISP_CMD_REDRAW_FULL);
                    } else {
                        ESP_LOGI(TAG, "Exiting menu to idle screen");
                        atomic_store(&g_app_state, APP_STATE_IDLE);
                        request_redraw(DISP_CMD_REDRAW_FULL);
                    }
                }
                break;

            case BTN_RIGHT:
                if (g_current_menu->grid_mode) {
                    /* Grid mode: navigate right within the grid */
                    menu_navigate_right(g_current_menu);
                    request_redraw(DISP_CMD_REDRAW_ITEM);
                }
                /* List mode: no action for RIGHT */
                break;

            case BTN_B:
                /* B always goes back / exits to idle */
                if (menu_back(&g_current_menu)) {
                    ESP_LOGI(TAG, "Navigated back to parent menu");
                    request_redraw(DISP_CMD_REDRAW_FULL);
                } else {
                    ESP_LOGI(TAG, "Exiting menu to idle screen");
                    atomic_store(&g_app_state, APP_STATE_IDLE);
                    request_redraw(DISP_CMD_REDRAW_FULL);
                }
                break;

            case BTN_A:
            case BTN_STICK:    /* joystick press also activates */
            case BTN_SELECT:
                /* Check for submenu first */
                if (menu_enter_submenu(&g_current_menu)) {
                    ESP_LOGI(TAG, "Entered submenu");
                    request_redraw(DISP_CMD_REDRAW_FULL);
                } else {
                    /* No submenu, activate action */
                    menu_select(g_current_menu);
                    request_redraw(DISP_CMD_REDRAW_ITEM);
                }
                break;

            default:
                break;
            }
        }
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */
void app_main(void) {
    ESP_LOGI(TAG, "Disobey Badge 2025/26 – FreeRTOS firmware");

    /* ── Queues ── */
    g_btn_queue  = xQueueCreate(BTN_QUEUE_LEN,  sizeof(btn_event_t));
    g_disp_queue = xQueueCreate(DISP_QUEUE_LEN, sizeof(disp_cmd_t));
    configASSERT(g_btn_queue && g_disp_queue);

    /* ── Driver init ── */
    st7789_init();
    sk6812_init();
    audio_init();       /* Microphone driver init */
    buttons_init(g_btn_queue);
    settings_init();

    /* ── WiFi subsystem init (needed for spectrum/list screens) ── */
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "WiFi STA started (ready for scanning)");
    }

    /* ── Filesystem init for Python apps ── */
    esp_err_t fs_ret = pyapps_fs_init();
    if (fs_ret == ESP_OK) {
        ESP_LOGI(TAG, "Python apps filesystem mounted successfully");
    } else {
        ESP_LOGW(TAG, "Failed to mount Python apps filesystem: %s", esp_err_to_name(fs_ret));
    }

    /* ── MicroPython runner init (bridge only, no background task) ── */
    esp_err_t mp_ret = micropython_runner_init();
    if (mp_ret == ESP_OK) {
        ESP_LOGI(TAG, "MicroPython runner initialized (on-demand mode)");
    } else {
        ESP_LOGW(TAG, "Failed to initialize MicroPython runner: %s", esp_err_to_name(mp_ret));
    }
    
    /* ── Build menus with icons and submenus ── */
    
    /* Diagnostics submenu */
    menu_init(&g_diag_menu, "Diagnostics");
    menu_add_item(&g_diag_menu, 'T', NULL, "UI Test", action_ui_test, NULL);
    menu_add_item(&g_diag_menu, 'S', NULL, "Sensor Readout", action_sensor_readout, NULL);
    menu_add_item(&g_diag_menu, 'V', NULL, "Signal Strength", action_signal_strength, NULL);
    menu_add_item(&g_diag_menu, 'Z', NULL, "WiFi Spectrum", action_wlan_spectrum, NULL);
    menu_add_item(&g_diag_menu, 'N', NULL, "WiFi Networks", action_wlan_list, NULL);

    /* Tools submenu */
    menu_init(&g_tools_menu, "Tools");
    menu_add_item(&g_tools_menu, '@', NULL, "Audio Spectrum", action_audio_spectrum, NULL);
    
    /* Games submenu */
    menu_init(&g_games_menu, "Games");
    menu_add_item(&g_games_menu, 'H', NULL, "Hacky Bird", action_hacky_bird, NULL);
    menu_add_item(&g_games_menu, 'S', NULL, "Space Shooter", action_space_shooter, NULL);
    menu_add_item(&g_games_menu, 'N', NULL, "Snake", action_snake, NULL);
    
    /* LED Animation submenu */
    menu_init(&g_led_menu, "LED Animation");
    menu_add_item(&g_led_menu, 'b', NULL, "Accent Pulse", action_led_accent, NULL);
    menu_add_item(&g_led_menu, 'r', NULL, "Rainbow", action_led_rainbow, NULL);
    menu_add_item(&g_led_menu, 'd', NULL, "Disco Party", action_led_disco, NULL);
    menu_add_item(&g_led_menu, 'p', NULL, "Police Strobe", action_led_police, NULL);
    menu_add_item(&g_led_menu, 's', NULL, "Smooth Relax", action_led_relax, NULL);
    menu_add_item(&g_led_menu, 'o', NULL, "Smooth Rotate", action_led_rotate, NULL);
    menu_add_item(&g_led_menu, 'c', NULL, "LED Chase", action_led_chase, NULL);
    menu_add_item(&g_led_menu, 'm', NULL, "Color Morph", action_led_morph, NULL);
    menu_add_item(&g_led_menu, 'y', NULL, "Breath Cycle", action_led_breath_cyc, NULL);
    menu_add_item(&g_led_menu, 'i', NULL, "Disobey Identity", action_led_identity, NULL);
    menu_add_item(&g_led_menu, 'f', NULL, "Flame", action_led_flame, NULL);
    menu_add_item(&g_led_menu, 'v', NULL, "VU meter mode (MIC ON!)", action_led_vu, NULL);
    menu_add_item(&g_led_menu, 'x', NULL, "Off", action_led_off, NULL);

    /* Settings submenu */
    menu_init(&g_settings_menu, "Settings");
    menu_add_item(&g_settings_menu, 'n', NULL, "Edit Nickname", action_settings, NULL);
    menu_add_item(&g_settings_menu, 'c', NULL, "Accent Color", action_color_select, NULL);
    menu_add_item(&g_settings_menu, 't', NULL, "Text Color", action_text_color_select, NULL);
    menu_add_item(&g_settings_menu, 'L', NULL, "LED Animation", NULL, &g_led_menu);
    menu_add_item(&g_settings_menu, 'T', NULL, "Set Time & Date", action_time_date_set, NULL);

    /* Development submenu */
    menu_init(&g_dev_menu, "Development");
    menu_add_item(&g_dev_menu, 'P', NULL, "Python Demo", action_python_demo, NULL);

    /* Main menu — icon grid mode */
    menu_init(&g_menu, TITLE_STR);
    g_menu.grid_mode = true;
    menu_add_item(&g_menu, '#', ICON_TOOLS,       "Tools",       NULL, &g_tools_menu);
    menu_add_item(&g_menu, 'G', ICON_GAMES,       "Games",       NULL, &g_games_menu);
    menu_add_item(&g_menu, 'O', ICON_SETTINGS,    "Settings",    NULL, &g_settings_menu);
    menu_add_item(&g_menu, 'D', ICON_DIAGNOSTICS, "Diagnostics", NULL, &g_diag_menu);
    menu_add_item(&g_menu, 'X', ICON_DEVELOPMENT, "Development", NULL, &g_dev_menu);
    menu_add_item(&g_menu, '?', ICON_ABOUT,       "About",       action_about, NULL);

    g_current_menu = &g_menu;    /* ── Tasks (all on CPU0; CPU1 reserved for MicroPython) ── */
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 5, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(input_task,   "input",   2048, NULL, 6, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(led_task,     "led",     4096, NULL, 4, NULL, PRO_CPU_NUM);

    ESP_LOGI(TAG, "All tasks launched. UP/DOWN to navigate, A/STICK/SELECT to activate.");
}
