/*
 * An interface to control leds over spi shifter, so its possible to
 * set a sequence of leds with a mode (e.g. flashing, brightness,
 * l/r rotation)
 */

#include <eagle_soc.h>
#include <ets_sys.h>
#include <pwm.h>
#include <osapi.h>
#include <driver/spi_interface.h>
#include <stdint.h>
#include <stdbool.h>
#include "led.h"

// set GPIO4 as PWM pin
#define PWM_0_OUT_IO_MUX PERIPHS_IO_MUX_GPIO4_U
#define PWM_0_OUT_IO_NUM 4
#define PWM_0_OUT_IO_FUNC  FUNC_GPIO4

#if !defined(LED_PULSE_LOWER_BOUND)
// Define a lower bound on the pluse animation to avoid a large time
// when the LEDs are dark. This depends on the specifc LEDs/circuit.
 #define LED_PULSE_LOWER_BOUND 25
#endif // !defined(LED_PULSE_LOWER_BOUND)

LOCAL os_timer_t g_spi_timer_t;

LOCAL uint32_t g_ui_value = 0xAA; // needs to be 32bit
LOCAL uint8_t g_ui_flags = 0x00;
LOCAL uint32_t g_ui_speed = 0x00;
LOCAL uint8_t g_ui_brightness = 0x00;
LOCAL uint8_t g_ui_brightness_target = 0xFF;
LOCAL bool g_b_pulse_down = true;

LOCAL
void ICACHE_FLASH_ATTR spi_timer(void *arg) {
  SpiData d = {0,0,0,0,&g_ui_value,1};
  if (SPIMasterSendData(SpiNum_HSPI, &d) == -1) {
    os_printf("spi failed to send");
    return;
  }

  switch (g_ui_flags) {
  case LED_NONE:
    g_ui_brightness = g_ui_brightness_target;
    break;
  case LED_FADE:
    if (g_ui_brightness < g_ui_brightness_target) {
      g_ui_brightness++;
      os_timer_arm(&g_spi_timer_t,g_ui_speed,0);
    }
    if (g_ui_brightness > g_ui_brightness_target) {
      g_ui_brightness--;
      os_timer_arm(&g_spi_timer_t,g_ui_speed,0);
    }
    break;
  case LED_PULSE:
    if (g_b_pulse_down) {
      if (g_ui_brightness > LED_PULSE_LOWER_BOUND) {
        g_ui_brightness--;
      } else {
        g_b_pulse_down = false;
      }
    } else {
      if (g_ui_brightness < g_ui_brightness_target) {
        g_ui_brightness++;
      } else {
        g_b_pulse_down = true;
      }
    }
    os_timer_arm(&g_spi_timer_t,g_ui_speed,0);
    break;
  case LED_FLASH:
    if (g_ui_brightness == g_ui_brightness_target) {
      g_ui_brightness = 0;
    } else {
      g_ui_brightness = g_ui_brightness_target;
    }
    os_timer_arm(&g_spi_timer_t,g_ui_speed,0);
    break;
  case LED_ROTATE_L:
    g_ui_brightness = g_ui_brightness_target;
    g_ui_value = (g_ui_value << 1) | (g_ui_value >> 7);
    os_timer_arm(&g_spi_timer_t,g_ui_speed,0);
    break;
  case LED_ROTATE_R:
    g_ui_brightness = g_ui_brightness_target;
    g_ui_value = (g_ui_value << 7) | (g_ui_value >> 1);
    os_timer_arm(&g_spi_timer_t,g_ui_speed,0);
    break;
  }

  pwm_set_duty(((uint32_t)g_ui_brightness) * 88, 0);
  pwm_start();

}

void ICACHE_FLASH_ATTR
led_init(void) {
  uint32 io_info[1][3] = {
    { PWM_0_OUT_IO_MUX, PWM_0_OUT_IO_FUNC, PWM_0_OUT_IO_NUM },
  };
  uint32 pwm_duty_init[1] = {0};

  /* pwm_init should be called only once, for now  */
  pwm_init(1000, pwm_duty_init, 1, io_info);

  /* configure hspi, first set pin functions, then init HSPI */
  WRITE_PERI_REG(PERIPHS_IO_MUX, 0x105);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, 2);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, 2);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, 2);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, 2);
  SpiAttr attr = { SpiMode_Master,
                   SpiSubMode_0,
                   SpiSpeed_8MHz,
                   SpiBitOrder_MSBFirst };
  SPIInit(SpiNum_HSPI, &attr);

  os_timer_setfn(&g_spi_timer_t, spi_timer , NULL);
  os_timer_arm(&g_spi_timer_t,5,0);
}

void ICACHE_FLASH_ATTR
led_display(uint8_t  ui_value,
            uint8_t  ui_mode,
            uint32_t ui_delay,
            uint8_t  ui_brightness) {
  os_timer_disarm(&g_spi_timer_t);
  if (ui_mode != LED_BRIGHT) {
    g_ui_value             = ui_value;
    g_b_pulse_down         = g_ui_flags == LED_PULSE ? g_b_pulse_down : true;
    g_ui_flags             = ui_mode;
    g_ui_speed             = ui_delay;
  }
  g_ui_brightness_target = ui_brightness;
  os_timer_arm(&g_spi_timer_t,5,0);
}
