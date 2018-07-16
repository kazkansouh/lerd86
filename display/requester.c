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
  LENGTH, // chunked transfer encoding
  LENGTH_CR,
  IN_CHUNK,
  CHUNK_CR,
  CHUNK_LF,
  IN_BODY,
  IN_ERROR,
  NOOP,
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
  uint16_t ui_data;
  const uint8_t* pch_data;
  // method to use, e.g. GET, POST
  const char* pch_method;
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
  uint16_t ui_response_code;
  // content length of response, or if b_chunked size of chunk
  int32_t i_response_length;
  // chunked transfer from server
  bool b_chunked;
  // headers to append to request
  requester_headers_t* s_req_headers;
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
                               const uint8_t* const pch_data,
                               uint16_t ui_data,
                               requester_headers_t* s_req_headers,
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
  s_context.pch_data = pch_data;
  s_context.ui_data = ui_data;
  // hard code the request method based on whether messagae body is
  // present
  s_context.pch_method = pch_data ? "POST" : "GET";
  s_context.s_req_headers = s_req_headers;

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

#define MEM_ERROR                                               \
  ctx->f_status(ctx->ctx, REQ_OUT_OF_MEM);                      \
  ctx->e_status = CLOSE_CONNECTION;                             \
  system_os_post(requester_TaskPrio, 0, (uintptr_t)pespconn);   \
  return;

#define REQUEST_BUFFER_SIZE 1024
LOCAL const char* const pch_request =
  "%s %s HTTP/1.1\r\n"
  "Host: %s\r\n"
  "Accept-Encoding: identity\r\n"
  "Accept: */*\r\n"
  "User-Agent: esp8622/0.1\r\n"
  "Connection: close\r\n"
  ;

