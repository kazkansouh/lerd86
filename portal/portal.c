
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include <driver/uart.h>
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>

#include "sntp.h"

static const int pin_station = 12;
static const int pin_ap = 14;

static volatile os_timer_t oneshot_init_t;
static volatile os_timer_t error_timer_t;

LOCAL uint16_t server_timeover = 60*5;
LOCAL struct espconn masterconn;
LOCAL struct espconn apconn;

void ICACHE_FLASH_ATTR connectAP(void);

void ICACHE_FLASH_ATTR oneshot_init(void *arg) {
  wifi_set_opmode( STATION_MODE );
  os_printf("Connecting as station\n");
  wifi_station_connect();
  os_timer_arm(&error_timer_t, 7500, 0);
}

void ICACHE_FLASH_ATTR error_timer(void *arg) {
  //os_printf("Mark\n");
  //  system_deep_sleep(10000000);
  os_printf("ERROR Timout\n");
  wifi_station_disconnect();
  //  if (!wifi_station_disconnect()) {
    connectAP();
    //  }
}

const char ssid[] = "ESP8266_CONF";

void ICACHE_FLASH_ATTR connectStation(const char* ssid, const uint8 ssid_len,
				      const char* pass, const uint8 pass_len) {
  struct station_config stationConf;

  os_memset(&stationConf.ssid, 0x00, 64);
  os_memset(&stationConf.password, 0x00, 32);

  os_memcpy(&stationConf.ssid, ssid, ssid_len);
  os_memcpy(&stationConf.password, pass, pass_len);
  wifi_set_opmode( STATION_MODE );
  wifi_station_set_config(&stationConf);
  wifi_station_disconnect();
  wifi_station_connect();
  os_timer_arm(&error_timer_t, 15000, 0);
  //os_timer_arm(&oneshot_init_t,100,0);
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

  espconn_accept(&apconn);  
}


typedef enum {
  eSearching = 0,
  eLine1     = 1,
  eLine2     = 2,
  eLine3     = 3,
  eLine4     = 4,
  eLine5     = 5,
  eLine6     = 6,
  eLine7     = 7,
  eLine8     = 8,
  eLine9     = 9,
  eLine10    = 10
} EUartState;

typedef enum {
  eDelim = 4,
  eByte1 = 0,
  eByte2 = 1,
  eByte3 = 2,
  eByte4 = 3
} EUartSubState;

LOCAL unsigned int uart_state = eSearching;
LOCAL unsigned int uart_sub_state = eByte1;

#define ROWS 10

const uint32_t rows = ROWS;
uint32_t data[ROWS];
uint32_t data_new[ROWS];

void ICACHE_FLASH_ATTR ///////
uart_recvTask(os_event_t *events) {
  if(events->sig == 0) {
    uint8 fifo_len = (READ_PERI_REG(UART_STATUS(UART0))>>UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT;
    uint8 d_tmp = 0;
    uint8 idx=0;
    for(idx=0;idx<fifo_len;idx++) {
      d_tmp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;

      switch (uart_state) {
      case eSearching:
	if (d_tmp == 0xFF) {
	  uart_state = eLine1;
	  uart_sub_state = eDelim;
	}
	break;
      case eLine1:
      case eLine2:
      case eLine3:
      case eLine4:
      case eLine5:
      case eLine6:
      case eLine7:
      case eLine8:
      case eLine9:
      case eLine10:
	switch (uart_sub_state) {
	case eDelim:
	 if (d_tmp == 0xFF) {
	  uart_sub_state = eByte4;
	 }
	 break;
	case eByte4:
	case eByte3:
	case eByte2:
	case eByte1:
	  data_new[uart_state - 1] &= ~(((uint32_t)0xFF) << (8 * uart_sub_state));
	  data_new[uart_state - 1] |= ((uint32_t)d_tmp) << (8 * uart_sub_state);
	  //os_printf("%d-", d_tmp);
	  if (uart_sub_state == eByte1) {
	    uart_sub_state = eDelim;
	    data_new[uart_state - 1] = data_new[uart_state - 1] >> 10;
	    //os_printf("#%d\n", uart_state);
	    if (uart_state != eLine10) {
	      uart_state++;
	    } else {
	      os_memcpy(data,data_new,sizeof(uint32_t)*rows);
	      uart_state = eSearching;
	    }
	  } else {
	    uart_sub_state--;
	  }
	  break;
	default:
	  // error case
	  uart_state = eSearching;
	}
	break;
      default:
	// error case
	uart_state = eSearching;
      }

      /*
      switch (d_tmp) {
      case '1':
	os_printf("1: Connecting\n");
	connect();
	break;
      case '2':
	os_printf("2: Disconnecting\n");
	break;
      default:
	os_printf("Unknown Command!\n");
      }
      */
    }
    WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR|UART_RXFIFO_TOUT_INT_CLR);
    uart_rx_intr_enable(UART0);
  }
}


