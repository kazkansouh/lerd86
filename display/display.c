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
#include "stringutil.h"
#include <stdlib.h>
#include <spi_flash.h>

#define LED_POWERON   do {                      \
    gb_ready = false;                           \
    led_display(0xFF, LED_PULSE, 10, 0x80);     \
  } while(0);
#define LED_AP        do {                      \
    gb_ready = false;                           \
    led_display(0xFF, LED_FLASH, 100, 0xFF);    \
  } while(0);
#define LED_CONNECTED do {                      \
    gb_ready = false;                           \
    led_display(0x7E, LED_PULSE, 10, 0x80);     \
  } while(0);
#define LED_READY     do {                      \
    gb_ready = true;                            \
    led_display(0x3C, LED_PULSE, 10, 0x80);     \
  } while(0);

#define LED_HTTP_CONN led_display(0x18, LED_PULSE, 5, 0xFF)
#define LED_LOGIN_ERR led_display(0x18, LED_ROTATE_L, 500, 0xFF)

#define LED_ERROR     led_display(0x55, LED_FLASH, 500, 0xFF)

LOCAL const char gpch_ssid[] = "EConfig";

LOCAL os_timer_t gs_error_timer;
LOCAL os_timer_t gs_request_timer;

LOCAL bool gb_ready = false;

LOCAL requester_cookies_t* gp_s_cookiejar = NULL;

/* main state of application */
LOCAL enum {
  eInit,
  eReAuth,
  eGetToken,
  eLogin,
  eMembers,
  eMembersToken,
  eDoNothing,
} ge_state = eInit;

/* tokens to search for in the html response */
LOCAL const char gpch_token[] =
  "input name=\"__RequestVerificationToken\" type=\"hidden\" value=\"";
LOCAL const char gpch_people[] =
  ", there are <span class=\"heading heading--"
  "level3 secondary-color margin-none\">";
/* auxiallary state while processing responses */
LOCAL uint8_t gui_match_state = 0;
/* scanner context */
LOCAL scan_context_t gs_scan_ctx;

/* following 4 are for the login process: headers and post data */
LOCAL
requester_header_t gs_hdr[] = { { "Content-Type", "application/json" } ,
                                { "__RequestVerificationToken", NULL } ,
};
LOCAL requester_headers_t gs_hdrs = { gs_hdr, 2 };
LOCAL const char gpch_login_request[] =
  "{"
    "\"associateAccount\": \"false\", "
    "\"pin\": \"%s\", "
    "\"email\": \"%s\""
  "}";
LOCAL char* gpch_post_data;


#define PARAMS_EMAIL_LEN 102
#define PARAMS_PIN_LEN 10
#define PARAMS_VERSION_BASE 0xF4AA5880
#define PARAMS_VERSION 2
#define PARAMS_MAGIC_NUMBER ((uint32_t)(PARAMS_VERSION_BASE + PARAMS_VERSION))

/* parameters that can be written to flash, each sector is 4KiB, so
   whole strcture should be less than the size of the sector */
typedef struct {
  const uint32_t ui_magic;
  char pch_email[PARAMS_EMAIL_LEN];
  char pch_pin[PARAMS_PIN_LEN];
} flash_params_t;
LOCAL flash_params_t gs_params;

LOCAL void ICACHE_FLASH_ATTR connectAP(void);
LOCAL void ICACHE_FLASH_ATTR save_params(void);

LOCAL void ICACHE_FLASH_ATTR error_timer(void *arg) {
  os_printf("ERROR Timout\n");
  wifi_station_disconnect();
  connectAP();
}

/*
 * callback when a the status of a request changes
 */
void ICACHE_FLASH_ATTR request_status_cb(void* ctx, e_requester_status_t err) {
  os_printf("Connection status: %d\n", err);
  if (err == REQ_CONN_FAIL) {
    ge_state = eInit;
    os_timer_arm(&gs_request_timer, 20000, 0);
    LED_ERROR;
  }
  if (err == REQ_CONN_CLOSED) {
    if (ge_state == eMembersToken) {
      ge_state = eMembers;
      os_timer_arm(&gs_request_timer, 60000, 0);
    } else if (ge_state == eDoNothing) {
      // do nothing
    } else {
      for (int i = 0; gp_s_cookiejar && i < gp_s_cookiejar->ui_size; i++) {
        os_printf("Cookie %s=%s\n",
                  gp_s_cookiejar->s_cookies[i].pch_name,
                  gp_s_cookiejar->s_cookies[i].pch_value);
      }
      /* timers of 250 have caused esp to reset */
      os_timer_arm(&gs_request_timer, 500, 0);
    }
  }
}

