// MIT License
//
// Copyright (c) 2026 Kevin Thomas
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Author:  Kevin Thomas
// Email:   kevin@mytechnotalent.com
// GitHub:  https://github.com/mytechnotalent/picokit-37-reaction-timer
// File:    monitor.c
// Desc:    Implements the reaction timer game that lights an LED after a
//          random delay and shows the button reaction time on the LCD.
// Created: 2026

#include "picokit_37_reaction_timer.h"
#include "monitor.h"
#include "button.h"
#include "display.h"
#include "radio.h"
#include "status_led.h"
#include "crypto_aead.h"
#include "crypto_kdf.h"
#include "envelope.h"
#include "field_secrets.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/rand.h"
#include "pico/time.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Module-ready flag.
 *
 * Set to true by monitor_init() once the peripherals are configured.
 * monitor_step() returns false while this flag is clear.
 */
static bool g_ready;

/**
 * @brief True while waiting for the random arm delay to elapse.
 */
static bool g_armed;

/**
 * @brief Most recent reaction time in milliseconds.
 */
static uint16_t g_millis;

/**
 * @brief Absolute time in microseconds when the reaction LED lit.
 */
static uint64_t g_lit_us;

/**
 * @brief Monotonic transmit sequence number.
 */
static uint16_t g_seq;

/**
 * @brief Absolute time in microseconds of the next reaction event.
 */
static uint64_t g_next_event_us;

/**
 * @brief Absolute time in microseconds of the next authenticated transmit.
 */
static uint64_t g_next_tx_us;

/**
 * @brief Inbound radio line accumulator.
 */
static char g_rx_line[RADIO_LINE_BUF_LEN];

/**
 * @brief Number of bytes currently held in the inbound line accumulator.
 */
static size_t g_rx_len;

/**
 * @brief Derived XChaCha20-Poly1305 session key for telemetry.
 */
static uint8_t g_key[CRYPTO_AEAD_KEY_LEN];

/**
 * @brief True once the telemetry session key has been derived.
 */
static bool g_key_ready;

/**
 * @brief Probe one I2C address and report whether it acknowledges.
 *
 * @param i2c Pointer to the I2C peripheral to probe.
 * @param addr The 7-bit address to probe.
 * @return bool true when the address acknowledged.
 */
static bool i2c_probe(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t dummy = 0u;
    if (i2c_write_blocking(i2c, addr, &dummy, 1u, false) < 0) {
        return false;
    }
    printf("  found 0x%02X\n", (unsigned)addr);
    return true;
}

/**
 * @brief Probe the I2C bus and print every device that acknowledges.
 *
 * @param i2c Pointer to the I2C peripheral to scan.
 * @return void
 */
static void i2c_bus_scan(i2c_inst_t *i2c) {
    uint8_t addr;
    uint8_t found = 0u;
    printf("I2C scan:\n");
    for (addr = 0x08u; addr < 0x78u; ++addr) {
        found += i2c_probe(i2c, addr) ? 1u : 0u;
    }
    if (found == 0u) {
        printf("  no devices\n");
    }
}

/**
 * @brief Initialize the I2C bus pins and scan the bus.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_bus_init(void) {
    i2c_init(PICOKIT_37_REACTION_TIMER_I2C, PICOKIT_37_REACTION_TIMER_I2C_BAUD);
    gpio_set_function(PICOKIT_37_REACTION_TIMER_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PICOKIT_37_REACTION_TIMER_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PICOKIT_37_REACTION_TIMER_I2C_SDA);
    gpio_pull_up(PICOKIT_37_REACTION_TIMER_I2C_SCL);
    i2c_bus_scan(PICOKIT_37_REACTION_TIMER_I2C);
}

/**
 * @brief Configure the onboard heartbeat LED as a dark output.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_state_init_io(void) {
    gpio_init(PICOKIT_37_REACTION_TIMER_LED_PIN);
    gpio_set_dir(PICOKIT_37_REACTION_TIMER_LED_PIN, GPIO_OUT);
    gpio_put(PICOKIT_37_REACTION_TIMER_LED_PIN, 0);
}

/**
 * @brief Draw one random arm delay in microseconds.
 *
 * @param void No parameters.
 * @return uint64_t Random arm delay in microseconds.
 */
