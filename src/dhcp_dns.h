#ifndef HOTSPOT_DHCP_DNS_H
#define HOTSPOT_DHCP_DNS_H
#include <stddef.h>
#include <stdint.h>
/* Return 1 when changed, 0 when unchanged/irrelevant, -1 when malformed.
 * dns and server are four bytes in network order; server may be NULL.
 * Every failure leaves the caller's packet byte-for-byte unchanged. */
int hpd_rewrite_dhcp(uint8_t *packet, size_t length,
                     const uint8_t dns[4], const uint8_t *server);
int hpd_rewrite_ethernet(uint8_t *frame, size_t length,
                         const uint8_t dns[4], const uint8_t *server);
int hpd_server_id(const uint8_t *packet, size_t length, uint8_t server[4]);
#endif
