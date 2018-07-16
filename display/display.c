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
#include <gpio.h>
#include "beacon.h"
#include "uconf.h"

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

// these two are missing from the "ets_sys.h" and included here for
// clean compilation.
void ets_isr_mask(int);
void ets_isr_unmask(int);

LOCAL const char gpch_ssid[] = "EConfig";

LOCAL os_timer_t gs_error_timer;
LOCAL os_timer_t gs_request_timer;
LOCAL os_timer_t gs_button_timer;

// uncomment the below line to print heap usage out at regular
// intervals.
//#define DEBUG_HEAP
#ifdef DEBUG_HEAP
 LOCAL os_timer_t gs_debug_timer;
#endif // DEBUG_HEAP

LOCAL bool gb_ready = false;
LOCAL bool gb_button_hyst = false;

LOCAL uint32_t gui_button_count = 0;
LOCAL uint8_t gui_brightness = 0x80;

LOCAL int gi_active = -1;

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

#define xstr(s) str(s)
#define str(s) #s

#define PARAMS_EMAIL_LEN 102
#define PARAMS_PIN_LEN 10
#define PARAMS_VERSION_BASE 0xF4AA5880
#define PARAMS_VERSION 3
#define PARAMS_MAGIC_NUMBER ((uint32_t)(PARAMS_VERSION_BASE + PARAMS_VERSION))

/* parameters that can be written to flash, each sector is 4KiB, so
   whole strcture should be less than the size of the sector */
typedef struct {
  const uint32_t ui_magic;
  char pch_email[PARAMS_EMAIL_LEN];
  char pch_pin[PARAMS_PIN_LEN];
  uint8_t ui_brightness;
} flash_params_t;
LOCAL flash_params_t gs_params;

LOCAL void ICACHE_FLASH_ATTR connectAP(void);
LOCAL void ICACHE_FLASH_ATTR save_params(void);

LOCAL void ICACHE_FLASH_ATTR error_timer(void *arg) {
  os_printf("ERROR Timout\n");
  wifi_station_disconnect();
  connectAP();
}

#ifdef DEBUG_HEAP
 LOCAL void ICACHE_FLASH_ATTR debug_timer(void *arg) {
   os_printf("FREE HEAP: %d\n", system_get_free_heap_size());
 }
#endif // DEBUG_HEAP

/*
 * uConf getters and setters
 */
LOCAL
uint8_t ICACHE_FLASH_ATTR get_brightness() {
  return gui_brightness;
}

LOCAL
bool ICACHE_FLASH_ATTR set_brightness(uint8_t i_brightness) {
  if (i_brightness < 0) {
    i_brightness = 0;
  }
  if (i_brightness > 0xFF) {
    i_brightness = 0xFF;
  }
  gui_brightness = i_brightness;
  if (gb_ready) {
    led_display(LED_NONE, LED_BRIGHT, LED_NONE, gui_brightness);
  }
  return true;
}

LOCAL
char* ICACHE_FLASH_ATTR get_email() {
  return gs_params.pch_email;
}

LOCAL
bool ICACHE_FLASH_ATTR set_email(char* pch_email) {
  size_t len = os_strlen(pch_email);
  if (!pch_email ||
      len >= PARAMS_EMAIL_LEN ||
      len <= 5 ||
      !os_strchr(pch_email, '@')) {
    os_printf("failed to set email %s", pch_email);
    return false;
  }
  os_timer_disarm(&gs_request_timer);
  os_strcpy(gs_params.pch_email, pch_email);
  save_params();
  ge_state = eInit;
  if (gb_ready) {
    os_timer_arm(&gs_request_timer, 500, 0);
  }
  return true;
}

LOCAL
char* ICACHE_FLASH_ATTR get_pin() {
  return gs_params.pch_pin;
}

LOCAL
bool ICACHE_FLASH_ATTR set_pin(char* pch_pin) {
  size_t len = os_strlen(pch_pin);
  if (!pch_pin ||
      len >= PARAMS_PIN_LEN ||
      len <= 5) {
    return false;
  }
  os_timer_disarm(&gs_request_timer);
  os_strcpy(gs_params.pch_pin, pch_pin);
  save_params();
  ge_state = eInit;
  if (gb_ready) {
    os_timer_arm(&gs_request_timer, 500, 0);
  }
  return true;
}

LOCAL
int ICACHE_FLASH_ATTR get_active() {
  if (gb_ready) {
    return gi_active;
  }
  return -1;
}

LOCAL
bool ICACHE_FLASH_ATTR set_active(int i_active) {
  gi_active = i_active;

  uint8_t ui_val;
  if (i_active < 20)
    ui_val = 0x00;
  else if (i_active <= 35)
    ui_val = 0x01;
  else if (i_active <= 50)
    ui_val = 0x03;
  else if (i_active <= 70)
    ui_val = 0x07;
  else if (i_active <= 90)
    ui_val = 0x0F;
  else if (i_active <= 110)
    ui_val = 0x1F;
  else if (i_active <= 125)
    ui_val = 0x3F;
  else if (i_active <= 135)
    ui_val = 0x7F;
  else
    ui_val = 0xFF;

  led_display(ui_val, LED_NONE, 0, gui_brightness);
  return true;
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

    os_free(gs_scan_ctx.pch_token);
    gs_scan_ctx.pch_token = NULL;
    gs_scan_ctx.ui_position = 0;
    ge_state = eMembersToken;

    uconf_var_set_int("active", i);
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
    "PureGym Activity Tracker",css,
    "<h1>uConfig interface</h1>"
    "<a href=\"info\">System Info</a><br/>"
    "<a href=\"https://github.com/kazkansouh/uConfig\">uConfig</a>");
