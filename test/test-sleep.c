
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include <driver/uart.h>
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>

static volatile os_timer_t oneshot1_init_t;
static volatile os_timer_t oneshot2_init_t;

// update these with wifi credentials
const char ssid[64] = "WIFI";
const char password[32] = "PASS";

void ICACHE_FLASH_ATTR oneshot1_init(void *arg) {
  struct station_config stationConf;

  os_printf("Connecting as station\n");

  os_memcpy(&stationConf.ssid, ssid, 64);
  os_memcpy(&stationConf.password, password, 32);
  wifi_set_opmode( STATION_MODE );
  wifi_station_set_config(&stationConf);

  wifi_station_connect();
  os_timer_disarm(&oneshot1_init_t);
}

bool first;

void ICACHE_FLASH_ATTR ///////
uart_recvTask(os_event_t *events) {
}


#define FPM_SLEEP_MAX_TIME 	 0xFFFFFFF
//#define SLEEP_MAX
		
void fpm_wakup_cb_func1(void) {
  wifi_fpm_close();   	
	
  // disable force sleep function
  wifi_set_opmode(STATION_MODE);      	
  wifi_station_connect();         	
  // set station mode
  // connect to AP
}

void goto_modem_sleep() { 
  wifi_station_disconnect();
  wifi_set_opmode(NULL_MODE);      	
  // set WiFi mode to null mode.
  wifi_fpm_set_sleep_type(MODEM_SLEEP_T); // modem sleep
  wifi_fpm_open();	 	
  
  // enable force sleep
  
#ifdef SLEEP_MAX
  /* For modem sleep, FPM_SLEEP_MAX_TIME can only be wakened by calling
     wifi_fpm_do_wakeup. */
  wifi_fpm_do_sleep(FPM_SLEEP_MAX_TIME);
#else
  // wakeup automatically when timeout.
  wifi_fpm_set_wakeup_cb(fpm_wakup_cb_func1);  // Set wakeup callback
  wifi_fpm_do_sleep(5*1000000);    
#endif 
}

void goto_light_sleep() { 
  wifi_station_disconnect();
  wifi_set_opmode(NULL_MODE);      	
  // set WiFi mode to null mode.
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T); // modem sleep
  wifi_fpm_open();            	 	
  
  // enable force sleep
  
  // wakeup automatically when timeout.
  wifi_fpm_set_wakeup_cb(fpm_wakup_cb_func1);  // Set wakeup callback
  wifi_fpm_do_sleep(5*1000000);    
}

void ICACHE_FLASH_ATTR oneshot2_init(void *arg) {
  os_printf("Trying Light Sleep\n");
  os_timer_disarm(&oneshot2_init_t);
  goto_light_sleep();
}

void wifi_handle_event_cb(System_Event_t *evt) {
  os_printf("event %x\n", evt->event);

  switch (evt->event) {
  case EVENT_STAMODE_GOT_IP: 
    if (first) {
      os_timer_arm(&oneshot2_init_t,1000,0);
      first = false;
    }
    break;
  }
}



void ICACHE_FLASH_ATTR user_init() {
  first = true;
  wifi_set_event_handler_cb(&wifi_handle_event_cb);
  uart_init(BIT_RATE_9600, uart_recvTask);

  uart0_sendStr("Hello World\n");



  os_timer_setfn(&oneshot1_init_t, oneshot1_init , NULL);
  os_timer_setfn(&oneshot2_init_t, oneshot2_init , NULL);
  os_timer_arm(&oneshot1_init_t,1000,0);

}