LOCAL void ICACHE_FLASH_ATTR
requester_Task(os_event_t *events) {
  espconn_t *pespconn = (espconn_t *)events->par;
  requester_context_t *ctx = (requester_context_t*)pespconn->reverse;
  char ch_buffer[REQUEST_BUFFER_SIZE];
  int i;

  switch (ctx->e_status) {
  case OPEN_CONNECTION:
    // set ssl client buffer to 2KiB
    if (!espconn_secure_set_size(0x01, 0x0800)) {
      os_printf("failed to set buffer size\n");
    }
    ctx->f_status(ctx->ctx,
                  espconn_secure_connect(pespconn) == 0 ?
                  REQ_CONN_CONNECTED : REQ_CONN_FAIL);
    break;
  case SEND_REQUEST:
    // format request line
    i = os_sprintf(ch_buffer,
                   pch_request,
                   ctx->pch_method,
                   ctx->pch_resource,
                   ctx->pch_host);
    // add cookies
    for (int j = 0; ctx->s_jar && j < ctx->s_jar->ui_size; j++) {
      // "Cookie: name=value[; name=value]\r\n"
      if (j == 0) {
        if (i + 8 <= REQUEST_BUFFER_SIZE) {
          i += os_sprintf(ch_buffer + i, "Cookie: ");
        } else {
          MEM_ERROR;
        }
      } else {
        if (i + 2 <= REQUEST_BUFFER_SIZE) {
        ch_buffer[i++] = ';';
        ch_buffer[i++] = ' ';
        } else {
          MEM_ERROR;
        }
      }
      if (i +
          os_strlen(ctx->s_jar->s_cookies[j].pch_name) +
          os_strlen(ctx->s_jar->s_cookies[j].pch_value) +
          1 <= REQUEST_BUFFER_SIZE) {
        i += os_sprintf(ch_buffer + i,
                        "%s=%s",
                        ctx->s_jar->s_cookies[j].pch_name,
                        ctx->s_jar->s_cookies[j].pch_value);
      } else {
        MEM_ERROR;
      }
    }
    if (ctx->s_jar->ui_size) {
      if (i + 2 <= REQUEST_BUFFER_SIZE) {
        ch_buffer[i++] = '\r';
        ch_buffer[i++] = '\n';
      } else {
        MEM_ERROR;
      }
    }
    // additional headers
    for (int j = 0;
         ctx->s_req_headers && j < ctx->s_req_headers->ui_size; j++) {
      // "name: value\r\n"
      if (i +
          os_strlen(ctx->s_req_headers->s_headers[j].pch_name) +
          os_strlen(ctx->s_req_headers->s_headers[j].pch_value) +
          4 <= REQUEST_BUFFER_SIZE) {
         i += os_sprintf(ch_buffer + i,
                        "%s: %s\r\n",
                        ctx->s_req_headers->s_headers[j].pch_name,
                        ctx->s_req_headers->s_headers[j].pch_value);
      } else {
        MEM_ERROR;
      }
    }
    // content length
    if (ctx->pch_data && ctx->ui_data) {
      // "Content-Length: xxxxx\r\n"
      if (i + 23 <= REQUEST_BUFFER_SIZE) {
        i += os_sprintf(ch_buffer + i, "Content-Length: %d\r\n", ctx->ui_data);
      } else {
        MEM_ERROR;
      }
    }
    // crlf
    if (i + 2 <= REQUEST_BUFFER_SIZE) {
      ch_buffer[i++] = '\r';
      ch_buffer[i++] = '\n';
    } else {
      MEM_ERROR;
    }
    // message body
    if (ctx->pch_data && ctx->ui_data) {
      if (i + ctx->ui_data <= REQUEST_BUFFER_SIZE) {
        os_memcpy(ch_buffer + i, ctx->pch_data, ctx->ui_data);
        i += ctx->ui_data;
      } else {
        MEM_ERROR;
      }
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
  if (strnchr(ctx->pch_line_buffer, ':', ctx->ui_line_buffer)) {
    // todo, update to allow for multiple cookies in a
    // set-cookie header and quoted cookie values.
    if (ctx->ui_line_buffer >= 12 &&
        os_strncmp("Set-Cookie: ", ctx->pch_line_buffer, 12) == 0) {
      char* cookie_name = ctx->pch_line_buffer + 12;
      char* cookie_name_end = ctx->pch_line_buffer + 12;
      uint32_t len = ctx->ui_line_buffer - 12;
      // allow for empty cookie names
      while (len > 0 && *cookie_name_end != '=') {
        cookie_name_end++;
        len--;
      }
      // require equals
      if (*cookie_name_end == '=') {
          char* cookie_value = cookie_name_end + 1;
          char* cookie_value_end = cookie_name_end + 1;
          if (len) {
            len--;
            while (len > 0 &&
                   *cookie_value_end != ' ' &&
                   *cookie_value_end != ';') {
              cookie_value_end++;
              len--;
            }
          }
          // save cookie
          requester_cookie_add(ctx->s_jar,
                               cookie_name,
                               cookie_name_end - cookie_name,
                               cookie_value,
                               cookie_value_end - cookie_value);
      } else {
        // invalid header
        return false;
      }
    }
    if (ctx->ui_line_buffer > 16 &&
        os_strncmp("Content-Length: ", ctx->pch_line_buffer, 16) == 0) {
      // enforce C-string
      ctx->pch_line_buffer[ctx->ui_line_buffer] = '\0';
      char* ptr_end = NULL;
      uint16_t ui_content_length = strtol(ctx->pch_line_buffer + 16,
                                          &ptr_end, 10);
      if (!ptr_end || ptr_end == ctx->pch_line_buffer + 16) {
        // did not read any digits
        return false;
      }
      ctx->i_response_length = ui_content_length;
    }
    if (ctx->ui_line_buffer >= 19 &&
        os_strncmp("Transfer-Encoding: ",
                   ctx->pch_line_buffer, 19) == 0) {
      ctx->pch_line_buffer[ctx->ui_line_buffer] = '\0';
      // todo, support combination "Transfer-Encoding: indentity, chunked"
      if (os_strcmp("chunked", ctx->pch_line_buffer + 19) == 0) {
        ctx->b_chunked = true;
      } else {
        // unsupported value
        return false;
      }
    }
    return true;
  }
  return false;
}

// uncomment to printout debugging information about the processing of
// the state of the receive state.
// #define DEBUG_RECEIVE_STATE

#ifdef DEBUG_RECEIVE_STATE
  #define SETSTATE(s)                             \
    do {                                          \
      ctx->e_response_state = s;                  \
      os_printf("%x",ctx->e_response_state);      \
    } while(0);
#else
  #define SETSTATE(s) ctx->e_response_state = s;
#endif // DEBUG_RECEIVE_STATE

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

#ifdef DEBUG_RECEIVE_STATE
  os_printf(".");
#endif // DEBUG_RECEIVE_STATE
  os_timer_arm(&timeout_timer_t, 500, 0);

  while (len || ctx->e_response_state == IN_ERROR) {
    switch (ctx->e_response_state) {
    case STATUS_LINE   :
      if (*pdata == '\r') {
        SETSTATE(LINE_CR);
        // process line
        if (!process_status_line(ctx)) {
          SETSTATE(IN_ERROR);
        }
      } else {
        ctx->pch_line_buffer[ctx->ui_line_buffer] = *pdata;
        ctx->ui_line_buffer++;
      }
      break;
    case LINE_CR       :
      if (*pdata == '\n') {
        SETSTATE(LINE_LF);
        os_memset(ctx->pch_line_buffer, 0x00, MAX_HEADER_LENGTH);
        ctx->ui_line_buffer = 0;
      } else {
        SETSTATE(IN_ERROR);
      }
      break;
    case LINE_LF       :
      if (*pdata == '\r') {
        SETSTATE(END_HEADER_CR);
        break;
      } else {
        SETSTATE(HEADER_LINE);
      }
      // ! no break
    case HEADER_LINE   :
      if (*pdata == '\r') {
        SETSTATE(LINE_CR);
        // process line
        if (!process_header_line(ctx)) {
          SETSTATE(IN_ERROR);
        }
      } else {
        ctx->pch_line_buffer[ctx->ui_line_buffer] = *pdata;
        ctx->ui_line_buffer++;
      }
      break;
    case END_HEADER_CR :
      if (*pdata == '\n') {
        SETSTATE(ctx->b_chunked ? LENGTH : IN_BODY);
        os_memset(ctx->pch_line_buffer, 0x00, MAX_HEADER_LENGTH);
        ctx->ui_line_buffer = 0;
        if (!ctx->f_header(ctx->ctx, ctx->ui_response_code)) {
          SETSTATE(IN_ERROR);
        }
      } else {
        SETSTATE(IN_ERROR);
      }
      break;
    case LENGTH        :
      if (*pdata == '\r') {
        SETSTATE(LENGTH_CR);
        ctx->pch_line_buffer[ctx->ui_line_buffer] = '\0';
      } else {
        ctx->pch_line_buffer[ctx->ui_line_buffer] = *pdata;
        ctx->ui_line_buffer++;
      }
      break;
    case LENGTH_CR     :
      if (*pdata == '\n') {
        char *ptr_end = NULL;
        long int i = strtol(ctx->pch_line_buffer, &ptr_end, 16);
        if (!ptr_end || ptr_end == ctx->pch_line_buffer) {
          SETSTATE(IN_ERROR);
        } else {
          SETSTATE(IN_CHUNK);
          ctx->i_response_length = i;
          if (i == 0) {
            STATUS(conn) = CLOSE_CONNECTION;
            system_os_post(requester_TaskPrio, 0, (uintptr_t)conn);
            SETSTATE(NOOP);
            return;
          }
        }
      } else {
        SETSTATE(IN_ERROR);
      }
      break;
    case IN_CHUNK      :
      // remainder of chunk is handled by application
      if (len >= ctx->i_response_length) {
        if (ctx->f_response(ctx->ctx, pdata, ctx->i_response_length)) {
          pdata += (ctx->i_response_length - 1);
          len -= (ctx->i_response_length - 1);
          ctx->i_response_length = 0;
          SETSTATE(CHUNK_CR);
        } else {
          SETSTATE(IN_ERROR);
        }
      } else {
        if (ctx->f_response(ctx->ctx, pdata, len)) {
          ctx->i_response_length -= len;
          pdata += len;
          len = 0;
          return;
        } else {
          SETSTATE(IN_ERROR);
        }
      }
      break;
    case CHUNK_CR      :
      if (*pdata == '\r') {
        SETSTATE(CHUNK_LF);
      } else {
        SETSTATE(IN_ERROR);
      }
      break;
    case CHUNK_LF      :
      if (*pdata == '\n') {
        SETSTATE(LENGTH);
        os_memset(ctx->pch_line_buffer, 0x00, MAX_HEADER_LENGTH);
        ctx->ui_line_buffer = 0;
      } else {
        SETSTATE(IN_ERROR);
      }
      break;
    case IN_BODY       :
      // remainder of buffer is handled by application
      if (ctx->f_response(ctx->ctx, pdata, len)) {
        ctx->i_response_length -= len;
        if (ctx->i_response_length <= 0) {
          STATUS(conn) = CLOSE_CONNECTION;
          system_os_post(requester_TaskPrio, 0, (uintptr_t)conn);
          SETSTATE(NOOP);
        }
        return;
      }
      SETSTATE(IN_ERROR);
      break;
    case IN_ERROR      :
      os_printf("invalid response received\n");
      STATUS(conn) = CLOSE_CONNECTION;
      system_os_post(requester_TaskPrio, 0, (uintptr_t)conn);
      SETSTATE(NOOP);
      return;
    case NOOP:
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
                     const uint16_t ui_name_size,
                     char* const pch_value,
                     const uint16_t ui_value_size) {
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

void ICACHE_FLASH_ATTR
requester_header_add(requester_headers_t* s_hdrs,
                     char* const pch_name,
                     const uint8_t ui_name_size,
                     char* const pch_value,
                     const uint8_t ui_value_size) {
  if (!s_hdrs) {
    return;
  }
  // create a header
  s_hdrs->s_headers =
    os_realloc(s_hdrs->s_headers,
               sizeof(requester_headers_t) * (s_hdrs->ui_size + 1));
  s_hdrs->s_headers[s_hdrs->ui_size].pch_name = os_zalloc(ui_name_size+1);
  os_memcpy((char*)s_hdrs->s_headers[s_hdrs->ui_size].pch_name,
            pch_name,
            ui_name_size);
  s_hdrs->s_headers[s_hdrs->ui_size].pch_value = os_zalloc(ui_value_size+1);
  os_memcpy((char*)s_hdrs->s_headers[s_hdrs->ui_size].pch_value,
            pch_value,
            ui_value_size);
  s_hdrs->ui_size++;
}

void ICACHE_FLASH_ATTR requester_header_free(requester_headers_t* s_hdrs) {
  if (!s_hdrs) {
    return;
  }
  for (int i = 0; i < s_hdrs->ui_size; i++) {
    os_free((char*)s_hdrs->s_headers[i].pch_name);
    os_free((char*)s_hdrs->s_headers[i].pch_value);
    s_hdrs->s_headers[i].pch_name = NULL;
    s_hdrs->s_headers[i].pch_value = NULL;
  }
  os_free(s_hdrs->s_headers);
  s_hdrs->s_headers = NULL;
  os_free(s_hdrs);
}

LOCAL void ICACHE_FLASH_ATTR timeout_timer(void *arg) {
  STATUS(((espconn_t*)arg)) = CLOSE_CONNECTION;
  system_os_post(requester_TaskPrio, 0, (uintptr_t)arg);
}