/* callback for when /login/ headers are received */
bool  ICACHE_FLASH_ATTR login_response_header_cb(void* ctx,
                                                 uint32_t ui_response_code) {
  os_printf("/login/ code %d\n", ui_response_code);
  if (ui_response_code == 200) {
    gs_scan_ctx.pch_token = (char*)gpch_token;
    gs_scan_ctx.ui_position = 0;
    gui_match_state = 0;
  } else {
    ge_state = eInit;
    os_timer_arm(&gs_request_timer, 20000, 0);
    LED_ERROR;
  }
  return true;
}

/* callback for when /api/members/login/ headers are received */
bool  ICACHE_FLASH_ATTR loginapi_response_header_cb(void* ctx,
                                                    uint32_t ui_response_code) {
  os_printf("/api/members/login/ code %d\n", ui_response_code);
  // free memory allocated by scan_until
  os_free(gs_scan_ctx.pch_token);
  gs_scan_ctx.pch_token = NULL;
  gs_scan_ctx.ui_position = 0;
  // free post data
  os_free(gpch_post_data);
  gpch_post_data = NULL;
  switch (ui_response_code) {
  case 200:
    ge_state = eMembers;
    break;
  case 302:
    os_printf("login failed!!\n");
    ge_state = eDoNothing;
    LED_LOGIN_ERR;
    break;
  default:
    os_printf("login unsuccessful!!\n");
    ge_state = eInit;
    os_timer_arm(&gs_request_timer, 20000, 0);
  }
  return true;
}

/* callback for when /members/ headers are received */
bool  ICACHE_FLASH_ATTR members_response_header_cb(void* ctx,
                                                   uint32_t ui_response_code) {
  os_printf("/members/ code %d\n", ui_response_code);
  switch (ui_response_code) {
  case 200:
    gs_scan_ctx.pch_token = (char*)gpch_people;
    gs_scan_ctx.ui_position = 0;
    gui_match_state = 0;
    break;
  case 302:
    // need to authenticate again
    ge_state = eReAuth;
    os_timer_arm(&gs_request_timer, 100, 0);
    break;
  default:
    ge_state = eInit;
    os_timer_arm(&gs_request_timer, 100, 0);
    break;
  }
  return true;
}

/* callback with content of request */
bool ICACHE_FLASH_ATTR
login_response_cb(void* ctx,
                  const char *pch_data,
                  uint16_t ui_len) {
  uint16_t ui_remain = ui_len;
  if (gui_match_state == 0 && scan_token(&gs_scan_ctx, pch_data, &ui_remain)) {
    gui_match_state = 1;
    gs_scan_ctx.pch_token = NULL;
    gs_scan_ctx.ui_position = 0;
    os_printf("login token found\n");
  }
  if (gui_match_state == 1 && scan_until(&gs_scan_ctx,
                                         '"',
                                         pch_data + (ui_len - ui_remain),
                                         &ui_remain)) {
    gui_match_state = 2;
    os_printf("value '%s'\n", gs_scan_ctx.pch_token);
    ge_state = eLogin;
  }
  return true;
}

/* callback with content of request */
bool ICACHE_FLASH_ATTR
loginapi_response_cb(void* ctx,
                     const char *pch_data,
                     uint16_t ui_len) {
  // content is not important, only the set-cookie header
  return true;
}

