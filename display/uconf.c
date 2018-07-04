#include <c_types.h>
#include <osapi.h>
#include <mem.h>
#include <ip_addr.h>
#include <espconn.h>
#include <stdlib.h>

#include "http.h"
#include "uconf.h"

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

typedef enum {
  eInt,
  eString,
} e_type_t;

typedef union {
  void* valid;
  f_uconf_read_int_t i;
  f_uconf_read_cstr_t s;
} read_function_t;

typedef union {
  void* valid;
  f_uconf_write_int_t i;
  f_uconf_write_cstr_t s;
} write_function_t;

typedef struct variable_t {
  struct variable_t* ps_next;
  const char* pch_name;
  e_type_t e_type;
  read_function_t f_reader;
  write_function_t f_writer;
} variable_t;

typedef struct action_t {
  struct action_t* ps_next;
  const char* pch_name;
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

LOCAL
bool ICACHE_FLASH_ATTR uconf_register_var(const char* const pch_name,
                                          const e_type_t e_type,
                                          const read_function_t f_reader,
                                          const write_function_t f_writer) {
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
  }
  return true;
}

bool ICACHE_FLASH_ATTR
uconf_register_read_int(const char* const pch_name,
                        const f_uconf_read_int_t f_reader) {
  read_function_t f_r = { .i = f_reader };
  write_function_t f_w = { .valid = NULL };
  return uconf_register_var(pch_name, eInt, f_r, f_w);
}

bool ICACHE_FLASH_ATTR
uconf_register_write_int(const char* const pch_name,
                         const f_uconf_write_int_t f_writer) {
  read_function_t f_r = { .valid = NULL };
  write_function_t f_w = { .i = f_writer };
  return uconf_register_var(pch_name, eInt, f_r, f_w);
}

#if !defined(SCHEMA_BUFF_LEN)
 #define SCHEMA_BUFF_LEN 1024
#endif // !defined(SCHEMA_BUFF_LEN)

LOCAL
const char* ICACHE_FLASH_ATTR uconf_show_type(e_type_t e) {
  switch (e) {
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
  variable_t* ps_current = gs_model.ps_variables;
  while (ps_current) {
    // 51 = 48 + two closing curly braces and null byte
    if (i + os_strlen(ps_current->pch_name) + 51 > SCHEMA_BUFF_LEN) {
      os_free(pch_buff);
      return false;
    }
    i += os_sprintf(pch_buff + i,
                    "%s\"%s\":{\"READ\":%s,\"WRITE\":%s,\"TYPE\":\"%s\"}",
                    ps_current == gs_model.ps_variables ? "" : ",",
                    ps_current->pch_name,
                    ps_current->f_reader.valid ? "true" : "false",
                    ps_current->f_writer.valid ? "true" : "false",
                    uconf_show_type(ps_current->e_type));
    ps_current = ps_current->ps_next;
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
  switch (ps_var->e_type) {
  case eInt:
    pch_buffer = (char*)os_malloc(15 + os_strlen(ps_var->pch_name));
    if (pch_buffer) {
      os_sprintf(pch_buffer,
                 "{\"%s\":%d}",
                 ps_var->pch_name,
                 ps_var->f_reader.i());
    }
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

  if (ps_var && pch_value && ps_var->f_writer.valid) {
    const char* pch_result = "fail";
    switch (ps_var->e_type) {
    case eInt: {
      char *endcode = NULL;
      long int i = strtol(pch_value, &endcode, 10);
      if (*endcode == '\0' && endcode != pch_value) {
        if (ps_var->f_writer.i(i)) {
          pch_result = NULL;
        }
      } else {
        pch_result = "bad int";
      }
    }
      break;
    case eString:
      if (ps_var->f_writer.s((char*)pch_value)) {
        pch_result = NULL;
      }
      break;
    default:
      pch_result = "bad type";
    }
    char* pch_buffer = (char*)os_malloc(50);
    uint16_t ui_response = pch_result == NULL ? 200 : 400;
    size_t i = os_sprintf(pch_buffer,
                          "{\"result\":\"%s\"}",
                          pch_result == NULL ? "ok" : pch_result);
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
}