LOCAL void ICACHE_FLASH_ATTR
shell_tcp_disconcb(void *arg) {
    struct espconn *pespconn = (struct espconn *) arg;

    //espconn_delete(arg);
    //os_printf("tcp connection %u disconnected\n", pespconn->link_cnt);
}

const char* msg_error = "HTTP/1.1 404\r\n\r\n<html><head><title>ERROR</title></head><body><h1>Error 404</h1></body></html>";
const char* msg_welcome = 
  "HTTP/1.1 200\r\n\r\n"
  "<html><head><title>ESP8622</title></head><body><h1>Hello I am ESP!</h1><a href=\"next.html\">Info</a><br/><a href=\"toggle.html\">Click Me</a><br/><a href=\"state.html\">State</a><br/><a href=\"wifi.html\">Configure WiFi Details</a></body></html>";
const char* msg_next_pt1 = 
  "HTTP/1.1 200\r\n\r\n"
  "<html><head><title>ESP8622</title></head><body><h1>My Details</h1><pre>";
const char* msg_next_pt2 = 
  "</pre></body></html>";
const char* msg_toggle = 
  "HTTP/1.1 200\r\n\r\n"
  "<html><head><title>ESP8622 AJAX</title></head><body><script>function rListen() {var jsonResponse = JSON.parse(this.responseText); document.getElementById('blah').style.backgroundColor = jsonResponse.color;} function request() {var oReq = new XMLHttpRequest(); oReq.addEventListener(\"load\", rListen); oReq.open(\"GET\", \"led.json\"); oReq.send();}</script><div id=\"blah\" style=\"height: 100px; width: 100px; background-color: powderblue;\" onclick=\"request()\" >Hi</div></body></html>";
const char* json_on = 
  "HTTP/1.1 200\r\n\r\n"
  "{\"color\": \"green\"}";
const char* json_off = 
  "HTTP/1.1 200\r\n\r\n"
  "{\"color\": \"red\"}";
const char* msg_state = 
  "HTTP/1.1 200\r\n\r\n"
  "<html><head><title>ESP8622 Data State</title></head><body><svg id='display' width='800' height='400'></svg><script>for (i = 0; i < 10; i++) {for (j = 0; j < 20; j++) {var c = document.createElementNS('http://www.w3.org/2000/svg', 'circle');c.setAttribute('id', i + '-' + j);c.setAttribute('cx', j * 40 + 20);c.setAttribute('cy', i * 40 + 20);c.setAttribute('r', '20');c.setAttribute('fill', 'yellow');document.getElementById('display').appendChild(c);}}; function rListen() {var jsonResponse = JSON.parse(this.responseText); document.getElementById('result').textContent = jsonResponse.message;} function rUpdate() {var jsonResponse = JSON.parse(this.responseText); for (var i = 0; i < jsonResponse.length; i++) { for (var j = 0; j < jsonResponse[i].length; j++) {if (jsonResponse[i][j] == 0) {document.getElementById(i + '-' + j).setAttribute('fill','grey');} else {document.getElementById(i + '-' + j).setAttribute('fill','blue');}}}} function request(uri,handle) {var oReq = new XMLHttpRequest(); oReq.addEventListener(\"load\", handle); oReq.open(\"GET\", uri); oReq.send();} window.setInterval(\"request('state.json',rUpdate)\",250)</script><div><button type=\"button\" onclick=\"request('zero.json',rListen)\">Zero</button><button type=\"button\" onclick=\"request('settime.json',rListen)\">SetTime</button><button type=\"button\" onclick=\"request('scroll.json?scroll=1',rListen)\">Enable Scroll</button><button type=\"button\" onclick=\"request('scroll.json?scroll=0',rListen)\">Disable Scroll</button><input type='range' id='speed' value='4' onchange=\"request('speed.json?speed=' + (50 + (this.value * 49)),rListen)\"></input></div><br><div id=\"result\">select a command</div></body></html>";