/* callback with content of request */
bool ICACHE_FLASH_ATTR
members_response_cb(void* ctx,
                    const char *pch_data,
                    uint16_t ui_len) {
  uint16_t ui_remain = ui_len;
  if (gui_match_state == 0 && scan_token(&gs_scan_ctx, pch_data, &ui_remain)) {
    gui_match_state = 1;
    gs_scan_ctx.pch_token = NULL;
    gs_scan_ctx.ui_position = 0;
    os_printf("token found\n");
  }
  if (gui_match_state == 1 && scan_until(&gs_scan_ctx,
                                         ' ',
                                         pch_data + (ui_len - ui_remain),
                                         &ui_remain)) {
    gui_match_state = 2;
    os_printf("value '%s'\n", gs_scan_ctx.pch_token);
    char* ptr_end = NULL;
    int i = strtol(gs_scan_ctx.pch_token, &ptr_end, 10);

    if (!ptr_end || ptr_end == gs_scan_ctx.pch_token) {
      i = 0;
    }

    uint8_t ui_val;
    if (i < 20)
      ui_val = 0x00;
    else if (i <= 35)
      ui_val = 0x01;
    else if (i <= 50)
      ui_val = 0x03;
    else if (i <= 70)
      ui_val = 0x07;
    else if (i <= 90)
      ui_val = 0x0F;
    else if (i <= 110)
      ui_val = 0x1F;
    else if (i <= 125)
      ui_val = 0x3F;
    else if (i <= 135)
      ui_val = 0x7F;
    else
      ui_val = 0xFF;

    led_display(ui_val, LED_NONE, 0, 0x80);

    os_free(gs_scan_ctx.pch_token);
    gs_scan_ctx.pch_token = NULL;
    gs_scan_ctx.ui_position = 0;
    ge_state = eMembersToken;
  }
  return true;
}

/*
 * Application event handler.
 */
