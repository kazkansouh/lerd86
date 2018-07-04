/*
 * simple http server for esp8266, follows standard architecture by
 * allowing for namespaces on the server to be registered with
 * callback function to handle the respose. currently only supports
 * get requests
 */

#include "http.h"
#include "stringutil.h"
#include <c_types.h>
#include <ip_addr.h>
#include <espconn.h>
#include <mem.h>
#include <osapi.h>
#include <strings.h>
#include <user_interface.h>
#include <stdlib.h>

#define TIMEOUT 60*5
#define MAX_HANDLERS 10

typedef struct espconn espconn_t;

LOCAL void connectcb(espconn_t *conn);
LOCAL void receivecb(espconn_t *conn, char *pdata, unsigned short len);
LOCAL void disconnectcb(espconn_t *conn);
LOCAL void sentcb(espconn_t *conn);

LOCAL espconn_t s_server_conn = {0};
LOCAL esp_tcp s_server_tcp = {0};

#define server_TaskPrio     1
#define server_TaskQueueLen 10

LOCAL os_event_t server_TaskQueue[server_TaskQueueLen];
LOCAL void server_Task(os_event_t *events);

enum server_task_ids {
  DISCONNECT_CLIENT    = 0x00,
  ERROR_404,
  APP_SERVICED,
  SEND_DATA,
};

LOCAL char*          pch_handlers[MAX_HANDLERS];
LOCAL http_handler_t pf_handlers [MAX_HANDLERS];

// each accepted connection will have the below data assigned.
typedef struct http_internal_t {
  // callback that will provide the response
  http_handler_t f_handler;
  // below are used for sending the response (as it might be larger
  // than a packet)
  const uint8_t* pch_data;
  uint16_t ui_data_len;
  const uint8_t* pch_data_to_free;
} http_internal_t;

LOCAL
const char* pch_msg_error = HTTP_RESP_HEADER(404, text/html, 77)
                            HTML_DOC("ERROR", "<h1>Error 404</h1>");

void ICACHE_FLASH_ATTR http_init(uint16_t ui_port) {
  system_os_task(server_Task,
                 server_TaskPrio,
                 server_TaskQueue,
                 server_TaskQueueLen);

  if (s_server_conn.state == ESPCONN_LISTEN) {
    espconn_disconnect(&s_server_conn);
    os_memset(&s_server_conn, 0x00, sizeof(espconn_t));
    os_memset(&s_server_tcp, 0x00, sizeof(esp_tcp));
  }

  s_server_conn.type = ESPCONN_TCP;
  s_server_conn.state = ESPCONN_NONE;
  s_server_conn.proto.tcp = &s_server_tcp;
  s_server_conn.proto.tcp->local_port = ui_port;
  s_server_conn.reverse = NULL;

  espconn_regist_connectcb(&s_server_conn, (espconn_connect_callback)connectcb);
  espconn_regist_time(&s_server_conn, TIMEOUT, 0);
  espconn_accept(&s_server_conn);
}

LOCAL void ICACHE_FLASH_ATTR
connectcb(espconn_t *conn) {
  conn->reverse =
    (http_request_context_t *)os_zalloc(sizeof(http_request_context_t));
  if (!conn->reverse) {
    // out of mem
    return;
  }
  ((http_request_context_t *)conn->reverse)->ps_internal =
    (http_internal_t*)os_zalloc(sizeof(http_internal_t));
  if (!((http_request_context_t *)conn->reverse)->ps_internal) {
    // out of mem
    os_free(conn->reverse);
    return;
  }
  espconn_regist_recvcb(conn, (espconn_recv_callback)receivecb);
  espconn_regist_disconcb(conn, (espconn_connect_callback)disconnectcb);
  espconn_regist_sentcb(conn, (espconn_sent_callback)sentcb);
}

LOCAL void ICACHE_FLASH_ATTR
receivecb(espconn_t *conn, char *pdata, unsigned short len) {
  http_request_context_t* ctx = (http_request_context_t*)conn->reverse;

  char* eol = strnchr(pdata, 0x0D, len);
  if (eol) {
    if (os_strncmp(pdata, "GET ", 4) == 0) {
      char* request = pdata += 4;
      char* space = strnchr(request, 0x20, eol - pdata - 4);
      if (space) {
        unsigned short reqsize = space - request;
        char* query = strnchr(request, '?', reqsize);
        if (query++) {
          unsigned short querysize = reqsize - (query - request);
          reqsize -= querysize + 1;
          // extract parameters from query string
          while (querysize) {
            char* equal = strnchr(query, '=', querysize);
            if (equal) {
              char* param = query;
              unsigned short paramsize = equal - query;
              char* value = equal + 1;
              querysize -= paramsize + 1;
              query = value;
              unsigned short valuesize = querysize;
              char* next = strnchr(query, '&', querysize);
              if (next) {
                valuesize = next - value;
                query = next + 1;
                querysize -= valuesize + 1;
              } else {
                querysize = 0;
              }

              // param + paramsize
              // value + valuesize
              http_request_context_add_parameter(ctx,
                                                 param,
                                                 paramsize,
                                                 value,
                                                 valuesize);
            } else {
              break;
            }
          }
        }

        // request + reqsize
        http_request_context_set_resource(ctx, request, reqsize);

        // iterate over handlers, if matched save into ctx
        int i;
        for (i = 0; i < MAX_HANDLERS; i++) {
          if (strncasecmp(pch_handlers[i],
                          ctx->pch_resource,
                          os_strlen(pch_handlers[i])) == 0) {
            ctx->ps_internal->f_handler = pf_handlers[i];
            system_os_post(server_TaskPrio, APP_SERVICED, (uintptr_t)conn);
            return;
          }
        }
      }
    }
  }

  system_os_post(server_TaskPrio, ERROR_404, (uintptr_t)conn);
}