const char* json_ok = 
  "HTTP/1.1 200\r\n\r\n"
  "{\"message\": \"ok\"}";
const char* json_fail = 
  "HTTP/1.1 200\r\n\r\n"
  "{\"message\": \"fail\"}";
const char* json_state = 
  "HTTP/1.1 200\r\n\r\n";
const char* msg_setwifi_page = 
  "HTTP/1.1 200\r\n\r\n"
  "<html><head><title>ESP8622 SetWiFi Details</title></head><body><form action=\"setwifi.html\" method=\"get\">SSID: <input type=\"text\" name=\"ssid\"></input><br>Password: <input type=\"text\" name=\"password\"></input><br><input type=\"submit\">Submit</input></form></body></html>";
const char* json_msg_pt1 = 
  "HTTP/1.1 200\r\n\r\n"
  "{\"message\": \"";
const char* json_msg_pt2 = 
  "\"}";



#define tcp_connTaskPrio        1
#define tcp_connTaskQueueLen    10
os_event_t tcp_connTaskQueue[tcp_connTaskQueueLen];

LOCAL void ICACHE_FLASH_ATTR
tcpclient_sent_cb(void *arg) {
  struct espconn *pespconn = (struct espconn *) arg;
  
  //  os_printf("tcp connection %u written\n", pespconn->link_cnt);
  system_os_post(tcp_connTaskPrio, 1, (uint32_t)arg);
}

/*

GET /favicon.ico HTTP/1.1\r\n
Host: 192.168.0.6\r\n
Connection: keep-alive
Pragma: no-cache
Cache-Control: no-cache
User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.84 Safari/537.36
Accept: *\/*
Referer: http://192.168.0.6/
Accept-Encoding: gzip, deflate, sdch
Accept-Language: en-GB,en-US;q=0.8,en;q=0.6

*/

LOCAL char* ICACHE_FLASH_ATTR strnchr(char *pdata, char chr, unsigned short len) {
  unsigned short i = 0;
  while (i < len) {
    if (pdata[i] == chr) {
      return pdata + i;
    }
    i++;
  }
  return NULL;
}

bool inline isSpace(const char c) {
  switch (c) {
  case ' ':
  case '\t':
    return true;
  default:
    return false;
  }
}

bool inline isDigit(const char c) {
  return c >= '0' && c <= '9';
}

bool inline isAlpha(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'z');
}

bool inline isUpper(const char c) {
  return (c >= 'A' && c <= 'z');
}

#include <limits.h>


/*
 * Convert a string to a long integer.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 */