void ICACHE_FLASH_ATTR request_timer(void *arg) {
  size_t i = 0;

  switch (ge_state) {
  case eReAuth:
  case eInit:
    os_printf("Starting request for verification token\n");
    if (gp_s_cookiejar) {
      os_printf("Clearing cookies\n");
      requester_cookie_free(gp_s_cookiejar);
    }
    gp_s_cookiejar =
      (requester_cookies_t*)os_zalloc(sizeof(requester_cookies_t));
    if (!request("www.puregym.com",
                 443,
                 "/login/",
                 gp_s_cookiejar,
                 NULL,
                 0,
                 NULL,
                 request_status_cb,
                 login_response_header_cb,
                 login_response_cb,
                 NULL)) {
      os_printf("failed to start requester %d\n", ge_state);
    } else {
      ge_state = eGetToken;
      if (ge_state != eReAuth) {
        LED_HTTP_CONN;
      }
    }
    break;
  case eLogin:
    os_printf("Starting request for login api\n");
    gs_hdr[1].pch_value = gs_scan_ctx.pch_token;
    if (gpch_post_data) {
      os_free(gpch_post_data);
    }
    gpch_post_data = (char*)os_malloc(sizeof(gpch_login_request)
                                      + os_strlen(gs_params.pch_email)
                                      + os_strlen(gs_params.pch_pin)
                                      + 1);
    i = os_sprintf(gpch_post_data,
                   gpch_login_request,
                   gs_params.pch_pin,
                   gs_params.pch_email);
    os_printf("sending: %s\n", gpch_post_data);
    if (!request("www.puregym.com",
                 443,
                 "/api/members/login/",
                 gp_s_cookiejar,
                 (const uint8_t*)gpch_post_data,
                 i,
                 &gs_hdrs,
                 request_status_cb,
                 loginapi_response_header_cb,
                 loginapi_response_cb,
                 NULL)) {
      os_printf("failed to start requester %d\n", ge_state);
    }
    break;
  case eMembers:
    os_printf("Starting request for members\n");
    if (!request("www.puregym.com",
                 443,
                 "/members/",
                 gp_s_cookiejar,
                 NULL,
                 0,
                 NULL,
                 request_status_cb,
                 members_response_header_cb,
                 members_response_cb,
                 NULL)) {
      os_printf("failed to start requester %d\n", ge_state);
    } else {
      os_printf("ok\n");
    }
    break;
  case eGetToken:
  case eMembersToken:
    os_printf("bad state, token not found?: %d!\n", ge_state);
    ge_state = eInit;
    os_timer_arm(&gs_request_timer, 500, 0);
    break;
  case eDoNothing:
    break;
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

LOCAL void ICACHE_FLASH_ATTR
connectAP() {
  struct softap_config apConf;
  os_printf("starting AP\n");
  LED_AP;
  os_timer_disarm(&gs_request_timer);

  wifi_set_opmode( SOFTAP_MODE );
  os_memcpy(&apConf.ssid, gpch_ssid, os_strlen(gpch_ssid));
  apConf.ssid_len = os_strlen(gpch_ssid);
  apConf.authmode = AUTH_OPEN;
  wifi_softap_set_config_current(&apConf);
}

const char* const msg_welcome =
  HTML_STYLE_DOC(
    "ESP8622",css,
    "<h1>Hello I am ESP!</h1>"
    "<a href=\"info\">Info</a><br/>"
    "<a href=\"wifi\">Configure WiFi Details</a><br/>"
    "<a href=\"pg\">Configure PureGym</a>");
const char* const msg_setwifi_page =
  HTML_STYLE_DOC(
    "ESP8622 SetWiFi Details",css,
    "<form method=\"get\">"
    "SSID: <input type=\"text\" name=\"ssid\"></input><br>"
    "Password: <input type=\"text\" name=\"password\"></input><br>"
    "<input type=\"submit\"></input></form>");
const char* const msg_info_page =
  HTML_STYLE_DOC(
    "ESP8622",css,
    "<h1>System Info</h1>"
    "SDK Info:<pre>%s</pre><br/>"
    "Free heap: %d bytes");
const char* const msg_css =
  "body {"
    "background: black;"
    "color: #80c0c0"
  "}";
const char const msg_puregym_page[] =
  HTML_STYLE_DOC(
    "ESP8622 PureGym Login",css,
    "<form method=\"get\">"
    "E-Mail: <input type=\"text\" name=\"email\" value=\"%s\"></input><br>"
    "PIN: <input type=\"text\" name=\"pin\" value=\"%s\"></input><br>"
    "<input type=\"submit\"></input></form>");

const char* const ct_html = "text/html";
const char* const ct_css = "text/css";

/* handler that fails (results in 404 error */
bool ICACHE_FLASH_ATTR fail_handler(struct espconn* conn,
                                    struct http_request_context_t* ctx) {
  return false;
}

/* default handler, used to write static content */
bool ICACHE_FLASH_ATTR root_handler(struct espconn* conn,
                                    struct http_request_context_t* ctx) {
  const char* ptr_data = msg_welcome;
  const char* ptr_ct = ct_html;
  if (strcasecmp(ctx->pch_resource, "/css") == 0) {
    ptr_data = msg_css;
    ptr_ct = ct_css;
  }
  int8_t i = http_send_response(conn,
                                200,
                                ptr_ct,
                                NULL,
                                0,
                                ptr_data,
                                os_strlen(ptr_data),
                                false);
  if (i != ESPCONN_OK) {
    os_printf("failed to write to connection: %d\n", i);
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
    int8_t i = http_send_response(conn,
                                  200,
                                  ct_html,
                                  NULL,
                                  0,
                                  msg_setwifi_page,
                                  os_strlen(msg_setwifi_page),
                                  false);
    if (i != ESPCONN_OK) {
      os_printf("failed to write to connection: %d\n", i);
    }
    return true;
  }
}

/* displays system info */
bool ICACHE_FLASH_ATTR info_handler(struct espconn* conn,
                                    struct http_request_context_t* ctx) {
  char* ch_buffer = (char*)os_malloc(500);
  size_t len = os_sprintf(ch_buffer,
                          msg_info_page,
                          system_get_sdk_version(),
                          system_get_free_heap_size());
  int8_t i = http_send_response(conn,
                                200,
                                ct_html,
                                NULL,
                                0,
                                (const char*)ch_buffer,
                                len,
                                true);
  if (i != ESPCONN_OK) {
    os_printf("failed to write to connection: %d\n", i);
  }
  return true;
}

/* handles the puregym configuration page */
bool ICACHE_FLASH_ATTR puregym_handler(struct espconn* conn,
                                       struct http_request_context_t* ctx) {
  const char* pch_email = http_request_context_lookup(ctx, "email");
  const char* pch_pin = http_request_context_lookup(ctx, "pin");

  if (pch_email && pch_pin &&
      os_strlen(pch_email) < PARAMS_EMAIL_LEN &&
      os_strlen(pch_pin) < PARAMS_PIN_LEN) {
    os_timer_disarm(&gs_request_timer);
    os_strcpy(gs_params.pch_email, pch_email);
    os_strcpy(gs_params.pch_pin, pch_pin);
    save_params();
    ge_state = eInit;
    if (gb_ready) {
      os_timer_arm(&gs_request_timer, 100, 0);
    }
    return root_handler(conn, ctx);
  } else {
    size_t email_len = os_strlen(gs_params.pch_email);
    size_t pin_len = os_strlen(gs_params.pch_pin);
    char* ch_buff = (char*)os_malloc(sizeof(msg_puregym_page)
                                     + email_len + pin_len + 1);
    if (ch_buff) {
      size_t len = os_sprintf(ch_buff,
                              msg_puregym_page,
                              gs_params.pch_email,
                              gs_params.pch_pin);
      int8_t i = http_send_response(conn,
                                    200,
                                    ct_html,
                                    NULL,
                                    0,
                                    (const char*)ch_buff,
                                    len,
                                    true);
      if (i != ESPCONN_OK) {
        os_printf("failed to write to connection: %d\n", i);
      }
    } else {
      os_printf("memory allocation error\n");
      return false;
    }
    return true;
  }
}

void wifi_handle_event_cb(System_Event_t *evt) {
  /* ignore EVENT_STAMODE_DISCONNECTED as it can be emitted during
     normal connection process to base station */
  if (evt->event != EVENT_STAMODE_DISCONNECTED) {
    os_timer_disarm(&gs_error_timer);
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
    os_timer_arm(&gs_error_timer,20000,0);
    LED_CONNECTED;
    break;
  case EVENT_STAMODE_GOT_IP:
    // we are good to start services here
    LED_READY;
    // reinitilise http server with new connection
    http_init(80);
    os_timer_arm(&gs_request_timer,1000,0);
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
      os_timer_arm(&gs_error_timer, 20000, 0);
    }
    if (evt->event_info.opmode_changed.new_opmode == SOFTAP_MODE) {
      http_init(80);
    }
    break;
  }
}

LOCAL void ICACHE_FLASH_ATTR save_params() {
  spi_flash_erase_sector(PG_CONFIG_SECTOR);
  switch (spi_flash_write(PG_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE,
                          (uint32 *)&gs_params,
                          sizeof(flash_params_t))) {
  case SPI_FLASH_RESULT_OK:
    os_printf("params saved\n");
    break;
  case SPI_FLASH_RESULT_ERR:
    os_printf("params failed to save\n");
    break;
  case SPI_FLASH_RESULT_TIMEOUT:
    os_printf("params timed out while saving\n");
    break;
  }
}

void ICACHE_FLASH_ATTR user_init() {
  uart_init(BIT_RATE_115200);

  // hook into wifi callbacks
  wifi_set_event_handler_cb(&wifi_handle_event_cb);

  os_printf("\nSDK version: %s \n", system_get_sdk_version());

  // read saved parameters
  switch (spi_flash_read(PG_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE,
                         (uint32 *)&gs_params,
                         sizeof(flash_params_t))) {
  case SPI_FLASH_RESULT_OK:
    os_printf("params read\n");
    if (gs_params.ui_magic != PARAMS_MAGIC_NUMBER) {
      *(uint32_t*)(&gs_params.ui_magic) = PARAMS_MAGIC_NUMBER;
      os_strcpy(gs_params.pch_email, "you@company.com");
      os_strcpy(gs_params.pch_pin, "12345678");
      save_params();
    }
    break;
  case SPI_FLASH_RESULT_ERR:
    os_printf("params read err\n");
    os_memset(&gs_params, 0, sizeof(flash_params_t));
    break;
  case SPI_FLASH_RESULT_TIMEOUT:
    os_printf("params timeout while reading\n");
    os_memset(&gs_params, 0, sizeof(flash_params_t));
    break;
  }

  // initialise handlers for http server
  http_register_init();
  http_register_handler("/favicon.ico", &fail_handler);
  http_register_handler("/wifi", &wifi_handler);
  http_register_handler("/info", &info_handler);
  http_register_handler("/pg", &puregym_handler);
  http_register_handler("/", &root_handler);

  // setup timers
  os_timer_setfn(&gs_error_timer, error_timer , NULL);
  os_timer_setfn(&gs_request_timer, request_timer , NULL);

  // setup leds
  led_init();
  LED_POWERON;

  // start wifi connection process
  wifi_set_opmode( STATION_MODE );
  os_printf("Connecting as station\n");
}