const char* const msg_info_page =
  HTML_STYLE_DOC(
    "ESP8622",css,
    "<h1>System Info</h1>"
    "SDK info:<pre>%s</pre><br/>"
    "Saved parameters version: " xstr(PARAMS_MAGIC_NUMBER) "<br/>"
    "Free heap: %d bytes");
const char* const msg_css =
  "body {"
    "background:black;"
    "color:#80c0c0;"
    "font-family:Arial;"
  "}";

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

/* system info handler */
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

/* uConf action handlers */
LOCAL
bool ICACHE_FLASH_ATTR setwifi_action(uint8_t ui_args,
                                      const uconf_data_t* pu_args) {
  if (ui_args != 2) {
    return false;
  }

  os_printf("request to set wifi credentials to: %s:%s\n",
            pu_args[0].s,
            pu_args[1].s);

  connectStation(pu_args[0].s, os_strlen(pu_args[0].s),
                 pu_args[1].s, os_strlen(pu_args[1].s));
  return true;
}

LOCAL
bool ICACHE_FLASH_ATTR savebrightness_action(uint8_t ui_args,
                                             const uconf_data_t* pu_args) {
  if (ui_args != 0) {
    return false;
  }

  os_printf("request to store brightness to flash\n");

  if (gs_params.ui_brightness != gui_brightness) {
    gs_params.ui_brightness = gui_brightness;
    save_params();
  }

  return true;
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
    beacon_init(30000);
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
      beacon_init(30000);
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

LOCAL void ICACHE_FLASH_ATTR button_timer(void *arg) {
  if (gb_button_hyst) {
    // hysteresis timeout finished
    gb_button_hyst = false;
    // increment counter on button press
    gui_button_count++;
    // set timeout for action on button elapsed
    os_timer_arm(&gs_button_timer, 250, 0);
    // clear interrupt
    uint16_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
    // enable gpio interrupts
    ETS_GPIO_INTR_ENABLE();
  } else {
    os_printf("button was pressed %d times\n", gui_button_count);
    gui_button_count = 0;
  }
  switch (gui_button_count) {
  case 0:
    break;
  case 1:
    uconf_var_set_uint8("brightness",0xFF);
    break;
  case 2:
    uconf_var_set_uint8("brightness",0x80);
    break;
  case 3:
    uconf_var_set_uint8("brightness",0x20);
    break;
  case 4:
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
    uconf_var_set_uint8("brightness",0x0A);
    break;
  default:
    uconf_var_set_uint8("brightness",0x00);
    break;
  }
}

void ICACHE_FLASH_ATTR gpio_isr(void* arg) {
  // clear interrupt
  uint16_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
  GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
  // disable all interrupts on gpio pins
  ETS_GPIO_INTR_DISABLE();

  // mark in hysteresis
  gb_button_hyst = true;
  // set timeout for action on button elapsed
  os_timer_arm(&gs_button_timer, 150, 0);
}

const uconf_parameter_t wifiparams[] = {
  {"ssid", eString},
  {"password", eString},
};

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
      gs_params.ui_brightness = 0x80;
      save_params();
    }
    gui_brightness = gs_params.ui_brightness;
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

  // register uconf handlers
  uconf_register_read_uint8("brightness", &get_brightness);
  uconf_register_write_uint8("brightness", &set_brightness, true, true);

  uconf_register_read_cstr("email", &get_email);
  uconf_register_write_cstr("email", &set_email, true, false);

  uconf_register_read_cstr("pin", &get_pin);
  uconf_register_write_cstr("pin", &set_pin, true, false);

  uconf_register_read_int("active", &get_active);
  uconf_register_write_int("active", &set_active, false, true);

  uconf_register_action("setwifi", wifiparams, 2, setwifi_action);
  uconf_register_action("savebrightness", NULL, 0, savebrightness_action);

  // initialise handlers for http server
  http_register_init();
  uconf_register_http();
  http_register_handler("/favicon.ico", &fail_handler);
  http_register_handler("/info", &info_handler);
  http_register_handler("/", &root_handler);

  // setup timers
  os_timer_setfn(&gs_error_timer, error_timer , NULL);
  os_timer_setfn(&gs_request_timer, request_timer , NULL);
  os_timer_setfn(&gs_button_timer, button_timer , NULL);
#ifdef DEBUG_HEAP
  os_timer_setfn(&gs_debug_timer, debug_timer , NULL);
  os_timer_arm(&gs_debug_timer, 35000, 1);
#endif // DEBUG_HEAP

  // setup leds
  led_init();
  LED_POWERON;

  // start wifi connection process
  wifi_set_opmode( STATION_MODE );
  os_printf("Connecting as station\n");

  // setup interrupt on GPIO5
  // get GPIO5 pin as GPIO5
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
  // set GPIO as input mode
  GPIO_DIS_OUTPUT(GPIO_ID_PIN(5));
  // enable pullup
  PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO5_U);
  // disable all interrupts on gpio pins
  ETS_GPIO_INTR_DISABLE();
  // attach isr for gpio, second argument is parameter to pass to
  // function
  ETS_GPIO_INTR_ATTACH(gpio_isr, NULL);
  // set interrupt on rising edge
  gpio_pin_intr_state_set(GPIO_ID_PIN(5),GPIO_PIN_INTR_NEGEDGE);
  // enable gpio interrupts
  ETS_GPIO_INTR_ENABLE();
}
