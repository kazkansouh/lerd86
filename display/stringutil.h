#ifndef _STRING_H
#define _STRING_H

char* strnchr(char *pdata, char chr, unsigned short len);
long strtol(const char *nptr, char **endptr, register int base);

#define isSpace(c) (c == ' ' || c == '\t')
#define isDigit(c) (c >= '0' && c <= '9')
#define isAlpha(c) ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'z'))
#define isUpper(c) (c >= 'A' && c <= 'z')

#endif // _STRING_H