LOCAL void ICACHE_FLASH_ATTR
disconnectcb(espconn_t *conn) {
  http_request_context_free(conn->reverse);
  conn->reverse = NULL;
}

LOCAL void ICACHE_FLASH_ATTR
sentcb(espconn_t *conn) {
  http_request_context_t *ctx = (http_request_context_t*)conn->reverse;
  if (ctx->ps_internal->ui_data_len) {
    system_os_post(server_TaskPrio, SEND_DATA, (uintptr_t)conn);
  } else {
    system_os_post(server_TaskPrio, DISCONNECT_CLIENT, (uintptr_t)conn);
  }
}

LOCAL void ICACHE_FLASH_ATTR
server_Task(os_event_t *events) {
  espconn_t *pespconn = (espconn_t *)events->par;
  http_request_context_t *ctx = (http_request_context_t*)pespconn->reverse;

  switch (events->sig) {
  case DISCONNECT_CLIENT:
    if (ctx->ps_internal->pch_data_to_free) {
      os_free((void*)ctx->ps_internal->pch_data_to_free);
      ctx->ps_internal->pch_data_to_free = NULL;
    }
    espconn_disconnect(pespconn);
    break;
  case SEND_DATA:
    if (ctx->ps_internal->pch_data && ctx->ps_internal->ui_data_len) {
      uint16_t ui_send_len = 512;
      if (ctx->ps_internal->ui_data_len < ui_send_len) {
        ui_send_len = ctx->ps_internal->ui_data_len;
      }
      if (espconn_send(pespconn,
                       (uint8_t*)ctx->ps_internal->pch_data,
                       ui_send_len)
          == ESPCONN_OK) {
        ctx->ps_internal->ui_data_len -= ui_send_len;
        ctx->ps_internal->pch_data += ui_send_len;
      } else {
        system_os_post(server_TaskPrio, DISCONNECT_CLIENT, events->par);
      }
    } else {
      system_os_post(server_TaskPrio, DISCONNECT_CLIENT, events->par);
    }
    break;
  case APP_SERVICED:
    if (ctx->ps_internal->f_handler(pespconn, ctx)) {
      break;
    }
    // !! no break
  case ERROR_404:
  default:
    // send error
    espconn_send(pespconn, (uint8*)pch_msg_error, os_strlen(pch_msg_error));
  }
}

void ICACHE_FLASH_ATTR http_register_init() {
  int i;
  for (i = 0; i < MAX_HANDLERS; i++) {
    if (pch_handlers[i]) {
      os_free(pch_handlers[i]);
      pch_handlers[i] = NULL;
      pf_handlers[i] = NULL;
    }
  }
}

bool ICACHE_FLASH_ATTR http_register_handler(const char* pch_namespace,
                                             http_handler_t f_handler) {
  int i;
  for (i = 0; i < MAX_HANDLERS; i++) {
    if (pch_handlers[i] == NULL) {
      pch_handlers[i] = (char*)os_zalloc(os_strlen(pch_namespace) + 1);
      os_memcpy(pch_handlers[i], pch_namespace, os_strlen(pch_namespace));
      pf_handlers[i] = f_handler;
      return true;
    }
  }
  return false;
}

void ICACHE_FLASH_ATTR
http_request_context_set_resource(http_request_context_t* ctx,
                                  char* pch_resource,
                                  uint8_t ui_size) {
  if (ctx->pch_resource) {
    os_free(ctx->pch_resource);
  }
  ctx->pch_resource = os_zalloc(ui_size+1);
  os_memcpy(ctx->pch_resource, pch_resource, ui_size);
}

LOCAL void ICACHE_FLASH_ATTR
http_urldecode(char* pch_url) {
  int i = 0;
  char* ptr = pch_url;

  while (*ptr != '\0') {
    if (*ptr != '%') {
      pch_url[i++] = *(ptr++);
    } else {
      char val[3] = {0,0,0};

      if ((val[0] = *(ptr+1)) != '\0' &&
          (val[1] = *(ptr+2)) != '\0') {
        char* end = NULL;
        long int x = strtol(val, &end, 16);
        if (end != val+2) {
          // invalid hex value
          break;
        }
        pch_url[i++] = x & 0xFF;
        ptr += 3;
      } else {
        // malformed
        break;
      }
    }
  }
  pch_url[i] = '\0';
}

