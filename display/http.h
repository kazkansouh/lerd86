#ifndef _HTTP_H
#define _HTTP_H

#include <stdint.h>
#include <stdbool.h>

#define HTTP_RESP_HEADER(code, content)                 \
  "HTTP/1.1 " #code "\r\n"                              \
  "Content-Type: " #content "\r\n\r\n"

#define HTML_REF_DOC(title,refs,body)                   \
  "<html>"                                              \
    "<head>"                                            \
      "<title>" title "</title>" refs                   \
    "</head>"                                           \
    "<body>" body "</body>"                             \
  "</html>"

#define HTML_DOC(title,body) HTML_REF_DOC(title,"",body)

#define HTML_STYLE_DOC(title,style,body)                                \
  HTML_REF_DOC(                                                         \
    title,                                                              \
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"" #style "\">",  \
    body)

struct http_request_context_t;
struct espconn;

typedef bool(*http_handler_t)(struct espconn* conn,
                              struct http_request_context_t* ctx);

typedef struct {
  char* pch_param;
  char* pch_value;
} http_query_t;

typedef struct http_request_context_t {
  char*          pch_resource;
  http_query_t*  s_query;
  uint8_t        ui_query_size;
  http_handler_t f_handler;
} http_request_context_t;

void http_init(uint16_t ui_port);
void http_register_init(void);
bool http_register_handler(const char* pch_namespace,
                           http_handler_t f_handler);

void http_request_context_set_resource(http_request_context_t* ctx,
                                       char* pch_resource,
                                       uint8_t ui_size);
void http_request_context_add_parameter(http_request_context_t* ctx,
                                       char* pch_param,
                                       uint8_t ui_param_size,
                                       char* pch_value,
                                       uint8_t ui_value_size);
void http_request_context_free(http_request_context_t* ctx);

const char* http_request_context_lookup(http_request_context_t* ctx,
                                        const char* pch_param);

#endif // _HTTP_H
