#include <c_types.h>
#include <osapi.h>
#include <mem.h>
#include <ip_addr.h>
#include <espconn.h>
#include <stdlib.h>

#include "http.h"
#include "uconf.h"
#include "beacon.h"

/*
 * Server side uConf implementation.
 *
 * Integrates with http server to provide basic
 * configuration. Provides the following:
 *
 *  /uconf/schema
 *  /uconf/get?var=<name>
 *  /uconf/set?var=<name>&val=<value>
 *  /uconf/invoke?method=<name>&<param_name_i>=<param_value_i>
 *
 * Here, schema is a json object describing the registered
 * handlers. Currently supported values are of integer and cstring.
 */

typedef e_uconf_type_t e_type_t;
typedef uconf_data_t data_t;

typedef union {
  void* valid;
  f_uconf_read_uint8_t ui8;
  f_uconf_read_int_t i;
  f_uconf_read_cstr_t s;
} read_function_t;

typedef union {
  void* valid;
  f_uconf_write_uint8_t ui8;
  f_uconf_write_int_t i;
  f_uconf_write_cstr_t s;
} write_function_t;

typedef struct variable_t {
  struct variable_t* ps_next;
  const char* pch_name;
  e_type_t e_type;
  bool b_broadcast;
  read_function_t f_reader;
  write_function_t f_writer;
  bool b_remote_write;
} variable_t;

typedef struct action_t {
  struct action_t* ps_next;
  const char* pch_name;
  f_uconf_action_t f_action;
  const uconf_parameter_t* ps_args;
  uint8_t ui_args_len;
} action_t;

LOCAL
struct {
  variable_t* ps_variables;
  action_t* ps_actions;
} gs_model = {NULL, NULL};

// base type of action and variable structures, used for generic
// recursion
typedef struct base_t {
  struct base_t* ps_next;
  const char* pch_name;
} base_t;

const char* const gpch_json_ct = "application/json";

LOCAL base_t* uconf_find_by_name(base_t* ps_first, const char* const pch_name) {
  while (pch_name && ps_first) {
    if (os_strcmp(ps_first->pch_name, pch_name) == 0) {
      return ps_first;
    } else {
      ps_first = ps_first->ps_next;
    }
  }
  return NULL;
}

bool ICACHE_FLASH_ATTR
uconf_register_action(const char* const pch_name,
                      const uconf_parameter_t* const ps_args,
                      const uint8_t ui_args_len,
                      const f_uconf_action_t f) {
  if (!f) {
    return false;
  }
  action_t* ps_act =
    (action_t*)uconf_find_by_name((base_t*)gs_model.ps_actions, pch_name);
  if (ps_act) {
    return false;
  }

  ps_act = gs_model.ps_actions;
  gs_model.ps_actions = (action_t*)os_zalloc(sizeof(action_t));
  gs_model.ps_actions->ps_next = ps_act;
  gs_model.ps_actions->pch_name = pch_name;
  gs_model.ps_actions->f_action = f;
  gs_model.ps_actions->ps_args = ps_args;
  gs_model.ps_actions->ui_args_len = ui_args_len;

  return true;
}

LOCAL
bool ICACHE_FLASH_ATTR uconf_register_var(const char* const pch_name,
                                          const e_type_t e_type,
                                          const read_function_t f_reader,
                                          const write_function_t f_writer,
                                          const bool b_remote_write,
                                          const bool b_broadcast) {
  if (!f_reader.valid && !f_writer.valid) {
    return false;
  }
  variable_t* ps_var =
    (variable_t*)uconf_find_by_name((base_t*)gs_model.ps_variables, pch_name);
  if (!ps_var) {
    ps_var = gs_model.ps_variables;
    gs_model.ps_variables = (variable_t*)os_zalloc(sizeof(variable_t));
    gs_model.ps_variables->ps_next = ps_var;
    gs_model.ps_variables->pch_name = pch_name;
    gs_model.ps_variables->e_type = e_type;
    ps_var = gs_model.ps_variables;
  } else {
    if (ps_var->e_type != e_type) {
      return false;
    }
  }
  if (f_reader.valid) {
    ps_var->f_reader = f_reader;
  }
  if (f_writer.valid) {
    ps_var->f_writer = f_writer;
    ps_var->b_remote_write = b_remote_write;
    ps_var->b_broadcast = b_broadcast;
  }
  return true;
}

