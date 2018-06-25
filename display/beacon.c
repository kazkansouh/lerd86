#include <c_types.h>
#include <ip_addr.h>
#include <espconn.h>
#include <osapi.h>
#include <user_interface.h>

#if !defined(BEACON_PORT)
  #define BEACON_PORT 8003
#endif

#if !defined(BEACON_NAME)
  #define BEACON_NAME "PureGym"
#endif

typedef struct espconn espconn_t;

LOCAL os_timer_t gs_beacon_timer = {0};

LOCAL esp_udp gs_beacon_udp = {0};
LOCAL espconn_t gs_beacon_conn = {0};

LOCAL const char gpch_template[] =
  "{\"beacon\":"
    "{\"api\":\"/uconf/\","
     "\"id\":\"%02X:%02X:%02X\","
     "\"name\":\"" BEACON_NAME
  "\"}}";

void ICACHE_FLASH_ATTR beacon_deinit() {
  os_timer_disarm(&gs_beacon_timer);
  if (gs_beacon_conn.proto.udp) {
    os_printf("cleanup udp connection\n");
    espconn_delete(&gs_beacon_conn);
  }
  os_memset(&gs_beacon_conn, 0x00, sizeof(espconn_t));
}

LOCAL void ICACHE_FLASH_ATTR beacon_timer(void *arg) {
  os_printf("beacon transmit\n");
  char pch_buffer[sizeof(gpch_template)];
  uint8_t mac[6];
  wifi_get_macaddr(STATION_IF, mac);
  size_t i = os_sprintf(pch_buffer, gpch_template, mac[3], mac[4], mac[5]);
  if (espconn_sendto(&gs_beacon_conn, (uint8_t*)pch_buffer, i)
      != ESPCONN_OK) {
    os_printf("error sending beacon\n");
    beacon_deinit();
  };
}

void ICACHE_FLASH_ATTR beacon_init(uint32_t ui_period) {
  beacon_deinit();
  os_timer_setfn(&gs_beacon_timer, beacon_timer , NULL);

  os_memset(&gs_beacon_conn, 0x00, sizeof(espconn_t));
  os_memset(&gs_beacon_udp, 0x00, sizeof(esp_udp));
  gs_beacon_conn.type = ESPCONN_UDP;
  gs_beacon_conn.state = ESPCONN_NONE;
  gs_beacon_conn.proto.udp = &gs_beacon_udp;
  gs_beacon_conn.proto.udp->local_port = espconn_port();
  gs_beacon_conn.proto.udp->remote_port = BEACON_PORT;
  gs_beacon_conn.proto.udp->remote_ip[0] = 255;
  gs_beacon_conn.proto.udp->remote_ip[1] = 255;
  gs_beacon_conn.proto.udp->remote_ip[2] = 255;
  gs_beacon_conn.proto.udp->remote_ip[3] = 255;

  int8 i = espconn_create(&gs_beacon_conn);
  if (i != 0) {
    os_printf("failed to make udp connection\n");
    return;
  }
  os_timer_arm(&gs_beacon_timer, ui_period, 1);
}
