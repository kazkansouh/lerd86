#include <c_types.h>

#include "stringutil.h"

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
