
//#include "ets_sys.h"
#include "osapi.h"
//#include "gpio.h"
//#include "os_type.h"
//#include <driver/uart.h>
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>

#define NTP_EPOCH_OFFSET 2208988800U

LOCAL struct espconn ntpconn;

typedef struct __attribute__((packed)) {
  uint8_t flags;
  uint8_t stratum;
  uint8_t poll;
  uint8_t precision;
  uint16_t root_delay_sec;
  uint16_t root_delay_frac;
  uint16_t root_dispersion_sec;
  uint16_t root_dispersion_frac;
  uint32_t reference_identifier;
  uint32_t reference_timestamp_sec;
  uint32_t reference_timestamp_frac;
  uint32_t originate_timestamp_sec;
  uint32_t originate_timestamp_frac;
  uint32_t receive_timestamp_sec;
  uint32_t receive_timestamp_frac;
  uint32_t transmit_timestamp_sec;
  uint32_t transmit_timestamp_frac;
} ntp_payload;

LOCAL volatile os_timer_t ntp_t;


LOCAL inline uint32_t ntoh(uint32_t x) {
  return
    ((x & 0xFF) << 24)    |
    ((x & 0xFF00) << 8)   |
    ((x & 0xFF0000) >> 8) |
    (x >> 24);
}

LOCAL void  ICACHE_FLASH_ATTR 
ntpconn_recv_callback (void *arg,
		       char *pdata,
		       unsigned short len) {
  uint32_t un_receive = system_get_time();
  struct espconn *pespconn = (struct espconn *) arg;
  unsigned short i = 0;
  os_printf("Response Received!\n");
  os_timer_disarm(&ntp_t);

  for (i = 0; i < len; i++) {
    os_printf("%02X ", pdata[i]);
  }

  ntp_payload *p = (ntp_payload*)pdata;

  os_printf("\nTime: %d\n", ntoh(p->transmit_timestamp_sec) - NTP_EPOCH_OFFSET);

}


LOCAL void ICACHE_FLASH_ATTR request(void* arg) {
  os_printf("Sending request!\n");
  // build requiest
  ntp_payload req;
  req.flags		       = 0xe3;
  req.stratum		       = 0;
  req.poll		       = 3;
  req.precision		       = 0xFA;
  req.root_delay_sec	       = 1;
  req.root_delay_frac	       = 0;
  req.root_dispersion_sec      = 1;
  req.root_dispersion_frac     = 0;
  req.reference_identifier     = 0;
  req.reference_timestamp_sec  = 0;
  req.reference_timestamp_frac = 0;
  req.originate_timestamp_sec  = 0;
  req.originate_timestamp_frac = 0;
  req.receive_timestamp_sec    = 0;
  req.receive_timestamp_frac   = 0;
  os_timer_arm(&ntp_t,200,0);
  uint32_t t = system_get_time();
  req.transmit_timestamp_sec   = t/1000000;
  req.transmit_timestamp_frac  = (t%1000000) * 0xFFFFFFFF;
  espconn_sendto(&ntpconn, (uint8*)(&req), sizeof(ntp_payload));
}

void ICACHE_FLASH_ATTR requestTime() {

  ntpconn.type = ESPCONN_UDP;
  ntpconn.state = ESPCONN_NONE;
  ntpconn.proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
  ntpconn.proto.udp->remote_port = 123;
  ntpconn.proto.udp->remote_ip[0] = 178;
  ntpconn.proto.udp->remote_ip[1] = 79;
  ntpconn.proto.udp->remote_ip[2] = 162;
  ntpconn.proto.udp->remote_ip[3] = 34;

  espconn_regist_recvcb(&ntpconn, ntpconn_recv_callback);

  if (espconn_create(&ntpconn) != 0) {
    os_printf("Failed to create connection\n");
    return;
  }

  os_timer_setfn(&ntp_t, request , NULL);

  request(NULL);


  // TODO: Register callbacks
  //       Initilise connection
  // 

}
