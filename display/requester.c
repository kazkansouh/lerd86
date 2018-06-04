// Format of responses should match:
// https://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html

#include <c_types.h>
#include <osapi.h>
#include <ip_addr.h>
#include <espconn.h>
#include <mem.h>
#include <user_interface.h>
#include <stdlib.h>
#include "requester.h"
#include "stringutil.h"
#include <strings.h>

#define TIMEOUT 5
#define MAX_HEADER_LENGTH 512

typedef struct espconn espconn_t;

typedef enum {
  OPEN_CONNECTION = 0x00,
  SEND_REQUEST,
  CLOSE_CONNECTION,
} e_task_t;

typedef enum {
  STATUS_LINE,
  LINE_CR,
  LINE_LF,
  HEADER_LINE,
  END_HEADER_CR,
  IN_BODY,
  IN_ERROR,
} e_response_state_t;

typedef struct {
  // internal status of request, also used to select posted task
  e_task_t e_status;
  // tracks the status of parsing response, e.g. headers/body. only
  // valid when e_status == REQUEST_SENT, i.e. forms a hsm.
  e_response_state_t e_response_state;
  // host of request
  const char* pch_host;
  // resource requested on host
  const char* pch_resource;
  // any data to append to request (e.g. post data)
  const char* pch_data;
  // callback for status updates on process
  requester_status_callback_t f_status;
  // callback when response headers are consumed
  requester_header_callback_t f_header;
  // callback for received packets
  requester_response_callback_t f_response;
  // context passed to callbacks
  void* ctx;
  // cookie jar
  requester_cookies_t* s_jar;
  // buffer for storing lines of the header before parsing
  uint32_t ui_line_buffer;
  char pch_line_buffer[MAX_HEADER_LENGTH];
  // response code extracted from status line of response
  uint32_t ui_response_code;
} requester_context_t;

#define STATUS(c) ((requester_context_t*)c->reverse)->e_status

LOCAL void connectcb(espconn_t *conn);
LOCAL void receivecb(espconn_t *conn, char *pdata, unsigned short len);
LOCAL void disconnectcb(espconn_t *conn);
LOCAL void sentcb(espconn_t *conn);
LOCAL void dnscb(const char *name, ip_addr_t *ipaddr, espconn_t *conn);
LOCAL void reconncb(espconn_t *conn, sint8 err);

LOCAL espconn_t s_requester_conn = {0};
LOCAL esp_tcp s_requester_tcp = {0};
LOCAL ip_addr_t s_ip_addr;
LOCAL requester_context_t s_context;
LOCAL os_timer_t timeout_timer_t;

#define requester_TaskPrio     2
#define requester_TaskQueueLen 10

LOCAL os_event_t requester_TaskQueue[requester_TaskQueueLen];
LOCAL void requester_Task(os_event_t *events);
LOCAL void timeout_timer(void *arg);

bool ICACHE_FLASH_ATTR request(const char* const pch_host,
                               const uint16_t ui_port,
                               const char* const pch_resource,
                               requester_cookies_t* const s_cookies,
                               const requester_status_callback_t f_status,
                               const requester_header_callback_t f_header,
                               const requester_response_callback_t f_response,
                               void* const ctx) {
  system_os_task(requester_Task,
                 requester_TaskPrio,
                 requester_TaskQueue,
                 requester_TaskQueueLen);
  os_timer_setfn(&timeout_timer_t, timeout_timer, &s_requester_conn);

  if (s_requester_conn.state != ESPCONN_NONE &&
      s_requester_conn.state != ESPCONN_CLOSE) {
    os_printf("ERROR, connection is dirty\n");
    return false;
  }

  os_memset(&s_requester_conn, 0x00, sizeof(espconn_t));
  os_memset(&s_requester_tcp, 0x00, sizeof(esp_tcp));
  os_memset(&s_context, 0x00, sizeof(requester_context_t));
  s_requester_conn.type = ESPCONN_TCP;
  s_requester_conn.state = ESPCONN_NONE;
  s_requester_conn.proto.tcp = &s_requester_tcp;
  s_requester_conn.proto.tcp->local_port = espconn_port();
  s_requester_conn.proto.tcp->remote_port = ui_port;
  s_requester_conn.reverse = &s_context;
  s_context.pch_host = pch_host;
  s_context.pch_resource = pch_resource;
  s_context.f_status = f_status;
  s_context.f_header = f_header;
  s_context.f_response = f_response;
  s_context.s_jar = s_cookies;
  s_context.e_response_state = STATUS_LINE;
  s_context.ctx = ctx;

  espconn_regist_connectcb(&s_requester_conn,
                           (espconn_connect_callback)connectcb);
  espconn_regist_reconcb(&s_requester_conn,
                           (espconn_reconnect_callback)reconncb);
  espconn_regist_time(&s_requester_conn, TIMEOUT, 0);

  err_t e = espconn_gethostbyname(&s_requester_conn,
                                  pch_host,
                                  &s_ip_addr,
                                  (dns_found_callback)dnscb);
  if (e == ESPCONN_ARG) {
    return false;
  } else if (e == ESPCONN_OK) {
    dnscb(pch_host, &s_ip_addr, &s_requester_conn);
  }
  return true;
}