bool ICACHE_FLASH_ATTR
uconf_register_read_uint8(const char* const pch_name,
                          const f_uconf_read_uint8_t f_reader) {
  read_function_t f_r = { .ui8 = f_reader };
  write_function_t f_w = { .valid = NULL };
  return uconf_register_var(pch_name, eUint8, f_r, f_w, false, false);
}

bool ICACHE_FLASH_ATTR
uconf_register_write_uint8(const char* const pch_name,
                           const f_uconf_write_uint8_t f_writer,
                           const bool b_remote_write,
                           const bool b_broadcast) {
  read_function_t f_r = { .valid = NULL };
  write_function_t f_w = { .ui8 = f_writer };
  return uconf_register_var(pch_name,
                            eUint8,
                            f_r,
                            f_w,
                            b_remote_write,
                            b_broadcast);
}

bool ICACHE_FLASH_ATTR
uconf_register_read_int(const char* const pch_name,
                        const f_uconf_read_int_t f_reader) {
  read_function_t f_r = { .i = f_reader };
  write_function_t f_w = { .valid = NULL };
  return uconf_register_var(pch_name, eInt, f_r, f_w, false, false);
}

bool ICACHE_FLASH_ATTR
uconf_register_write_int(const char* const pch_name,
                         const f_uconf_write_int_t f_writer,
                         const bool b_remote_write,
                         const bool b_broadcast) {
  read_function_t f_r = { .valid = NULL };
  write_function_t f_w = { .i = f_writer };
  return uconf_register_var(pch_name,
                            eInt,
                            f_r,
                            f_w,
                            b_remote_write,
                            b_broadcast);
}

bool ICACHE_FLASH_ATTR
uconf_register_read_cstr(const char* const pch_name,
                         const f_uconf_read_cstr_t f_reader) {
  read_function_t f_r = { .s = f_reader };
  write_function_t f_w = { .valid = NULL };
  return uconf_register_var(pch_name, eString, f_r, f_w, false, false);
}

bool ICACHE_FLASH_ATTR
uconf_register_write_cstr(const char* const pch_name,
                          const f_uconf_write_cstr_t f_writer,
                          const bool b_remote_write,
                          const bool b_broadcast) {
  read_function_t f_r = { .valid = NULL };
  write_function_t f_w = { .s = f_writer };
  return uconf_register_var(pch_name,
                            eString,
                            f_r,
                            f_w,
                            b_remote_write,
                            b_broadcast);
}

#if !defined(SCHEMA_BUFF_LEN)
 #define SCHEMA_BUFF_LEN 1024
#endif // !defined(SCHEMA_BUFF_LEN)

LOCAL
const char* ICACHE_FLASH_ATTR uconf_show_type(e_type_t e) {
  switch (e) {
  case eUint8:
    return "UINT8";
  case eInt:
    return "INT";
  case eString:
    return "STRING";
  default:
    return "UNKNOWN";
  }
}

/* write out json schema of data model, e.g.
 * {
 *     'DATA' : {
 *         'varname1' : {
 *             'READ' : true,
 *             'WRITE' : true,
 *             'TYPE' : 'INT'
 *         },
 *     }
 * }
 */
