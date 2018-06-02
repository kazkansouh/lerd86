#include "osapi.h"
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>

#define NTP_EPOCH_OFFSET 2208988800U

LOCAL struct espconn ntpconn;

#define tcp_connTaskPrio        1

/* Standard NTP frame */
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

LOCAL os_timer_t ntp_t;


LOCAL inline uint32_t ntoh(uint32_t x) {
  return
    ((x & 0xFF) << 24)    |
    ((x & 0xFF00) << 8)   |
    ((x & 0xFF0000) >> 8) |
    (x >> 24);
}

LOCAL
void plus(uint32_t sec1, uint32_t frac1,
          uint32_t sec2, uint32_t frac2,
          uint64_t* res_sec, uint32_t* res_frac) {
  uint64_t f = (uint64_t)frac1 + (uint64_t)frac2;
  *res_frac = f & 0xFFFFFFFF;
  *res_sec = (uint64_t)sec1 + (uint64_t)sec2;
  if (f != *res_frac) {
    (*res_sec)++;
    *res_frac = f - 0xFFFFFFFF;
  }
}

LOCAL
void sub(uint32_t sec1, uint32_t frac1,
         uint32_t sec2, uint32_t frac2,
         uint32_t* res_sec, uint32_t* res_frac) {
  if (frac1 < frac2) {
    uint64_t f = 0xFFFFFFFF + frac1;
    *res_frac = f - frac2;
    *res_sec = sec1 - sec2 - 1;
  } else {
    *res_frac = frac1 - frac2;
    *res_sec = sec1 - sec2;
  }
}

LOCAL void  ICACHE_FLASH_ATTR
ntpconn_recv_callback (void *arg,
                       char *pdata,
                       unsigned short len) {
  uint32_t un_receive      = system_get_time();
  uint32_t un_receive_sec  = un_receive/1000000;
  uint32_t un_receive_frac = (un_receive%1000000) * 0xFFFFFFFF;
  struct espconn *pespconn = (struct espconn *) arg;
  unsigned short i = 0;
  os_printf("Response Received!\n");
  os_timer_disarm(&ntp_t);
  espconn_delete(&ntpconn);
  os_free(ntpconn.proto.udp);

  for (i = 0; i < len; i++) {
    os_printf("%02X ", pdata[i]);
  }

  ntp_payload *res = (ntp_payload*)pdata;

  os_printf("\nTime: %d\n", ntoh(res->transmit_timestamp_sec) - NTP_EPOCH_OFFSET);


  uint32_t t1_sec = 0;
  uint32_t t1_frac = 0;
  uint32_t t2_sec = 0;
  uint32_t t2_frac = 0;
  uint64_t t3_sec = 0;
  uint32_t t3_frac = 0;

  /*
  os_printf("\nOriginate Time: %u\n", ntoh(res->originate_timestamp_sec));
  os_printf("\nReceive Time: %u\n", un_receive_sec);
  */

  sub(ntoh(res->receive_timestamp_sec), ntoh(res->receive_timestamp_frac), ntoh(res->originate_timestamp_sec), ntoh(res->originate_timestamp_frac), &t1_sec, &t1_frac);
  sub(ntoh(res->transmit_timestamp_sec), ntoh(res->transmit_timestamp_frac), un_receive_sec, un_receive_frac, &t2_sec, &t2_frac);
  plus(t1_sec, t1_frac, t2_sec, t2_frac, &t3_sec, &t3_frac);

  /*
  os_printf("\nDouble: %ull ", t3_sec); // ## does not seem to print as uint64_t :(

  for (i = 0; i < sizeof(t3_sec); i++) {
    os_printf("%02X ", ((uint8_t*)&t3_sec)[i]);
  }
  */

  t3_sec = t3_sec / 2;
  t3_frac = t3_frac / 2;

  /*
  os_printf("\nCalculated T1: %u.%u ", t1_sec, t1_frac);
  for (i = 0; i < sizeof(t1_sec); i++) {
    os_printf("%02X ", ((uint8_t*)&t1_sec)[i]);
  }

  os_printf("\nCalculated T2: %u.%u ", t2_sec, t2_frac);
  for (i = 0; i < sizeof(t2_sec); i++) {
    os_printf("%02X ", ((uint8_t*)&t2_sec)[i]);
  }

  os_printf("\nCalculated Time Offset: %u.%u\n", offset, t3_frac);
  os_printf("\nCurrent TimeStamp: %u\n", offset + system_get_time()/1000000);
  */

  uint32_t offset = t3_sec - NTP_EPOCH_OFFSET + (system_get_time() / 1000000);

  uint32_t hours = (offset % 86400) / 3600;
  uint32_t minutes = (offset % 3600) / 60;
  uint32_t seconds = offset % 60;

  os_printf("Time: %02d:%02d:%02d\n", hours, minutes, seconds);

  uint32_t ardtime = offset % 86400;

  uart0_write_char(0xFF);
  uart0_write_char(0x04);
  uart0_write_char(ardtime >> 24);
  uart0_write_char((ardtime & 0x00FF0000) >> 16);
  uart0_write_char((ardtime & 0x0000FF00) >> 8);
  uart0_write_char(ardtime & 0x0FF);

  ((struct espconn *)(pespconn->reverse))->reverse = (void*)ardtime;
  system_os_post(tcp_connTaskPrio, 11, (uint32_t)(pespconn->reverse));
}

LOCAL void ICACHE_FLASH_ATTR request(void* arg) {
  os_printf("Sending request!\n");
  // build requiest
  ntp_payload req;
  req.flags                    = 0xe3;
  req.stratum                  = 0;
  req.poll                     = 3;
  req.precision                = 0xFA;
  req.root_delay_sec           = 1;
  req.root_delay_frac          = 0;
  req.root_dispersion_sec      = 1;
  req.root_dispersion_frac     = 0;
  req.reference_identifier     = 0;
  req.reference_timestamp_sec  = 0;
  req.reference_timestamp_frac = 0;
  req.originate_timestamp_sec  = 0;
  req.originate_timestamp_frac = 0;
  req.receive_timestamp_sec    = 0;
  req.receive_timestamp_frac   = 0;
  os_timer_arm(&ntp_t,250,0);
  uint32_t t = system_get_time();
  req.transmit_timestamp_sec   = ntoh(t/1000000);
  req.transmit_timestamp_frac  = ntoh((t%1000000) * 0xFFFFFFFF);
  espconn_sendto(&ntpconn, (uint8*)(&req), sizeof(ntp_payload));
}

bool ICACHE_FLASH_ATTR requestTime(void *arg) {

  ntpconn.type = ESPCONN_UDP;
  ntpconn.state = ESPCONN_NONE;
  ntpconn.proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
  ntpconn.proto.udp->remote_port = 123;
  ntpconn.proto.udp->remote_ip[0] = 178;
  ntpconn.proto.udp->remote_ip[1] = 79;
  ntpconn.proto.udp->remote_ip[2] = 162;
  ntpconn.proto.udp->remote_ip[3] = 34;
  ntpconn.reverse = arg;

  espconn_regist_recvcb(&ntpconn, ntpconn_recv_callback);

  if (espconn_create(&ntpconn) != 0) {
    os_printf("Failed to create connection\n");
    return false;
  }

  os_timer_setfn(&ntp_t, request , NULL);

  request(NULL);

  return true;
}
