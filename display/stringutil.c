#include <c_types.h>

#include "stringutil.h"
#include <osapi.h>
#include <mem.h>

char* ICACHE_FLASH_ATTR
strnchr(char *pdata, char chr, unsigned short len) {
  unsigned short i = 0;
  while (i < len) {
    if (pdata[i] == chr) {
      return pdata + i;
    }
    i++;
  }
  return NULL;
}

bool ICACHE_FLASH_ATTR
scan_token(scan_context_t* s_ctx,
           const char* const pch_data,
           uint16_t* const ui_len) {
  uint16_t i = 0;
  const size_t toklen = os_strlen(s_ctx->pch_token);
  while (*ui_len != 0) {
    (*ui_len)--;
    if (pch_data[i++] == s_ctx->pch_token[s_ctx->ui_position++]) {
      if (toklen == s_ctx->ui_position) {
        return true;
      }
    } else {
      s_ctx->ui_position = 0;
    }
  }
  return false;
}

bool ICACHE_FLASH_ATTR
scan_until(scan_context_t* s_ctx,
           const char c,
           const char* const pch_data,
           uint16_t* const ui_len) {
  uint16_t i = 0;
  while (*ui_len != 0) {
    (*ui_len)--;
    // increase by one byte
    s_ctx->pch_token = os_realloc(s_ctx->pch_token, s_ctx->ui_position + 1);
    if (pch_data[i] == c) {
      s_ctx->pch_token[s_ctx->ui_position] = '\0';
      return true;
    } else {
      s_ctx->pch_token[s_ctx->ui_position++] = pch_data[i++];
    }
  }
  return false;
}
