#ifndef _HTTP_H
#define _HTTP_H

#include <stdint.h>
#include <stdbool.h>

#define HTTP_RESP_HEADER(code, content, len)            \
  "HTTP/1.1 " #code "\r\n"                              \
  "Content-Type: " #content "\r\n"                      \
  "Content-Length: " #len "\r\n\r\n"

#define HTML_REF_DOC(title,refs,body)                   \
  "<html>"                                              \
    "<head>"                                            \
      "<title>" title "</title>" refs                   \
    "</head>"                                           \
    "<body>" body "</body>"                             \
  "</html>"

#define HTML_REF_DOC_LEN 54

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
} http_query_t, http_header_t;

typedef struct http_request_context_t {
  char*                   pch_resource;
  http_query_t*           s_query;
  uint8_t                 ui_query_size;
  // should not be changed outside of http code
  struct http_internal_t* ps_internal;
} http_request_context_t;

void http_init(uint16_t ui_port);
void http_register_init(void);
bool http_register_handler(const char* pch_namespace,
                           http_handler_t f_handler);

int8_t http_send_response(struct espconn* const conn,
                          const uint16_t ui_response_code,
                          const char* const pch_content_type,
                          http_header_t* ps_headers,
                          uint8_t ui_headers_len,
                          const char* const pch_payload,
                          const uint16_t ui_payload_len,
                          const bool b_free_payload);

const char* http_request_context_lookup(http_request_context_t* ctx,
                                        const char* pch_param);

#endif // _HTTP_H
