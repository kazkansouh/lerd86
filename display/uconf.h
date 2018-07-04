#ifndef _UCONF_H
#define _UCONF_H

#include <stdbool.h>

typedef int(*f_uconf_read_int_t)(void);
typedef char*(*f_uconf_read_cstr_t)(void);

typedef bool(*f_uconf_write_int_t)(int);
typedef bool(*f_uconf_write_cstr_t)(char*);

bool uconf_register_read_int(const char* const pch_name,
                             const f_uconf_read_int_t f_reader);

bool uconf_register_write_int(const char* const pch_name,
                              const f_uconf_write_int_t f_writer);

void uconf_register_http(void);

#endif // _UCONF_H
