#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "tusb.h"

// ノート設定
#define NOTE_NUM        60
#define ON_VELOCITY     127
#define OFF_VELOCITY    0

// ADC設定
#define ADC_RISE_PIN    26   // GPIO26 = ADC0
#define ADC_FALL_PIN    27   // GPIO27 = ADC1
#define ADC_RISE_INPUT  0    // ADC0
#define ADC_FALL_INPUT  1    // ADC1

// ヒステリシスしきい値
#define ON_THRESHOLD    550
#define OFF_THRESHOLD   250

// UART設定（Dueとの通信用）
#define UART_ID         uart1
#define UART_BAUD_RATE  115200
#define UART_TX_PIN     4    // Pico GP4
#define UART_RX_PIN     5    // Pico GP5

static void uart_setup(void)
{
    uart_init(UART_ID, UART_BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
}

static void process_uart_handshake(void)
{
    static char rx_buf[64];
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
                uart_puts(UART_ID, "OK\n");
            } else {
                uart_puts(UART_ID, "ERR\n");
            }

            rx_idx = 0;
        } else {
            if (rx_idx < (int)(sizeof(rx_buf) - 1)) {
                rx_buf[rx_idx++] = c;
            } else {
                rx_idx = 0;  // バッファオーバーフロー時は破棄
            }
        }
    }
}

int main(void)
{
    // ADC初期化
    adc_init();
    adc_gpio_init(ADC_RISE_PIN);
    adc_gpio_init(ADC_FALL_PIN);

    // UART初期化
    uart_setup();

    // TinyUSB初期化
    tusb_init();

    bool note_on = false;

    while (true)
    {
        tud_task();

        // Dueとのハンドシェイク処理
        process_uart_handshake();

        // ADC26(GPIO26, ADC0) 読み取り
        adc_select_input(ADC_RISE_INPUT);
        uint16_t adc_rise = adc_read();

        // ADC27(GPIO27, ADC1) 読み取り
        adc_select_input(ADC_FALL_INPUT);
        uint16_t adc_fall = adc_read();

        // 差分を計算
        int16_t sensor_diff = (int16_t)adc_rise - (int16_t)adc_fall;

        if (tud_midi_mounted())
        {
            uint8_t packet[4];

            // まだノートが鳴っていない時だけ Note On
            if (!note_on && sensor_diff >= ON_THRESHOLD)
            {
                packet[0] = 0x09;
                packet[1] = 0x90;
                packet[2] = NOTE_NUM;
                packet[3] = ON_VELOCITY;
                tud_midi_stream_write(0, packet, 4);
                note_on = true;
            }
            // すでにノートが鳴っている時だけ Note Off
            else if (note_on && sensor_diff <= OFF_THRESHOLD)
            {
                packet[0] = 0x08;
                packet[1] = 0x80;
                packet[2] = NOTE_NUM;
                packet[3] = OFF_VELOCITY;
                tud_midi_stream_write(0, packet, 4);
                note_on = false;
            }
        }

        sleep_ms(1);
    }
}