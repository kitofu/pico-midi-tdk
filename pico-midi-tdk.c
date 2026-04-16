#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
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
#define ON_THRESHOLD    600
#define OFF_THRESHOLD   250

int main(void)
{
    // ADC初期化
    adc_init();
    adc_gpio_init(ADC_RISE_PIN);
    adc_gpio_init(ADC_FALL_PIN);

    // TinyUSB初期化
    tusb_init();

    bool note_on = false;

    while (true)
    {
        tud_task();

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
        // sleep_ms(1);
    }
}