#ifndef _LED_H
#define _LED_H

#include <stdint.h>

// just display
#define LED_NONE     0x00
// fade from current brightness to target
#define LED_FADE     0x01
// pulse from 0 brigtness to target
#define LED_PULSE    0x02
// flash leds on and off
#define LED_FLASH    0x03
// rotate left or right leds
#define LED_ROTATE_L 0x04
#define LED_ROTATE_R 0x05
// only set brightness of led
#define LED_BRIGHT   0x06

void led_init(void);
void led_display(uint8_t  ui_value,
                 uint8_t  ui_mode,
                 uint32_t ui_delay,
                 uint8_t  ui_brightness);

#endif // _LED_H
