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
#include <string.h>

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
};

LOCAL char*          pch_handlers[MAX_HANDLERS];
LOCAL http_handler_t pf_handlers [MAX_HANDLERS];

LOCAL
const char* pch_msg_error = HTTP_RESP_HEADER(400, text/html)
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
            ctx->f_handler = pf_handlers[i];
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
  system_os_post(server_TaskPrio, DISCONNECT_CLIENT, (uintptr_t)conn);
}

LOCAL void ICACHE_FLASH_ATTR
server_Task(os_event_t *events) {
  espconn_t *pespconn = (espconn_t *)events->par;
  http_request_context_t *ctx = (http_request_context_t*)pespconn->reverse;

  switch (events->sig) {
  case DISCONNECT_CLIENT:
    espconn_disconnect(pespconn);
    break;
  case APP_SERVICED:
    if (ctx->f_handler(pespconn, ctx)) {
      break;
    }
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
