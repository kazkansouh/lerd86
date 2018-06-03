#include <ets_sys.h>
#include <osapi.h>
#include <gpio.h>
#include <os_type.h>
#include <driver/uart.h>
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>
#include "http.h"
#include <driver/spi_interface.h>

LOCAL const int pin_station = 5;
LOCAL const int pin_ap = 4;

LOCAL const char ssid[] = "EConfig";

LOCAL os_timer_t error_timer_t;
LOCAL os_timer_t spi_timer_t;

void ICACHE_FLASH_ATTR connectAP(void);

void ICACHE_FLASH_ATTR error_timer(void *arg) {
  os_printf("ERROR Timout\n");
  wifi_station_disconnect();
  connectAP();
}

LOCAL uint32_t spi_var = 0x01;

void ICACHE_FLASH_ATTR spi_timer(void *arg) {
  spi_var <<= 1;
  spi_var %= 0x100;

  if (!spi_var) {
    spi_var = 1;
  }
  SpiData d = {0,0,0,0,&spi_var,1};

  if (SPIMasterSendData(SpiNum_HSPI, &d) == -1) {
    os_printf("spi failed to send");
  } else {
    os_timer_arm(&spi_timer_t,50,0);
  }
}

void ICACHE_FLASH_ATTR connectStation(const char* ssid, const uint8 ssid_len,
                                      const char* pass, const uint8 pass_len) {
  struct station_config stationConf;

  os_memset(&stationConf.ssid, 0x00, 64);
  os_memset(&stationConf.password, 0x00, 32);

  os_memcpy(&stationConf.ssid, ssid, ssid_len);
  os_memcpy(&stationConf.password, pass, pass_len);

  os_printf("setting ssid: %s\n", stationConf.ssid);
  os_printf("setting key: %s\n", stationConf.password);

  // will cause event of type 8 in wifi callback
  wifi_set_opmode( STATION_MODE );

  // must be called after setting mode
  wifi_station_set_config(&stationConf);
}

void ICACHE_FLASH_ATTR connectAP() {
  struct softap_config apConf;
  os_printf("starting AP\n");
  // set leds
  gpio_output_set((1 << pin_ap), (1 << pin_station), 0, 0);

  wifi_set_opmode( SOFTAP_MODE );
  os_memcpy(&apConf.ssid, ssid, os_strlen(ssid));
  apConf.ssid_len = os_strlen(ssid);
  apConf.authmode = AUTH_OPEN;
  wifi_softap_set_config_current(&apConf);
}

const char* msg_welcome =
  HTTP_RESP_HEADER(200, text/html)
  HTML_STYLE_DOC(
    "ESP8622",css,
    "<h1>Hello I am ESP!</h1>"
    "<a href=\"info\">Info</a><br/>"
    "<a href=\"wifi\">Configure WiFi Details</a>");
const char* msg_setwifi_page =
  HTTP_RESP_HEADER(200, text/html)
  HTML_STYLE_DOC(
    "ESP8622 SetWiFi Details",css,
    "<form method=\"get\">"
    "SSID: <input type=\"text\" name=\"ssid\"></input><br>"
    "Password: <input type=\"text\" name=\"password\"></input><br>"
    "<input type=\"submit\">Submit</input></form>");
const char* msg_info =
  HTTP_RESP_HEADER(200, text/html)
  HTML_STYLE_DOC(
    "ESP8622",css,
    "<h1>System Info</h1>"
    "SDK Info:<pre>%s</pre><br/>"
    "Free heap: %d bytes");
const char* msg_css =
  HTTP_RESP_HEADER(200, text/css)
  "body {"
    "background: black;"
    "color: #80c0c0"
  "}";

/* handler that fails (results in 404 error */
bool ICACHE_FLASH_ATTR fail_handler(struct espconn* conn,
                                    struct http_request_context_t* ctx) {
  return false;
}

/* default handler, used to write static content */
bool ICACHE_FLASH_ATTR root_handler(struct espconn* conn,
                                    struct http_request_context_t* ctx) {
  const char* ptr = msg_welcome;
  if (strcasecmp(ctx->pch_resource, "/css") == 0) {
    ptr = msg_css;
  }
  int x = espconn_send(conn, (uint8*)ptr, os_strlen(ptr));
  if (x != 0) {
    os_printf("failed to write to connection: %d\n", x);
  }
  return true;
}