LOCAL void ICACHE_FLASH_ATTR
dnscb(const char *name, ip_addr_t *ipaddr, espconn_t *conn) {
  requester_context_t *ctx = (requester_context_t*)conn->reverse;
  os_printf("callback!\n");
  if (ipaddr) {
    os_printf("ip for %s is %08X\n", name, ipaddr->addr);
    os_memcpy(conn->proto.tcp->remote_ip, &ipaddr->addr, 4);
    STATUS(conn) = OPEN_CONNECTION;
    system_os_post(requester_TaskPrio, 0, (uintptr_t)conn);
  } else {
    os_printf("failed to find ip addr\n");
    ctx->f_status(ctx->ctx, REQ_DNS_LOOKUP_FAIL);
  }
}

#define REQUEST_BUFFER_SIZE 500
LOCAL const char* const pch_request = "GET %s HTTP/1.1\r\nHost: %s\r\n";
LOCAL const char* const pch_cookie = "Cookie: ";

LOCAL void ICACHE_FLASH_ATTR
requester_Task(os_event_t *events) {
  espconn_t *pespconn = (espconn_t *)events->par;
  requester_context_t *ctx = (requester_context_t*)pespconn->reverse;
  char ch_buffer[REQUEST_BUFFER_SIZE];
  int i;

  switch (ctx->e_status) {
  case OPEN_CONNECTION:
    // set buffer to 4KiB
    if (!espconn_secure_set_size(0x01, 0x1000)) {
      os_printf("failed to set buffer size\n");
    }
    ctx->f_status(ctx->ctx,
                  espconn_secure_connect(pespconn) == 0 ?
                  REQ_CONN_CONNECTED : REQ_CONN_FAIL);
    break;
  case SEND_REQUEST:
    i = os_sprintf(ch_buffer, pch_request, ctx->pch_resource, ctx->pch_host);
    for (int j = 0; ctx->s_jar && j < ctx->s_jar->ui_size; j++) {
      // "Cookie: name=value\r\n"
      if (i +
          os_strlen(pch_cookie) +
          os_strlen(ctx->s_jar->s_cookies[j].pch_name) +
          os_strlen(ctx->s_jar->s_cookies[j].pch_value) +
          3 <= REQUEST_BUFFER_SIZE) {
        os_strcpy(ch_buffer + i, pch_cookie);
        i += os_strlen(pch_cookie);
        os_strcpy(ch_buffer + i, ctx->s_jar->s_cookies[j].pch_name);
        i += os_strlen(ctx->s_jar->s_cookies[j].pch_name);
        ch_buffer[i++] = '=';
        os_strcpy(ch_buffer + i, ctx->s_jar->s_cookies[j].pch_value);
        i += os_strlen(ctx->s_jar->s_cookies[j].pch_value);
        ch_buffer[i++] = '\r';
        ch_buffer[i++] = '\n';
      } else {
        // space error
        ctx->f_status(ctx->ctx, REQ_OUT_OF_MEM);
        ctx->e_status = CLOSE_CONNECTION;
        system_os_post(requester_TaskPrio, 0, (uintptr_t)pespconn);
        return;
      }
    }
    // crlf
    if (i + 2 <= REQUEST_BUFFER_SIZE) {
      ch_buffer[i++] = '\r';
      ch_buffer[i++] = '\n';
    } else {
      // space error
      ctx->f_status(ctx->ctx, REQ_OUT_OF_MEM);
      ctx->e_status = CLOSE_CONNECTION;
      system_os_post(requester_TaskPrio, 0, (uintptr_t)pespconn);
      return;
    }
    if (espconn_secure_send(pespconn, (uint8_t*)ch_buffer, i) != 0) {
      ctx->f_status(ctx->ctx, REQ_CONN_FAIL);
    }
    break;
  case CLOSE_CONNECTION:
    os_timer_disarm(&timeout_timer_t);
    espconn_secure_disconnect(pespconn);
  }
}