static uint64_t monitor_random_delay_us(void) {
    uint32_t span = (uint32_t)MONITOR_ARM_SPAN_MS;
    uint32_t offset = get_rand_32() % span;
    return ((uint64_t)MONITOR_ARM_MIN_MS + offset) * 1000u;
}

/**
 * @brief Arm the next round and schedule the random light delay.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_arm(uint64_t now_us) {
    g_armed = true;
    gpio_put(PICOKIT_37_REACTION_TIMER_LED_PIN, 0);
    g_next_event_us = now_us + monitor_random_delay_us();
}

/**
 * @brief Reset the round, sequence, and transmit timing, then arm.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_state_init(void) {
    uint64_t now_us = time_us_64();
    g_millis = 0u;
    g_seq = 0u;
    g_next_tx_us = now_us + (uint64_t)PICOKIT_37_REACTION_TIMER_TX_INTERVAL_MS * 1000u;
    g_ready = true;
    monitor_arm(now_us);
}

/**
 * @brief Derive the telemetry session key from the field secret.
 *
 * LAB-ONLY: production must provision the session key through OTP rather
 * than deriving it from a committed passphrase and salt.
 *
 * @param void No parameters.
 * @return bool true when the session key was derived.
 */
static bool monitor_derive_key(void) {
    bool ok = crypto_kdf_argon2id((const uint8_t *)FIELD_SECRET_PASSPHRASE, strlen(FIELD_SECRET_PASSPHRASE), FIELD_SECRET_SALT, 16u, g_key);
    g_key_ready = ok;
    return ok;
}

/**
 * @brief Print the boot banner for the reaction timer lesson.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_banner(void) {
    printf("=== PICOKIT-37 REACTION TIMER // RANDOM LIGHT + BUTTON TIME ===\n");
}

/**
 * @brief Derive the field key and announce a ready monitor.
 *
 * @param void No parameters.
 * @return bool true when the field key was derived and installed.
 */
static bool monitor_finish(void) {
    bool ok = monitor_derive_key();
    if (ok) {
        monitor_banner();
    }
    return ok;
}

/**
 * @brief Blink the onboard heartbeat LED exactly once.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_heartbeat(void) {
    gpio_put(PICOKIT_37_REACTION_TIMER_LED_PIN, 1);
    sleep_us(MONITOR_HEARTBEAT_BLINK_US);
    gpio_put(PICOKIT_37_REACTION_TIMER_LED_PIN, 0);
    sleep_us(MONITOR_HEARTBEAT_BLINK_US);
}

/**
 * @brief Show the press prompt on the 1602 LCD.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_show_ready(void) {
    display_show_lines(PICOKIT_37_REACTION_TIMER_I2C, PICOKIT_37_REACTION_TIMER_LCD_ADDR, "PICOKIT-37 TIMER", "PRESS BUTTON");
}

/**
 * @brief Show the last reaction time on the 1602 LCD.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_show_result(void) {
    char line2[DISPLAY_LINE_LEN];
    snprintf(line2, sizeof(line2), "REACTION %u ms", (unsigned)g_millis);
    display_show_lines(PICOKIT_37_REACTION_TIMER_I2C, PICOKIT_37_REACTION_TIMER_LCD_ADDR, "PICOKIT-37 TIMER", line2);
}

/**
 * @brief Light the reaction LED and open the press window.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_light(uint64_t now_us) {
    g_armed = false;
    g_lit_us = now_us;
    gpio_put(PICOKIT_37_REACTION_TIMER_LED_PIN, 1);
    monitor_show_ready();
    g_next_event_us = now_us + (uint64_t)MONITOR_PRESS_TIMEOUT_MS * 1000u;
}

/**
 * @brief Record the reaction time and arm the next round.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_reaction(uint64_t now_us) {
    g_millis = (uint16_t)((now_us - g_lit_us) / 1000u);
    monitor_show_result();
    monitor_arm(now_us);
}

/**
 * @brief Record a missed round and arm the next round.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_miss(uint64_t now_us) {
    g_millis = MONITOR_PRESS_TIMEOUT_MS;
    monitor_show_result();
    monitor_arm(now_us);
}

/**
 * @brief Service the armed or lit reaction event.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_event(uint64_t now_us) {
    if (g_armed) {
        monitor_light(now_us);
    } else {
        monitor_miss(now_us);
    }
}

/**
 * @brief Consume a press during the open press window.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_poll(uint64_t now_us) {
    if (!g_armed && button_consume_press()) {
        monitor_reaction(now_us);
    }
}

/**
 * @brief Format the heartbeat JSON body for the reaction time.
 *
 * @param frame Pointer to the mutable frame output buffer.
 * @param frame_len Capacity of the frame output buffer in bytes.
 * @return size_t Number of JSON bytes written, or zero on overflow.
 */