LOCAL
bool ICACHE_FLASH_ATTR schema_handler(struct espconn* conn,
                                      struct http_request_context_t* ctx) {
  char* pch_buff = (char*)os_malloc(SCHEMA_BUFF_LEN);
  if (!pch_buff) {
    os_printf(__FILE__ ": failed to allocated schema buffer\n");
    // return false to produce a 404
    return false;
  }
  size_t i = os_sprintf(pch_buff,
                        "{\"DATA\":{");
  variable_t* ps_var_current = gs_model.ps_variables;
  while (ps_var_current) {
    // 63 = 48 + ACTION text below, two curly braces and null byte
    if (i + os_strlen(ps_var_current->pch_name) + 63 > SCHEMA_BUFF_LEN) {
      os_free(pch_buff);
      return false;
    }
    i += os_sprintf(pch_buff + i,
                    "%s\"%s\":{\"READ\":%s,\"WRITE\":%s,\"TYPE\":\"%s\"}",
                    ps_var_current == gs_model.ps_variables ? "" : ",",
                    ps_var_current->pch_name,
                    ps_var_current->f_reader.valid ? "true" : "false",
                    ((ps_var_current->f_writer.valid &&
                      ps_var_current->b_remote_write) ? "true" : "false"),
                    uconf_show_type(ps_var_current->e_type));
    ps_var_current = ps_var_current->ps_next;
  }
  i += os_sprintf(pch_buff + i, "},\"ACTION\":{");
  action_t* ps_act_current = gs_model.ps_actions;
  while (ps_act_current) {
    // 9 includes two additional curly braces and null byte
    size_t len = os_strlen(ps_act_current->pch_name) + 9;
    for (int j = 0; j < ps_act_current->ui_args_len; j++) {
      len += 6 + os_strlen(ps_act_current->ps_args[j].pch_name) +
        os_strlen(uconf_show_type(ps_act_current->ps_args[j].e_type));
    }
    if (i + len > SCHEMA_BUFF_LEN) {
      os_free(pch_buff);
      return false;
    }
    i += os_sprintf(pch_buff + i,
                    "%s\"%s\":{",
                    ps_act_current == gs_model.ps_actions ? "" : ",",
                    ps_act_current->pch_name);
    for (int j = 0; j < ps_act_current->ui_args_len; j++) {
      i += os_sprintf(pch_buff + i,
                      "%s\"%s\":\"%s\"",
                      j > 0 ? "," : "",
                      ps_act_current->ps_args[j].pch_name,
                      uconf_show_type(ps_act_current->ps_args[j].e_type));
    }
    i += os_sprintf(pch_buff + i, "}");
    ps_act_current = ps_act_current->ps_next;
  }
  i += os_sprintf(pch_buff + i, "}}");
  int8_t j = http_send_response(conn,
                                200,
                                gpch_json_ct,
                                NULL,
                                0,
                                pch_buff,
                                i,
                                true);
  if (j != ESPCONN_OK) {
    os_printf("failed to write to connection: %d\n", i);
  }
  return true;
}

/* write out value of variable in json, e.g.
 * {
 *     'varname1' : 'value1'
 * }
 * allocates memory to store the result.
 */
LOCAL
char* ICACHE_FLASH_ATTR write_variable(variable_t* ps_var) {
  if (!ps_var || !ps_var->f_reader.valid) {
    return NULL;
  }
  char* pch_buffer = NULL;
  char* pch_tmp = NULL;
  int i = 0;
  switch (ps_var->e_type) {
  case eUint8:
    i = ps_var->f_reader.ui8();
    break;
  case eInt:
    i = ps_var->f_reader.i();
    break;
  case eString:
    pch_tmp = ps_var->f_reader.s();
    pch_buffer = (char*)os_malloc(8 +
                                  os_strlen(ps_var->pch_name) +
                                  os_strlen(pch_tmp));
    if (pch_buffer) {
      os_sprintf(pch_buffer,
                 "{\"%s\":\"%s\"}",
                 ps_var->pch_name,
                 pch_tmp);
    }
    break;
  }

  if (ps_var->e_type <= eInt) {
    pch_buffer = (char*)os_malloc(15 + os_strlen(ps_var->pch_name));
    if (pch_buffer) {
      os_sprintf(pch_buffer,
                 "{\"%s\":%d}",
                 ps_var->pch_name,
                 i);
    }
  }
  return pch_buffer;
}