LOCAL void ICACHE_FLASH_ATTR
reconncb(espconn_t *conn, sint8 err) {
  requester_context_t *ctx = (requester_context_t*)conn->reverse;
  os_printf("re-connect ssl cb with %d, status of conn %d\n", err, conn->state);
  // if err == 8, then otherside closed connection
  if (err == 8) {
    ctx->f_status(ctx->ctx, REQ_CONN_CLOSED);
  } else {
    ctx->f_status(ctx->ctx, REQ_CONN_FAIL);
  }
}

LOCAL void ICACHE_FLASH_ATTR
connectcb(espconn_t *conn) {
  os_printf("connected ssl cb\n");
  espconn_regist_recvcb(conn, (espconn_recv_callback)receivecb);
  espconn_regist_disconcb(conn, (espconn_connect_callback)disconnectcb);
  espconn_regist_sentcb(conn, (espconn_sent_callback)sentcb);

  STATUS(conn) = SEND_REQUEST;
  system_os_post(requester_TaskPrio, 0, (uintptr_t)conn);
}

LOCAL bool ICACHE_FLASH_ATTR process_status_line(requester_context_t *ctx) {
  if (ctx->ui_line_buffer >= 9 &&
      os_strncmp("HTTP/1.1 ", ctx->pch_line_buffer, 9) == 0) {
    char* ptr = ctx->pch_line_buffer + 9;
    uint32_t len = ctx->ui_line_buffer - 9;
    char* space = strnchr(ptr, ' ', len);

    if (space) {
      *space = '\0';
      char *endcode = NULL;
      long int i = strtol(ptr, &endcode, 10);
      if (space == endcode) {
        ctx->ui_response_code = i;
        return true;
      }
    }
  }
  return false;
}

LOCAL bool ICACHE_FLASH_ATTR process_header_line(requester_context_t *ctx) {
  char* colon = strnchr(ctx->pch_line_buffer, ':', ctx->ui_line_buffer);
  if (colon) {
    *colon = '\0';
    if (os_strcmp("Set-Cookie", ctx->pch_line_buffer) == 0) {
      uint32_t len = ctx->ui_line_buffer - (colon - ctx->pch_line_buffer);
      if (len > 0) {
        do {
          len--;
          colon++;
        } while(len > 0 && *colon == ' ');
        char* cookie_name = colon;
        char* cookie_name_end = colon;
        while (*cookie_name_end != '=' && len > 0) {
          cookie_name_end++;
          len--;
        }
        if (len > 0) {
          len--;
          char* cookie_value = cookie_name_end + 1;
          char* cookie_value_end = cookie_value;
          while (*cookie_value_end != ' ' &&
                 *cookie_value_end != ';' &&
                 len > 0) {
            cookie_value_end++;
            len--;
          }
          *cookie_name_end = '\0';
          if (len > 0) {
            *cookie_value_end = '\0';
          }
          requester_cookie_add(ctx->s_jar,
                               cookie_name,
                               cookie_name_end - cookie_name,
                               cookie_value,
                               cookie_value_end - cookie_value);
        }
      }
    }
    return true;
  }
  return false;
}

