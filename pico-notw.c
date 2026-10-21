#include <stdio.h>
#include <stdlib.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "oled.h"
#include "pico/stdlib.h"

#define PWM_PIN     16
#define JOY_VRX_PIN 26
#define JOY_VRY_PIN 27
#define JOY_SW_PIN  15
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5

#define SAMPLES  32
#define DEADZONE 80
#define TICK_MS  10

#define ENGAGE           40
#define RELEASE          25
#define FIRST_REPEAT_MS  400
#define CURSOR_REPEAT_MS 220
#define VALUE_SLOW_MS    300
#define VALUE_FAST_MS    30

#define DEBOUNCE_MS   30
#define LONG_PRESS_MS 800

#define FREQ_MIN 8
#define FREQ_MAX 1000000
#define DUTY_MIN 1
#define DUTY_MAX 99

enum { HOLD_NONE, HOLD_X, HOLD_Y };

static const int steps[] = {1000000, 100000, 10000, 1000, 100, 10, 1, 10, 1};

static int freq;
static int duty;
static int cursor;
static bool output_on;
static bool button_down;

static bool oled_ok;
static uint slice;
static uint chan;

static int read_avg(uint pin) {
    adc_select_input(pin - 26);
    int sum = 0;
    for (int i = 0; i < SAMPLES; i++) sum += adc_read();
    return sum / SAMPLES;
}

static int scale(int raw, int center) {
    int d = raw - center;
    if (d >= DEADZONE) return (d - DEADZONE) * 100 / (4095 - center - DEADZONE);
    if (d <= -DEADZONE) return (d + DEADZONE) * 100 / (center - DEADZONE);
    return 0;
}

static int limit(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void reset(void) {
    freq = 1000;
    duty = 50;
    cursor = 3;
    output_on = true;
}

static float pwm_apply(void) {
    uint64_t clk16 = (uint64_t)clock_get_hz(clk_sys) * 16;
    uint64_t div16 = (clk16 + (uint64_t)freq * 65536 - 1) / ((uint64_t)freq * 65536);
    if (div16 < 16) div16 = 16;
    uint32_t top = (clk16 + div16 * freq / 2) / (div16 * freq);

    pwm_set_clkdiv_int_frac(slice, div16 / 16, div16 % 16);
    pwm_set_wrap(slice, top - 1);
    pwm_set_chan_level(slice, chan, output_on ? top * duty / 100 : 0);
    return (float)clk16 / (div16 * top);
}

static void draw_line(int row, const char *text, int cursor_col) {
    int cw = OLED_CHAR_WIDTH * 2;
    int y = row * 16;
    oled_text(0, y, text, 2);
    if (cursor_col < 0) return;
    if (text[cursor_col] == ' ') oled_char(cursor_col * cw, y, '0', 2);
    oled_invert(cursor_col * cw - 1, y, cw, 16);
}

static void render(float real_hz) {
    printf("Freq: %7d Hz  Duty: %2d %%  %-3s  (skutecne %.2f Hz)\n",
           freq, duty, output_on ? "ON" : "OFF", real_hz);

    if (!oled_ok) return;
    char text[16];
    oled_clear();
    snprintf(text, sizeof(text), "%7d Hz", freq);
    draw_line(0, text, cursor < 7 ? cursor : -1);
    snprintf(text, sizeof(text), "Duty %2d%%", duty);
    draw_line(1, text, cursor < 7 ? -1 : cursor - 2);
    oled_text(OLED_WIDTH - 17, 20, output_on ? " ON" : "OFF", 1);
    if (!output_on) oled_invert(OLED_WIDTH - 19, 18, 19, 11);
    oled_show();
}

static int value_repeat_ms(int v) {
    int t = (v - ENGAGE) * 100 / (100 - ENGAGE);
    if (t < 0) t = 0;
    return VALUE_SLOW_MS - (VALUE_SLOW_MS - VALUE_FAST_MS) * t * t / 10000;
}

static bool joystick_update(int x, int y, uint32_t now) {
    static int hold = HOLD_NONE;
    static int dir;
    static uint32_t last_ms;
    static uint32_t wait_ms;

    if (hold == HOLD_NONE) {
        if (abs(x) < ENGAGE && abs(y) < ENGAGE) return false;
        hold = abs(x) > abs(y) ? HOLD_X : HOLD_Y;
        dir = (hold == HOLD_X ? x : y) > 0 ? 1 : -1;
        wait_ms = FIRST_REPEAT_MS;
    } else {
        int v = hold == HOLD_X ? x : y;
        if (v * dir < RELEASE) {
            hold = HOLD_NONE;
            return false;
        }
        if (now - last_ms < wait_ms) return false;
        wait_ms = hold == HOLD_X ? CURSOR_REPEAT_MS : value_repeat_ms(abs(v));
    }
    last_ms = now;

    if (hold == HOLD_X) cursor = limit(cursor + dir, 0, 8);
    else if (cursor < 7) freq = limit(freq + dir * steps[cursor], FREQ_MIN, FREQ_MAX);
    else duty = limit(duty + dir * steps[cursor], DUTY_MIN, DUTY_MAX);
    return true;
}

static bool button_update(bool pressed, uint32_t now) {
    static bool last;
    static bool long_done;
    static uint32_t changed_ms;
    static uint32_t down_ms;

    if (pressed != last) {
        last = pressed;
        changed_ms = now;
    }
    if (pressed != button_down && now - changed_ms >= DEBOUNCE_MS) {
        button_down = pressed;
        if (pressed) {
            down_ms = now;
            long_done = false;
            return false;
        }
        if (long_done) return false;
        output_on = !output_on;
        return true;
    }
    if (button_down && !long_done && now - down_ms >= LONG_PRESS_MS) {
        long_done = true;
        reset();
        return true;
    }
    return false;
}

int main(void) {
    stdio_init_all();
    sleep_ms(500);

    oled_ok = oled_init(i2c0, I2C_SDA_PIN, I2C_SCL_PIN);
    if (!oled_ok) printf("OLED nenalezen, jen vypis na seriovou linku\n");

    adc_init();
    adc_gpio_init(JOY_VRX_PIN);
    adc_gpio_init(JOY_VRY_PIN);

    gpio_init(JOY_SW_PIN);
    gpio_set_dir(JOY_SW_PIN, GPIO_IN);
    gpio_pull_up(JOY_SW_PIN);

    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);
    slice = pwm_gpio_to_slice_num(PWM_PIN);
    chan = pwm_gpio_to_channel(PWM_PIN);

    int cx = 0, cy = 0;
    for (int i = 0; i < 8; i++) {
        cx += read_avg(JOY_VRX_PIN);
        cy += read_avg(JOY_VRY_PIN);
    }
    cx /= 8;
    cy /= 8;

    reset();
    render(pwm_apply());
    pwm_set_enabled(slice, true);

    while (true) {
        int x = scale(read_avg(JOY_VRX_PIN), cx);
        int y = scale(read_avg(JOY_VRY_PIN), cy);
        bool pressed = !gpio_get(JOY_SW_PIN);
        uint32_t now = to_ms_since_boot(get_absolute_time());

        bool changed = button_update(pressed, now);
        if (pressed || button_down) x = y = 0;
        changed |= joystick_update(x, y, now);

        if (changed) render(pwm_apply());
        sleep_ms(TICK_MS);
    }
}