/*
 * handles requests to /uconf/get?var=name
 */
LOCAL
bool ICACHE_FLASH_ATTR get_handler(struct espconn* conn,
                                   struct http_request_context_t* ctx) {
  const char* pch_name = http_request_context_lookup(ctx, "var");
  variable_t* ps_var =
    (variable_t*)uconf_find_by_name((base_t*)gs_model.ps_variables, pch_name);

  if (ps_var && ps_var->f_reader.valid) {
    char* pch_buff = write_variable(ps_var);
    int8_t j = http_send_response(conn,
                                  200,
                                  gpch_json_ct,
                                  NULL,
                                  0,
                                  pch_buff,
                                  os_strlen(pch_buff),
                                  true);
    if (j != ESPCONN_OK) {
      os_printf(__FILE__ ": failed to write to connection\n");
    }
    return true;
  }
  return false;
}

LOCAL bool ICACHE_FLASH_ATTR uconf_set_data(variable_t* ps_var,
                                            data_t data) {
  bool result = false;

  switch (ps_var->e_type) {
  case eUint8:
    result = ps_var->f_writer.ui8(data.ui8);
    break;
  case eInt:
    result = ps_var->f_writer.i(data.i);
    break;
  case eString:
    result = ps_var->f_writer.s(data.s);
    break;
  }

  if (result && ps_var->b_broadcast) {
    os_printf("Broadcasting %s\n", ps_var->pch_name);
    switch (ps_var->e_type) {
    case eUint8:
      beacon_with_variable_uint8(ps_var->pch_name, data.ui8);
      break;
    case eInt:
      beacon_with_variable_int(ps_var->pch_name, data.i);
      break;
    case eString:
      beacon_with_variable_str(ps_var->pch_name, data.s);
      break;
    }
  }

  return result;
}

LOCAL
const char* ICACHE_FLASH_ATTR uconf_parse_parameter(const e_type_t e_type,
                                                    const char* pch_value,
                                                    data_t* result) {
  char *pch_end_value = NULL;
  long int i = 0;

  if (pch_value) {
    if (e_type <= eInt) {
      i = strtol(pch_value, &pch_end_value, 10);
      if (*pch_end_value != '\0' || pch_end_value == pch_value) {
        return "bad int";
      }
    }

    switch (e_type) {
    case eUint8:
      result->ui8 = i;
      break;
    case eInt:
      result->i = i;
      break;
    case eString:
      result->s = (char*)pch_value;
      break;
    default:
      return "bad type";
    }

    return NULL;
  }
  return "null ptr";
}

/*
 * handles requests to /uconf/set?var=name&val=value
 */
LOCAL
bool ICACHE_FLASH_ATTR set_handler(struct espconn* conn,
                                   struct http_request_context_t* ctx) {
  const char* pch_name = http_request_context_lookup(ctx, "var");
  variable_t* ps_var =
    (variable_t*)uconf_find_by_name((base_t*)gs_model.ps_variables, pch_name);
  const char* pch_value = http_request_context_lookup(ctx, "val");
  data_t data;

  if (ps_var && pch_value && ps_var->f_writer.valid && ps_var->b_remote_write) {
    const char* pch_result = uconf_parse_parameter(ps_var->e_type,
                                                   pch_value,
                                                   &data);
    uint16_t ui_response = 400;
    if (pch_result == NULL) {
      if (uconf_set_data(ps_var, data)) {
        ui_response = 200;
        pch_result = "ok";
      } else {
        pch_result = "fail";
      }
    }

    char* pch_buffer = (char*)os_malloc(50);
    size_t i = os_sprintf(pch_buffer,
                          "{\"result\":\"%s\"}",
                          pch_result);
    int8_t j = http_send_response(conn,
                                  ui_response,
                                  gpch_json_ct,
                                  NULL,
                                  0,
                                  pch_buffer,
                                  i,
                                  true);
    if (j != ESPCONN_OK) {
      os_printf(__FILE__ ": failed to write to connection\n");
    }
    return true;

  }
  return false;
}