/* handles the wifi configuration page */
bool ICACHE_FLASH_ATTR wifi_handler(struct espconn* conn,
                                    struct http_request_context_t* ctx) {
  const char* pch_ssid = http_request_context_lookup(ctx, "ssid");
  const char* pch_pass = http_request_context_lookup(ctx, "password");

  if (pch_ssid && pch_pass) {
    // ready to set network details
    connectStation(pch_ssid, os_strlen(pch_ssid),
                   pch_pass, os_strlen(pch_pass));
    return root_handler(conn, ctx);
  } else {
    espconn_send(conn, (uint8*)msg_setwifi_page, os_strlen(msg_setwifi_page));
    return true;
  }
}

/* displays system info */
bool ICACHE_FLASH_ATTR info_handler(struct espconn* conn,
                                    struct http_request_context_t* ctx) {
  char ch_buffer[500];
  int i = os_sprintf(ch_buffer,
                     msg_info,
                     system_get_sdk_version(),
                     system_get_free_heap_size());
  espconn_send(conn, (uint8*)ch_buffer, i);
  return true;
}


void wifi_handle_event_cb(System_Event_t *evt) {
  /* ignore EVENT_STAMODE_DISCONNECTED as it can be emitted during
     normal connection process to base station */
  if (evt->event != EVENT_STAMODE_DISCONNECTED) {
    os_timer_disarm(&error_timer_t);
  }
  os_printf("event %x\n", evt->event);

  /*
    EVENT_STAMODE_CONNECTED = 0,
    EVENT_STAMODE_DISCONNECTED,
    EVENT_STAMODE_AUTHMODE_CHANGE,
    EVENT_STAMODE_GOT_IP,
    EVENT_STAMODE_DHCP_TIMEOUT,
    EVENT_SOFTAPMODE_STACONNECTED,
    EVENT_SOFTAPMODE_STADISCONNECTED,
    EVENT_SOFTAPMODE_PROBEREQRECVED,
    EVENT_OPMODE_CHANGED,
    EVENT_MAX
  */

  switch (evt->event) {
  case EVENT_STAMODE_CONNECTED:
    // wait to get IP address
    os_timer_arm(&error_timer_t,20000,0);
    gpio_output_set(0, (1 << pin_station) | (1 << pin_ap), 0, 0);
    break;
  case EVENT_STAMODE_GOT_IP:
    // we are good to start services here
    gpio_output_set((1 << pin_station), 0, 0, 0);
    // reinitilise http server with new connection
    http_init(80);
    break;
  case EVENT_STAMODE_DISCONNECTED:
    gpio_output_set(0, (1 << pin_station), 0, 0);
    // todo, disconnected from base startion for over some time, start
    // in softap mode.
    break;
  case EVENT_STAMODE_DHCP_TIMEOUT:
    // start AP mode
    connectAP();
    break;
  case EVENT_OPMODE_CHANGED:
    if (evt->event_info.opmode_changed.new_opmode == STATION_MODE) {
      gpio_output_set(0, (1 << pin_station) | (1 << pin_ap), 0, 0);
      os_printf("old mode: %d\n", evt->event_info.opmode_changed.old_opmode);
      wifi_station_disconnect();
      wifi_station_connect();
      os_timer_arm(&error_timer_t, 20000, 0);
    }
    if (evt->event_info.opmode_changed.new_opmode == SOFTAP_MODE) {
      http_init(80);
    }
    break;
  }
}

void ICACHE_FLASH_ATTR user_init() {

  // init gpio sussytem
  gpio_init();

  // configure GPIO4 and GPIO5 pins for output
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
  gpio_output_set(0, 0, (1 << pin_station) | (1 << pin_ap), 0);

  uart_init(BIT_RATE_115200);

  // hook into wifi callbacks
  wifi_set_event_handler_cb(&wifi_handle_event_cb);

  os_printf("\nSDK version: %s \n", system_get_sdk_version());

  // configure hspi, first set pin functions, then init HSPI
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

  // initialise handlers for http server
  http_register_init();
  http_register_handler("/favicon.ico", &fail_handler);
  http_register_handler("/wifi", &wifi_handler);
  http_register_handler("/info", &info_handler);
  http_register_handler("/", &root_handler);

  // setup timers
  os_timer_setfn(&error_timer_t, error_timer , NULL);
  os_timer_setfn(&spi_timer_t, spi_timer , NULL);
  os_timer_arm(&spi_timer_t,50,0);

  // start wifi connection process
  wifi_set_opmode( STATION_MODE );
  os_printf("Connecting as station\n");
}
