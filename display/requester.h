#ifndef _REQUESTER_H
#define _REQUESTER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  REQ_CONN_CONNECT_START = 0x00,
  REQ_CONN_CONNECTED,
  REQ_CONN_REQ_SENT,
  REQ_CONN_CLOSED,
  REQ_DNS_LOOKUP_FAIL,
  REQ_CONN_FAIL,
  REQ_OUT_OF_MEM,
} e_requester_status_t;

typedef void(*requester_status_callback_t)(void *ctx, e_requester_status_t err);
// todo: consider a callback for each header received, further
// collapsing all callbacks into status callback
typedef bool (*requester_header_callback_t)(void* ctx,
                                            uint32_t ui_response_code);
typedef bool (*requester_response_callback_t)(void* ctx,
                                              const char *pdata,
                                              unsigned short len);

/*
 * todo, add support for domains and paths of cookies
 */
typedef struct {
  const char* pch_name;
  const char* pch_value;
} requester_cookie_t;

typedef struct {
  requester_cookie_t* s_cookies;
  uint32_t ui_size;
} requester_cookies_t;

bool request(const char* const pch_host,
             const uint16_t ui_port,
             const char* const pch_resource,
             requester_cookies_t* const s_cookies,
             const requester_status_callback_t f_status,
             const requester_header_callback_t f_header,
             const requester_response_callback_t f_response,
             void* const ctx); // ctx, application use for callbacks

void requester_cookie_add(requester_cookies_t* jar,
                          char* const pch_name,
                          const uint8_t ui_name_size,
                          char* const pch_value,
                          const uint8_t ui_value_size);

void requester_cookie_free(requester_cookies_t* jar);
#endif