long
ICACHE_FLASH_ATTR
strtol(const char *nptr, char **endptr, register int base)
{
	register const char *s = nptr;
	register unsigned long acc;
	register int c;
	register unsigned long cutoff;
	register int neg = 0, any, cutlim;

	/*
	 * Skip white space and pick up leading +/- sign if any.
	 * If base is 0, allow 0x for hex and 0 for octal, else
	 * assume decimal; if base is already 16, allow 0x.
	 */
	do {
		c = *s++;
	} while (isSpace(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;

	/*
	 * Compute the cutoff value between legal numbers and illegal
	 * numbers.  That is the largest legal value, divided by the
	 * base.  An input number that is greater than this value, if
	 * followed by a legal input character, is too big.  One that
	 * is equal to this value may be valid or not; the limit
	 * between valid and invalid numbers is then based on the last
	 * digit.  For instance, if the range for longs is
	 * [-2147483648..2147483647] and the input base is 10,
	 * cutoff will be set to 214748364 and cutlim to either
	 * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
	 * a value > 214748364, or equal but the next digit is > 7 (or 8),
	 * the number is too big, and we will return a range error.
	 *
	 * Set any if any `digits' consumed; make it negative to indicate
	 * overflow.
	 */
	cutoff = neg ? -(unsigned long)LONG_MIN : LONG_MAX;
	cutlim = cutoff % (unsigned long)base;
	cutoff /= (unsigned long)base;
	for (acc = 0, any = 0;; c = *s++) {
		if (isDigit(c))
			c -= '0';
		else if (isAlpha(c))
			c -= isUpper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
			any = -1;
		else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = neg ? LONG_MIN : LONG_MAX;
		//errno = ERANGE;
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char *) (any ? s - 1 : nptr);
	return (acc);
}


LOCAL void ICACHE_FLASH_ATTR
tcpclient_recv_cb(void *arg, char *pdata, unsigned short len) {
  struct espconn *pespconn = (struct espconn *) arg;
  
  /*os_printf("tcp connection %u received: ", pespconn->link_cnt);
  uart0_tx_buffer(pdata, len);
  os_printf("\n");
  */
  char* eol = strnchr(pdata, 0x0D, len);
  if (eol) {
    if (os_strncmp(pdata, "GET ", 4) == 0) {
      char* start = pdata += 4;
      char* space = strnchr(start, 0x20, eol - pdata - 4);
      /* os_printf("requested: '"); */
      unsigned short reqsize = space - start;
      /* uart0_tx_buffer(start, reqsize); */
      /* os_printf("'\n"); */

      if (os_strncmp("/",start, reqsize) == 0) {
	system_os_post(tcp_connTaskPrio, 2, (uint32_t)arg);
	return;
      } else if (os_strncmp("/next.html",start, reqsize) == 0) {
	system_os_post(tcp_connTaskPrio, 3, (uint32_t)arg);
	return;
      } else if (os_strncmp("/toggle.html",start, reqsize) == 0) {
	system_os_post(tcp_connTaskPrio, 4, (uint32_t)arg);
	return;
      } else if (os_strncmp("/led.json",start, reqsize) == 0) {
	system_os_post(tcp_connTaskPrio, 5, (uint32_t)arg);
	return;
      } else if (os_strncmp("/state.html",start, reqsize) == 0) {
	system_os_post(tcp_connTaskPrio, 6, (uint32_t)arg);
	return;
      } else if (os_strncmp("/zero.json",start, reqsize) == 0) {
	uart0_write_char(0xFF);
	uart0_write_char(0x00);
	system_os_post(tcp_connTaskPrio, 7, (uint32_t)arg);
	return;
      } else if (os_strncmp("/settime.json",start, reqsize) == 0) {
	if (!requestTime(arg)) {
          system_os_post(tcp_connTaskPrio, 10, (uint32_t)arg);
        }
	return;
      } else if (os_strncmp("/scroll.json?scroll=1",start, reqsize) == 0) {
	uart0_write_char(0xFF);
	uart0_write_char(0x01);
	system_os_post(tcp_connTaskPrio, 7, (uint32_t)arg);
	return;
      } else if (os_strncmp("/scroll.json?scroll=0",start, reqsize) == 0) {
	uart0_write_char(0xFF);
	uart0_write_char(0x02);
	system_os_post(tcp_connTaskPrio, 7, (uint32_t)arg);
	return;
      } else if (reqsize > 18 && os_strncmp("/speed.json?speed=",start, 18) == 0) {
	uint16 speed;
	start+=18;
	reqsize-=18;
	speed = strtol(start, NULL, 10);

	uart0_write_char(0xFF);
	uart0_write_char(0x03);
	uart0_write_char(speed >> 8);
	uart0_write_char(speed & 0xFF);
	system_os_post(tcp_connTaskPrio, 7, (uint32_t)arg);
	return;
      } else if (os_strncmp("/state.json",start, reqsize) == 0) {
	system_os_post(tcp_connTaskPrio, 8, (uint32_t)arg);
	return;
      } else if (os_strncmp("/wifi.html",start, reqsize) == 0) {
	system_os_post(tcp_connTaskPrio, 9, (uint32_t)arg);
	return;
      } else if (reqsize >= 14 && os_strncmp("/setwifi.html?",start, 14) == 0) {
	char* ssid = NULL;
	uint8 ssid_len = 0;
	char* pwd = NULL;
	uint8 pwd_len = 0;
	start += 14;
	reqsize -= 14;

	while (reqsize) {
	  if (*start == '&') {
	    reqsize--;
	    start++;
	  } else {
	    if (reqsize >= 5 && os_strncmp("ssid=",start,5) == 0) {
	      char* next;
	      start += 5;
	      reqsize -= 5;
	      ssid = start;
	      next = strnchr(start, '&', reqsize);
	      if (next == NULL) {
		ssid_len = reqsize;
		reqsize = 0;
	      } else {
		ssid_len = next - start;
		reqsize -= ssid_len;
		start += ssid_len;
	      }
	    } else if (reqsize >= 9 && os_strncmp("password=",start,9) == 0) {
	      char* next;
	      start += 9;
	      reqsize -= 9;
	      pwd = start;
	      next = strnchr(start, '&', reqsize);
	      if (next == NULL) {
		pwd_len = reqsize;
		reqsize = 0;
	      } else {
		pwd_len = next - start;
		reqsize -= pwd_len;
		start += pwd_len;
	      }
	    } else {
	      reqsize--;
	      start++;
	    }
	  }
	}

	connectStation(ssid, ssid_len, pwd, pwd_len);
	
	system_os_post(tcp_connTaskPrio, 2, (uint32_t)arg);
	return;
      }
    }
  }

  system_os_post(tcp_connTaskPrio, 0, (uint32_t)arg);
}

LOCAL void ICACHE_FLASH_ATTR
tcpserver_connectcb(void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;

    //os_printf("tcp connection %u established\n", pespconn->link_cnt);

    espconn_regist_recvcb(pespconn, tcpclient_recv_cb);
    // espconn_regist_reconcb(pespconn, tcpserver_recon_cb);
    espconn_regist_disconcb(pespconn, shell_tcp_disconcb);
    espconn_regist_sentcb(pespconn, tcpclient_sent_cb);

    //    system_os_post(tcp_connTaskPrio, 0, (uint32_t)arg);
}

const char* pch_space = "-";
const char* pch_bull  = "+";

void ICACHE_FLASH_ATTR ///////
tcp_connTask(os_event_t *events) {
  char buffer[2048];
  uint16 x = 0;
  struct espconn *pespconn = (struct espconn *)events->par;
  int i, j;

  switch (events->sig) {
  case 1:
    espconn_disconnect(pespconn);
    break;
  case 2:
    // send welome
    espconn_sent(pespconn, (uint8*)msg_welcome, os_strlen(msg_welcome));
    //os_printf("tcp connection %u writting welcome\n", pespconn->link_cnt);
    break;
  case 3:
    // send details
    os_memcpy(buffer, msg_next_pt1, os_strlen(msg_next_pt1));
    x = os_strlen(msg_next_pt1);
    os_memcpy(buffer+x, system_get_sdk_version(), os_strlen(system_get_sdk_version()));
    x += os_strlen(system_get_sdk_version());
    os_memcpy(buffer+x,msg_next_pt2, os_strlen(msg_next_pt2));
    x += os_strlen(msg_next_pt2);
    espconn_sent(pespconn, (uint8*)buffer, x);
    break;
  case 4:
    // send toggle
    espconn_sent(pespconn, (uint8*)msg_toggle, os_strlen(msg_toggle));
    //os_printf("tcp connection %u writting toggle\n", pespconn->link_cnt);
    break;
  case 5:
    // send toggle
    if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & (1 << pin_station)) {
      // set gpio low
      gpio_output_set(0, (1 << pin_station), 0, 0);
      espconn_sent(pespconn, (uint8*)json_off, os_strlen(json_off));
    } else {
      // set gpio high
      gpio_output_set((1 << pin_station), 0, 0, 0);
      espconn_sent(pespconn, (uint8*)json_on, os_strlen(json_on));
    }
    //os_printf("tcp connection %u writting toggle\n", pespconn->link_cnt);
    break;
  case 6:
    // send state
    espconn_sent(pespconn, (uint8*)msg_state, os_strlen(msg_state));
    break;
  case 7:
    espconn_sent(pespconn, (uint8*)json_ok, os_strlen(json_ok));
    break;
  case 8:
    os_memcpy(buffer, json_state, os_strlen(json_state));
    x = os_strlen(json_state);

    buffer[x++] = '[';

    for (i = 0 ; i < rows ; i++) {
      if (i > 0) {
	buffer[x++] = ',';
      }
      buffer[x++] = '[';
      for (j = 0 ; j < 20; j++) {
	if (j > 0) {
	  buffer[x++] = ',';
	}
	if (data[i] & (1 << j)) {
	  buffer[x++] = '1'; 
	} else {
	  buffer[x++] = '0';
	}
      }
      buffer[x++] = ']';
    }
    
    buffer[x++] = ']';
    espconn_sent(pespconn, (uint8*)buffer, x);
    break;
  case 9:
    espconn_sent(pespconn, (uint8*)msg_setwifi_page, os_strlen(msg_setwifi_page));
    break;
  case 10:
    espconn_sent(pespconn, (uint8*)json_fail, os_strlen(json_fail));
    break;
  case 11: 
    {
      uint32_t hours   = (uint32_t)pespconn->reverse / 3600;
      uint32_t minutes = ((uint32_t)pespconn->reverse - (hours * 3600)) / 60;
      uint32_t seconds = (uint32_t)pespconn->reverse - (hours * 3600) - (minutes * 60);

      os_sprintf(buffer, "%s%02d:%02d:%02d%s", json_msg_pt1, hours, minutes, seconds, json_msg_pt2);
      espconn_sent(pespconn, (uint8*)buffer, os_strlen(buffer));
    }
    break;
  case 0:
  default:
    // send error
    espconn_sent(pespconn, (uint8*)msg_error, os_strlen(msg_error));
    //    os_printf("tcp connection %u writting error page\n", pespconn->link_cnt);
    break;
  }
}


void wifi_handle_event_cb(System_Event_t *evt);

void ICACHE_FLASH_ATTR user_init() {

  // init gpio sussytem
  gpio_init();

  // configure MTDI to be GPIO12, set as output
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12); 
  // configure MTMS to be GPIO14, set as output
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14); 
  gpio_output_set(0, 0, (1 << pin_station) | (1 << pin_ap), 0);

  wifi_set_event_handler_cb(&wifi_handle_event_cb);

  masterconn.type = ESPCONN_TCP;
  masterconn.state = ESPCONN_NONE;
  masterconn.proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
  masterconn.proto.tcp->local_port = 80;

  apconn.type = ESPCONN_TCP;
  apconn.state = ESPCONN_NONE;
  apconn.proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
  apconn.proto.tcp->local_port = 80;

  espconn_regist_connectcb(&apconn, tcpserver_connectcb);
  espconn_regist_connectcb(&masterconn, tcpserver_connectcb);
  //espconn_accept(&masterconn);
  espconn_regist_time(&apconn, server_timeover, 0);
  espconn_regist_time(&masterconn, server_timeover, 0);

  uart_init(BIT_RATE_57600, uart_recvTask);
  system_os_task(tcp_connTask, tcp_connTaskPrio, tcp_connTaskQueue, tcp_connTaskQueueLen);
  
  os_printf("\nSDK version: %s \n", system_get_sdk_version());
  uart0_sendStr("Hello World\n");

  os_timer_setfn(&oneshot_init_t, oneshot_init , NULL);
  os_timer_setfn(&error_timer_t, error_timer , NULL);
  os_timer_arm(&oneshot_init_t,100,0);

  data[0] = 0xFFFFFFF0;
  data[1] = 0xFFFFFFF1;
  data[2] = 0xFFFFFFF2;
  data[3] = 0xFFFFFFF3;
  data[4] = 0xFFFFFFF4;
  data[5] = 0xFFFFFFF5;
  data[6] = 0xFFFFFFF6;
  data[7] = 0xFFFFFFF7;
  data[8] = 0xFFFFFFF8;
  data[9] = 0xFFFFFFF9;
}