/*
 * handles requests to /uconf/invoke?method=name&parami=valuei
 */
LOCAL
bool ICACHE_FLASH_ATTR invoke_handler(struct espconn* conn,
                                      struct http_request_context_t* ctx) {
  const char* pch_name = http_request_context_lookup(ctx, "method");
  action_t* ps_act =
    (action_t*)uconf_find_by_name((base_t*)gs_model.ps_actions, pch_name);
  if (ps_act) {
    data_t* params = (data_t*)os_zalloc(sizeof(data_t)*ps_act->ui_args_len);
    const char* pch_result = NULL;

    for (int i = 0; !pch_result && i < ps_act->ui_args_len; i++) {
      const char* pch_value =
        http_request_context_lookup(ctx, ps_act->ps_args[i].pch_name);
      if (!pch_value) {
        pch_result = "missing parameter";
      } else {
        pch_result = uconf_parse_parameter(ps_act->ps_args[i].e_type,
                                           pch_value,
                                           params + i);
      }
    }

    uint16_t ui_response = 400;
    if (!pch_result) {
      if (ps_act->f_action(ps_act->ui_args_len, params)) {
        pch_result = "ok";
        ui_response = 200;
      } else {
        pch_result = "invoke fail";
      }
    }
    os_free(params);

    char* pch_buffer = (char*)os_malloc(50);
    size_t i = os_sprintf(pch_buffer,
                          "{\"result\":\"%s\"}",
                          pch_result);
    int8_t j = http_send_response(conn,
                                  ui_response,
                                  gpch_json_ct,
                                  NULL,
                                  0,
                                  pch_buffer,
                                  i,
                                  true);
    if (j != ESPCONN_OK) {
      os_printf(__FILE__ ": failed to write to connection\n");
    }
    return true;

  }
  return false;
}

void ICACHE_FLASH_ATTR uconf_register_http() {
  http_register_handler("/uconf/schema", &schema_handler);
  http_register_handler("/uconf/get", &get_handler);
  http_register_handler("/uconf/set", &set_handler);
  http_register_handler("/uconf/invoke", &invoke_handler);
}

bool ICACHE_FLASH_ATTR uconf_var_set_uint8(const char* const pch_name,
                                           const uint8_t ui_value) {
  data_t d = { .ui8 = ui_value};
  variable_t* ps_var =
    (variable_t*)uconf_find_by_name((base_t*)gs_model.ps_variables, pch_name);
  if (ps_var && ps_var->f_writer.valid && ps_var->e_type == eUint8) {
    return uconf_set_data(ps_var, d);
  }
  return false;
}

bool ICACHE_FLASH_ATTR uconf_var_set_int(const char* const pch_name,
                                         const int i_value) {
  data_t d = { .i = i_value};
  variable_t* ps_var =
    (variable_t*)uconf_find_by_name((base_t*)gs_model.ps_variables, pch_name);
  if (ps_var && ps_var->f_writer.valid && ps_var->e_type == eInt) {
    return uconf_set_data(ps_var, d);
  }
  return false;
}

bool ICACHE_FLASH_ATTR uconf_var_set_cstr(const char* const pch_name,
                                          char* const  pch_value) {
  data_t d = { .s = pch_value};
  variable_t* ps_var =
    (variable_t*)uconf_find_by_name((base_t*)gs_model.ps_variables, pch_name);
  if (ps_var && ps_var->f_writer.valid && ps_var->e_type == eString) {
    return uconf_set_data(ps_var, d);
  }
  return false;
}