static size_t monitor_build_frame(char *frame, size_t frame_len) {
    int written = snprintf(frame, frame_len, "{\"n\":%u,\"s\":%u,\"m\":%u}", (unsigned)PACKET_NODE_ID, (unsigned)g_seq, (unsigned)g_millis);
    return (written > 0 && (size_t)written < frame_len) ? (size_t)written : 0u;
}

/**
 * @brief Seal the current heartbeat body into a hex envelope.
 *
 * @param hex Pointer to the NUL-terminated hex output buffer.
 * @param hex_len Capacity of the hex output buffer in bytes.
 * @return bool true when the heartbeat was sealed and encoded.
 */
static bool monitor_seal_frame(char *hex, size_t hex_len) {
    char frame[PICOKIT_37_REACTION_TIMER_FRAME_SIZE];
    uint8_t nonce[ENVELOPE_NONCE_LEN];
    uint8_t ad = (uint8_t)PACKET_NODE_ID;
    size_t frame_len = monitor_build_frame(frame, sizeof(frame));
    envelope_fill_nonce(nonce);
    return envelope_seal_hex(g_key, nonce, &ad, 1u, (const uint8_t *)frame, frame_len, hex, hex_len);
}

/**
 * @brief Build and transmit the authenticated heartbeat frame.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_transmit(void) {
    char hex[ENVELOPE_MAX_HEX_LEN];
    if (!g_key_ready) {
        return;
    }
    if (monitor_seal_frame(hex, sizeof(hex))) {
        radio_send_frame(PICOKIT_37_REACTION_TIMER_UART, (const uint8_t *)hex, strlen(hex));
        g_seq += 1u;
    }
}

/**
 * @brief Print one console line for the current heartbeat transmit.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_log_tx(void) {
    printf("REACTION %u ms seq=%u\n", (unsigned)g_millis, (unsigned)g_seq);
}

/**
 * @brief Transmit one heartbeat and schedule the next transmit.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_tx_tick(uint64_t now_us) {
    monitor_transmit();
    monitor_heartbeat();
    monitor_log_tx();
    g_next_tx_us = now_us + (uint64_t)PICOKIT_37_REACTION_TIMER_TX_INTERVAL_MS * 1000u;
}

/**
 * @brief Drain inbound radio lines and log every valid +RCV report.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_rx_tick(void) {
    radio_rcv_t rcv;
    while (radio_line_pump(PICOKIT_37_REACTION_TIMER_UART, g_rx_line, &g_rx_len)) {
        if (radio_parse_rcv(g_rx_line, &rcv) == RADIO_RESULT_OK) {
            printf("RX from 0x%04X, %u bytes\n", (unsigned)rcv.sender, (unsigned)rcv.len);
        }
    }
}

/**
 * @brief Service the reaction event and heartbeat transmit timers.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_service_timers(uint64_t now_us) {
    if (now_us >= g_next_event_us) {
        monitor_event(now_us);
    }
    if (now_us >= g_next_tx_us) {
        monitor_tx_tick(now_us);
    }
}

bool monitor_init(void) {
    bool ok;
    monitor_bus_init();
    ok = status_led_init() && button_init() && radio_init(PICOKIT_37_REACTION_TIMER_UART);
    monitor_state_init_io();
    monitor_state_init();
    return ok && display_init(PICOKIT_37_REACTION_TIMER_I2C, PICOKIT_37_REACTION_TIMER_LCD_ADDR) && monitor_finish();
}

void monitor_deinit(void) {
    g_ready = false;
}

bool monitor_step(void) {
    uint64_t now_us;
    if (!g_ready) {
        return false;
    }
    now_us = time_us_64();
    monitor_poll(now_us);
    monitor_service_timers(now_us);
    monitor_rx_tick();
    return true;
}