LOCAL void ICACHE_FLASH_ATTR
receivecb(espconn_t *conn, char *pdata, unsigned short len) {
  requester_context_t *ctx = (requester_context_t*)conn->reverse;
  /*
   * Response      = Status-Line               ; Section 6.1
   *                 *(( general-header        ; Section 4.5
   *                  | response-header        ; Section 6.2
   *                  | entity-header ) CRLF)  ; Section 7.1
   *                 CRLF
   *                 [ message-body ]          ; Section 7.2
   *
   * Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
   */

  os_timer_arm(&timeout_timer_t, 500, 0);

  while (len) {
    switch (ctx->e_response_state) {
    case STATUS_LINE   :
      if (*pdata == '\r') {
        ctx->e_response_state = LINE_CR;
        // process line
        if (!process_status_line(ctx)) {
          ctx->e_response_state = IN_ERROR;
        }
      } else {
        ctx->pch_line_buffer[ctx->ui_line_buffer] = *pdata;
        ctx->ui_line_buffer++;
      }
      break;
    case LINE_CR       :
      if (*pdata == '\n') {
        ctx->e_response_state = LINE_LF;
        os_memset(ctx->pch_line_buffer, 0x00, MAX_HEADER_LENGTH);
        ctx->ui_line_buffer = 0;
      } else {
        ctx->e_response_state = IN_ERROR;
      }
      break;
    case LINE_LF       :
      if (*pdata == '\r') {
        ctx->e_response_state = END_HEADER_CR;
        break;
      } else {
        ctx->e_response_state = HEADER_LINE;
      }
      // ! no break
    case HEADER_LINE   :
      if (*pdata == '\r') {
        ctx->e_response_state = LINE_CR;
        // process line
        if (!process_header_line(ctx)) {
          ctx->e_response_state = IN_ERROR;
        }
      } else {
        ctx->pch_line_buffer[ctx->ui_line_buffer] = *pdata;
        ctx->ui_line_buffer++;
      }
      break;
    case END_HEADER_CR :
      if (*pdata == '\n') {
        ctx->e_response_state = IN_BODY;
        os_memset(ctx->pch_line_buffer, 0x00, MAX_HEADER_LENGTH);
        ctx->ui_line_buffer = 0;
        if (!ctx->f_header(ctx->ctx, ctx->ui_response_code)) {
          ctx->e_response_state = IN_ERROR;
        }
      } else {
        ctx->e_response_state = IN_ERROR;
      }
      break;
    case IN_BODY       :
      // remainder of buffer is handled by application
      if (ctx->f_response(ctx->ctx, pdata, len)) {
        return;
      }
      ctx->e_response_state = IN_ERROR;
      // ! no break
    case IN_ERROR      :
      os_printf("invalid response received\n");
      STATUS(conn) = CLOSE_CONNECTION;
      system_os_post(requester_TaskPrio, 0, (uintptr_t)conn);
      return;
    }

    pdata++;
    len--;
  }
}

LOCAL void ICACHE_FLASH_ATTR
disconnectcb(espconn_t *conn) {
  requester_context_t *ctx = (requester_context_t*)conn->reverse;
  os_printf("disconnect ssl cb\n");
  ctx->f_status(ctx->ctx, REQ_CONN_CLOSED);
}

LOCAL void ICACHE_FLASH_ATTR
sentcb(espconn_t *conn) {
  requester_context_t *ctx = (requester_context_t*)conn->reverse;
  os_printf("sent ssl cb\n");
  ctx->f_status(ctx->ctx, REQ_CONN_REQ_SENT);
}

void ICACHE_FLASH_ATTR
requester_cookie_add(requester_cookies_t* jar,
                     char* const pch_name,
                     const uint8_t ui_name_size,
                     char* const pch_value,
                     const uint8_t ui_value_size) {
  if (!jar) {
    return;
  }
  // update an existing cookie if possible
  for (int i = 0; i < jar->ui_size; i++) {
    if (os_strlen(jar->s_cookies[i].pch_name) == ui_name_size &&
        strncasecmp(pch_name, jar->s_cookies[i].pch_name, ui_name_size) == 0) {
      os_free((char*)jar->s_cookies[i].pch_value);
      jar->s_cookies[i].pch_value = os_zalloc(ui_value_size+1);
      os_memcpy((char*)jar->s_cookies[i].pch_value,
                pch_value,
                ui_value_size);
      return;
    }
  }
  // create a new cookie
  jar->s_cookies = os_realloc(jar->s_cookies,
                              sizeof(requester_cookies_t) * (jar->ui_size + 1));
  jar->s_cookies[jar->ui_size].pch_name = os_zalloc(ui_name_size+1);
  os_memcpy((char*)jar->s_cookies[jar->ui_size].pch_name,
            pch_name,
            ui_name_size);
  jar->s_cookies[jar->ui_size].pch_value = os_zalloc(ui_value_size+1);
  os_memcpy((char*)jar->s_cookies[jar->ui_size].pch_value,
            pch_value,
            ui_value_size);
  jar->ui_size++;
}

void ICACHE_FLASH_ATTR requester_cookie_free(requester_cookies_t* jar) {
  if (!jar) {
    return;
  }
  for (int i = 0; i < jar->ui_size; i++) {
    os_free((char*)jar->s_cookies[i].pch_name);
    os_free((char*)jar->s_cookies[i].pch_value);
    jar->s_cookies[i].pch_name = NULL;
    jar->s_cookies[i].pch_value = NULL;
  }
  os_free(jar->s_cookies);
  jar->s_cookies = NULL;
  os_free(jar);
}

LOCAL void ICACHE_FLASH_ATTR timeout_timer(void *arg) {
  STATUS(((espconn_t*)arg)) = CLOSE_CONNECTION;
  system_os_post(requester_TaskPrio, 0, (uintptr_t)arg);
}
