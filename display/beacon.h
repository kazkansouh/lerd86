#ifndef _BEACON_H
#define _BEACON_H

bool beacon_init(uint32_t ui_period, bool b_bcast);
void beacon_deinit(void);

bool beacon_with_variable_str(const char* pch_name,
                              const char* pch_value);
bool beacon_with_variable_int(const char* pch_name,
                              const int i_value);
bool beacon_with_variable_uint8(const char* pch_name,
                                const uint8 ui_value);

#endif // _BEACON_H
