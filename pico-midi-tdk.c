#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "tusb.h"

// =============================
// MIDI settings
// =============================
#define NOTE_NUM        60
#define ON_VELOCITY     127
#define OFF_VELOCITY    0

// =============================
// ADC settings
// =============================
#define ADC_RISE_PIN    26   // GPIO26 = ADC0
#define ADC_FALL_PIN    27   // GPIO27 = ADC1
#define ADC_RISE_INPUT  0    // ADC0
#define ADC_FALL_INPUT  1    // ADC1

// =============================
// UART settings (to Arduino Due)
// =============================
#define UART_ID         uart1
#define UART_BAUD_RATE  115200
#define UART_TX_PIN     4    // Pico GP4
#define UART_RX_PIN     5    // Pico GP5

// =============================
// Thresholds (initial values)
// 条件として必要なのは
// S2 < S3
// S1 < S4
// =============================
static int threshold_s1 = 150;
static int threshold_s2 = 200;
static int threshold_s3 = 550;
static int threshold_s4 = 300;

// =============================
// Cancel hysteresis
// S2付近 / S4付近の揺れで連発しないため
// =============================
#define ON_CANCEL_HYS   20
#define OFF_CANCEL_HYS  20

// =============================
// State machine
// =============================
typedef enum {
    STATE_IDLE_OFF = 0,
    STATE_ON_MEASURING,
    STATE_IDLE_ON,
    STATE_OFF_MEASURING
} note_state_t;

static note_state_t note_state = STATE_IDLE_OFF;

// =============================
// Timing
// =============================
static uint64_t on_start_us = 0;
static uint64_t off_start_us = 0;

// =============================
// Helpers
// =============================
static void uart_setup(void)
{
    uart_init(UART_ID, UART_BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
}

static void send_uart_line(const char *msg)
{
    uart_puts(UART_ID, msg);
    uart_puts(UART_ID, "\n");
}

static void send_uart_time(const char *prefix, uint64_t elapsed_us)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s,%llu", prefix, (unsigned long long)elapsed_us);
    send_uart_line(buf);
}

static void send_note_on(uint8_t note, uint8_t velocity)
{
    uint8_t packet[4];
    packet[0] = 0x09;   // CIN: Note On
    packet[1] = 0x90;   // Note On, CH1
    packet[2] = note;
    packet[3] = velocity;
    tud_midi_stream_write(0, packet, 4);
}

static void send_note_off(uint8_t note, uint8_t velocity)
{
    uint8_t packet[4];
    packet[0] = 0x08;   // CIN: Note Off
    packet[1] = 0x80;   // Note Off, CH1
    packet[2] = note;
    packet[3] = velocity;
    tud_midi_stream_write(0, packet, 4);
}

// HELLO -> OK
// S1=...,S2=...,S3=...,S4=... -> ACK
// 範囲条件不正 -> ERR_RANGE
// 書式不正 -> ERR
static void process_uart_message(void)
{
    static char rx_buf[96];
    static int rx_idx = 0;

    while (uart_is_readable(UART_ID))
    {
        char c = uart_getc(UART_ID);

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            rx_buf[rx_idx] = '\0';

            if (strcmp(rx_buf, "HELLO") == 0) {
                send_uart_line("OK");
            } else {
                int s1, s2, s3, s4;
                int matched = sscanf(rx_buf, "S1=%d,S2=%d,S3=%d,S4=%d",
                                     &s1, &s2, &s3, &s4);

                if (matched == 4) {
                    if ((s2 < s3) && (s1 < s4)) {
                        threshold_s1 = s1;
                        threshold_s2 = s2;
                        threshold_s3 = s3;
                        threshold_s4 = s4;
                        send_uart_line("ACK");
                    } else {
                        send_uart_line("ERR_RANGE");
                    }
                } else {
                    send_uart_line("ERR");
                }
            }

            rx_idx = 0;
        } else {
            if (rx_idx < (int)(sizeof(rx_buf) - 1)) {
                rx_buf[rx_idx++] = c;
            } else {
                rx_idx = 0;
            }
        }
    }
}

int main(void)
{
    // ADC init
    adc_init();
    adc_gpio_init(ADC_RISE_PIN);
    adc_gpio_init(ADC_FALL_PIN);

    // UART init
    uart_setup();

    // TinyUSB init
    tusb_init();

    bool prev_valid = false;
    int16_t prev_diff = 0;

    while (true)
    {
        tud_task();
        process_uart_message();

        // ADC read
        adc_select_input(ADC_RISE_INPUT);
        uint16_t adc_rise = adc_read();

        adc_select_input(ADC_FALL_INPUT);
        uint16_t adc_fall = adc_read();

        int16_t diff = (int16_t)adc_rise - (int16_t)adc_fall;
        uint64_t now_us = time_us_64();

        if (!prev_valid) {
            prev_diff = diff;
            prev_valid = true;
            sleep_ms(1);
            continue;
        }

        switch (note_state)
        {
            case STATE_IDLE_OFF:
                // On計測開始: S2 を下から上へ跨ぐ
                if ((prev_diff < threshold_s2) && (diff >= threshold_s2)) {
                    on_start_us = now_us;
                    note_state = STATE_ON_MEASURING;
                }
                break;

            case STATE_ON_MEASURING:
                // キャンセル:
                // S2近辺の微小揺れではなく、十分下へ戻ったときだけ
                if (diff <= (threshold_s2 - ON_CANCEL_HYS)) {
                    send_uart_line("ON_CANCEL");
                    note_state = STATE_IDLE_OFF;
                }
                // On計測完了: S3 を下から上へ跨ぐ
                else if ((prev_diff < threshold_s3) && (diff >= threshold_s3)) {
                    uint64_t elapsed = now_us - on_start_us;
                    send_uart_time("ON", elapsed);

                    if (tud_midi_mounted()) {
                        send_note_on(NOTE_NUM, ON_VELOCITY);
                    }
                    note_state = STATE_IDLE_ON;
                }
                break;

            case STATE_IDLE_ON:
                // Off計測開始: S4 を上から下へ跨ぐ
                if ((prev_diff > threshold_s4) && (diff <= threshold_s4)) {
                    off_start_us = now_us;
                    note_state = STATE_OFF_MEASURING;
                }
                break;

            case STATE_OFF_MEASURING:
                // キャンセル:
                // S4近辺の微小揺れではなく、十分上へ戻ったときだけ
                if (diff >= (threshold_s4 + OFF_CANCEL_HYS)) {
                    send_uart_line("OFF_CANCEL");
                    note_state = STATE_IDLE_ON;
                }
                // Off計測完了: S1 を上から下へ跨ぐ
                else if ((prev_diff > threshold_s1) && (diff <= threshold_s1)) {
                    uint64_t elapsed = now_us - off_start_us;
                    send_uart_time("OFF", elapsed);

                    if (tud_midi_mounted()) {
                        send_note_off(NOTE_NUM, OFF_VELOCITY);
                    }
                    note_state = STATE_IDLE_OFF;
                }
                break;

            default:
                note_state = STATE_IDLE_OFF;
                break;
        }

        prev_diff = diff;
        sleep_ms(1);
    }
}