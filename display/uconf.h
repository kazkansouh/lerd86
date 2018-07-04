#ifndef _UCONF_H
#define _UCONF_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t(*f_uconf_read_uint8_t)(void);
typedef int(*f_uconf_read_int_t)(void);
typedef char*(*f_uconf_read_cstr_t)(void);

typedef bool(*f_uconf_write_uint8_t)(uint8_t);
typedef bool(*f_uconf_write_int_t)(int);
typedef bool(*f_uconf_write_cstr_t)(char*);

typedef enum {
  eUint8,
  eInt, // only values castable to int before this point
  eString,
} e_uconf_type_t;

typedef struct {
  const char* pch_name;
  e_uconf_type_t e_type;
} uconf_parameter_t;

typedef union {
  uint8_t ui8;
  int i;
  char* s;
} uconf_data_t;

typedef bool(*f_uconf_action_t)(uint8_t ui_args,
                                const uconf_data_t* pu_args);

bool uconf_register_action(const char* const pch_name,
                           const uconf_parameter_t* const ps_args,
                           const uint8_t ui_args_len,
                           const f_uconf_action_t f);

bool uconf_register_read_uint8(const char* const pch_name,
                               const f_uconf_read_uint8_t f_reader);
bool uconf_register_write_uint8(const char* const pch_name,
                                const f_uconf_write_uint8_t f_writer);

bool uconf_register_read_int(const char* const pch_name,
                             const f_uconf_read_int_t f_reader);
bool uconf_register_write_int(const char* const pch_name,
                              const f_uconf_write_int_t f_writer);

bool uconf_register_read_cstr(const char* const pch_name,
                              const f_uconf_read_cstr_t f_reader);
bool uconf_register_write_cstr(const char* const pch_name,
                               const f_uconf_write_cstr_t f_writer);

void uconf_register_http(void);

bool uconf_var_set_uint8(const char* const pch_name,
                         const uint8_t ui_value);
bool uconf_var_set_int(const char* const pch_name,
                       const int i_value);
bool uconf_var_set_cstr(const char* const pch_name,
                        char* const  pch_value);

#endif // _UCONF_H