void wifi_handle_event_cb(System_Event_t *evt) {
  os_timer_disarm(&error_timer_t);
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
  EVENT_MAX
  */

  switch (evt->event) {
  case EVENT_STAMODE_CONNECTED:
    // wait 3 seconds to get IP address
    os_timer_arm(&error_timer_t,20000,0);
    gpio_output_set(0, (1 << pin_station) | (1 << pin_ap), 0, 0);
    //connectStation("sss",4,"ddd",4);
    break;
  case EVENT_STAMODE_GOT_IP:
    // we are good to start services here
    gpio_output_set((1 << pin_station), 0, 0, 0);
    if (apconn.state == ESPCONN_LISTEN) {
      espconn_delete(&apconn);
    }
    espconn_accept(&masterconn);
    break;
  case EVENT_STAMODE_DISCONNECTED:
    espconn_delete(&masterconn);
  case EVENT_STAMODE_AUTHMODE_CHANGE:
  case EVENT_STAMODE_DHCP_TIMEOUT:
    // start AP mode
    connectAP();
    break;
  /* case EVENT_SOFTAPMODE_STACONNECTED: */
  /*   gpio_output_set((1 << pin_ap), 0, 0, 0); */
  /*   espconn_accept(&apconn); */
  /*   break; */
  /* case EVENT_SOFTAPMODE_STADISCONNECTED: */
  /*   gpio_output_set(0, (1 << pin_ap), 0, 0); */
  /*   espconn_delete(&apconn); */
  /*   break; */
  }
}
