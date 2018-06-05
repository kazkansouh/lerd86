#ifndef _STRINGUTIL_H
#define _STRINGUTIL_H

#include <stdint.h>
#include <stdbool.h>

char* strnchr(char *pdata, char chr, unsigned short len);

typedef struct {
  char* pch_token;
  uint32_t ui_position;
} scan_context_t;

/*
 * scan contiguious datapackets for a given token, set ctx->pch_token
 * to a zero terminated string of the token to search for and
 * ctx->ui_position to zero.
 *
 * return true when found, and false if counsumed all input. ui_len is
 * updated accordingly to how much data remains to be consumed.
 */
bool scan_token(scan_context_t* s_ctx,
                const char* const pch_data,
                uint16_t* const ui_len);


/*
 * scan contiguious datapackets and take bytes until an end char is
 * found. ctx should be set to a zero'd structure before calling.
 *
 * return true when found, and false if counsumed all input. ui_len is
 * updated accordingly to how much data remains to be
 * consumed. ctx->pch_token will be set to a newly allocated zero
 * terminated string.
 */
bool scan_until(scan_context_t* s_ctx,
                const char c,
                const char* const pch_data,
                uint16_t* const ui_len);

#endif // _STRINGUTIL_H
