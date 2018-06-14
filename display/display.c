#include <ets_sys.h>
#include <osapi.h>
#include <os_type.h>
#include <driver/uart.h>
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>
#include "http.h"
#include "requester.h"
#include <strings.h>
#include "led.h"

#define LED_POWERON   led_display(0xFF, LED_PULSE, 100, 0xFF)
#define LED_AP        led_display(0xFF, LED_FLASH, 100, 0xFF)
#define LED_CONNECTED led_display(0x7E, LED_PULSE, 10, 0xFF)
#define LED_READY     led_display(0x3C, LED_PULSE, 5, 0xFF)

#define LED_ERROR     led_display(0x55, LED_FLASH, 250, 0xFF)

LOCAL const char ssid[] = "EConfig";

LOCAL os_timer_t error_timer_t;
LOCAL os_timer_t request_timer_t;

LOCAL requester_cookies_t s_jar = {NULL, 0};

void ICACHE_FLASH_ATTR connectAP(void);

LOCAL void ICACHE_FLASH_ATTR error_timer(void *arg) {
  os_printf("ERROR Timout\n");
  wifi_station_disconnect();
  connectAP();
}

void ICACHE_FLASH_ATTR request_status(void* ctx, e_requester_status_t err) {
  os_printf("Connection status: %d\n", err);
  if (err == REQ_CONN_FAIL) {
    os_timer_arm(&request_timer_t, 20000, 0);
  }
  if (err == REQ_CONN_CLOSED) {
    for (int i = 0; i < s_jar.ui_size; i++) {
      os_printf("Cookie %s=%s\n",
                s_jar.s_cookies[i].pch_name,
                s_jar.s_cookies[i].pch_value);
    }
  }
}

bool  ICACHE_FLASH_ATTR response_header(void* ctx,
                     uint32_t ui_response_code) {
  os_printf("code %d\n", ui_response_code);
 return true;
}

bool ICACHE_FLASH_ATTR
response_processor(void* ctx,
                   const char *pdata,
                   unsigned short len) {
  return true;
}

void ICACHE_FLASH_ATTR request_timer(void *arg) {
  os_printf("Starting request\n");
  if (!request("www.google.com",
               443,
               "/",
               &s_jar,
               NULL,
               0,
               NULL,
               request_status,
               response_header,
               response_processor,
               NULL)) {
    os_printf("failed to start requester");
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
  LED_AP;
  os_timer_disarm(&request_timer_t);

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
    LED_CONNECTED;
    break;
  case EVENT_STAMODE_GOT_IP:
    // we are good to start services here
    LED_READY;
    // reinitilise http server with new connection
    http_init(80);
    os_timer_arm(&request_timer_t,1000,0);
    break;
  case EVENT_STAMODE_DISCONNECTED:
    LED_POWERON;
    // todo, disconnected from base startion for over some time, start
    // in softap mode.
    break;
  case EVENT_STAMODE_DHCP_TIMEOUT:
    // start AP mode
    connectAP();
    break;
  case EVENT_OPMODE_CHANGED:
    if (evt->event_info.opmode_changed.new_opmode == STATION_MODE) {
      LED_POWERON;
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

  uart_init(BIT_RATE_115200);

  // hook into wifi callbacks
  wifi_set_event_handler_cb(&wifi_handle_event_cb);

  os_printf("\nSDK version: %s \n", system_get_sdk_version());

  // initialise handlers for http server
  http_register_init();
  http_register_handler("/favicon.ico", &fail_handler);
  http_register_handler("/wifi", &wifi_handler);
  http_register_handler("/info", &info_handler);
  http_register_handler("/", &root_handler);

  // setup timers
  os_timer_setfn(&error_timer_t, error_timer , NULL);
  os_timer_setfn(&request_timer_t, request_timer , NULL);

  // setup leds
  led_init();
  LED_POWERON;

  // start wifi connection process
  wifi_set_opmode( STATION_MODE );
  os_printf("Connecting as station\n");
}