void ICACHE_FLASH_ATTR
http_request_context_add_parameter(http_request_context_t* ctx,
                                   char* pch_param,
                                   uint8_t ui_param_size,
                                   char* pch_value,
                                   uint8_t ui_value_size) {
  ctx->s_query = os_realloc(ctx->s_query,
                            sizeof(http_query_t) * (ctx->ui_query_size + 1));
  ctx->s_query[ctx->ui_query_size].pch_param = os_zalloc(ui_param_size+1);
  os_memcpy(ctx->s_query[ctx->ui_query_size].pch_param,
            pch_param,
            ui_param_size);
  ctx->s_query[ctx->ui_query_size].pch_value = os_zalloc(ui_value_size+1);
  os_memcpy(ctx->s_query[ctx->ui_query_size].pch_value,
            pch_value,
            ui_value_size);
  http_urldecode(ctx->s_query[ctx->ui_query_size].pch_value);
  ctx->ui_query_size++;
}

void ICACHE_FLASH_ATTR
http_request_context_free(http_request_context_t* ctx) {
  if (ctx) {
    if (ctx->pch_resource) {
      os_free(ctx->pch_resource);
      ctx->pch_resource = NULL;
    }
    int i;
    for (i = 0; i < ctx->ui_query_size; i++) {
      if (ctx->s_query[i].pch_param) {
        os_free(ctx->s_query[i].pch_param);
        ctx->s_query[i].pch_param = NULL;
      }
      if (ctx->s_query[i].pch_value) {
        os_free(ctx->s_query[i].pch_value);
        ctx->s_query[i].pch_value = NULL;
      }
    }
    os_free(ctx->s_query);
    ctx->s_query = NULL;
    os_free(ctx);
  }
}

const char*  ICACHE_FLASH_ATTR
http_request_context_lookup(http_request_context_t* ctx,
                            const char* pch_param) {
  int i;
  for (i = 0; i < ctx->ui_query_size; i++) {
    if (os_strcmp(ctx->s_query[i].pch_param, pch_param) == 0) {
      return ctx->s_query[i].pch_value;
    }
  }
  return NULL;
}

LOCAL size_t ICACHE_FLASH_ATTR
http_header_len(http_header_t* ps_headers,
                uint8_t ui_headers_len) {
  size_t i = 0;
  while (ui_headers_len--) {
    i += 4; // ": " and "\r\n"
    i += os_strlen(ps_headers->pch_param);
    i += os_strlen(ps_headers->pch_value);
    ps_headers++;
  }
  return i;
}

int8_t ICACHE_FLASH_ATTR
http_send_response(espconn_t* const conn,
                   const uint16_t ui_response_code,
                   const char* const pch_content_type,
                   http_header_t* ps_headers,
                   uint8_t ui_headers_len,
                   const char* const pch_payload,
                   const uint16_t ui_payload_len,
                   const bool b_free_payload) {
  // construct a header and send
  char* ch_buffer = (char*)os_malloc(14 + // response line
                                     // if content is present, include
                                     //   "Content-Length: xxxxxx\r\n"
                                     //   "Content-Type: \r\n"
                                     (ui_payload_len && pch_payload ?
                                      40 + os_strlen(pch_content_type) : 0) +
                                     http_header_len(ps_headers,
                                                     ui_headers_len) +
                                     3); // "\r\n\0"
  size_t i = os_sprintf(ch_buffer, "HTTP/1.1 %d\r\n", ui_response_code);
  if (ui_payload_len && pch_payload) {
    i += os_sprintf(ch_buffer + i,
                    "Content-Length: %d\r\n"
                    "Content-Type: %s\r\n",
                    ui_payload_len,
                    pch_content_type);
  }
  while (ui_headers_len--) {
    i += os_sprintf(ch_buffer + i,
                    "%s: %s\r\n",
                    ps_headers->pch_param,
                    ps_headers->pch_value);
    ps_headers++;
  }
  i += os_sprintf(ch_buffer + i, "\r\n");
  int8_t i_ret = espconn_send(conn, (uint8_t*)ch_buffer, i);
  if (i_ret == ESPCONN_OK) {
    http_request_context_t *ctx = (http_request_context_t*)conn->reverse;
    ctx->ps_internal->pch_data = (const uint8_t*)pch_payload;
    ctx->ps_internal->ui_data_len = ui_payload_len;
    if (b_free_payload) {
      ctx->ps_internal->pch_data_to_free = (const uint8_t*)pch_payload;
    } else {
      ctx->ps_internal->pch_data_to_free = NULL;
    }
  }
  return i_ret;
}
