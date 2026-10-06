#include "dhcp_dns.h"
#include <string.h>

struct options {
    unsigned type, overload, dns_count, server_count;
    uint8_t server[4];
};

static int scan(const uint8_t *p, size_t n, struct options *o, int primary) {
    for (size_t i = 0; i < n;) {
        unsigned tag = p[i++];
        if (tag == 0) continue;
        if (tag == 255) return 1;
        if (i == n) return 0;
        unsigned len = p[i++];
        if (len > n - i) return 0;
        if (tag == 6) {
            if (len < 4 || len % 4) return 0;
            o->dns_count += len / 4;
        } else if (tag == 53) {
            if (!primary || len != 1 || o->type || p[i] < 1 || p[i] > 8) return 0;
            o->type = p[i];
        } else if (tag == 52) {
            if (!primary || len != 1 || o->overload || !p[i] || p[i] > 3) return 0;
            o->overload = p[i];
        } else if (tag == 54) {
            if (len != 4 || o->server_count) return 0;
            memcpy(o->server, p + i, 4);
            o->server_count = 1;
        }
        i += len;
    }
    return 0; /* Require END in each option area. */
}

static int parse(const uint8_t *p, size_t n, struct options *o) {
    static const uint8_t cookie[4] = {99, 130, 83, 99};
    memset(o, 0, sizeof(*o));
    if (n < 241 || p[0] != 2 || p[1] != 1 || p[2] != 6 ||
        memcmp(p + 236, cookie, 4)) return 0;
    if (!scan(p + 240, n - 240, o, 1)) return -1;
    if ((o->overload & 1) && !scan(p + 108, 128, o, 0)) return -1;
    if ((o->overload & 2) && !scan(p + 44, 64, o, 0)) return -1;
    if (o->type != 2 && o->type != 5) return 0; /* OFFER and ACK only. */
    return 1;
}

int hpd_server_id(const uint8_t *p, size_t n, uint8_t server[4]) {
    struct options o;
    if (parse(p, n, &o) != 1 || !o.server_count) return 0;
    memcpy(server, o.server, 4);
    return 1;
}

static int replace(uint8_t *p, size_t n, const uint8_t dns[4]) {
    int changed = 0;
    for (size_t i = 0; i < n;) {
        unsigned tag = p[i++];
        if (tag == 0) continue;
        if (tag == 255) break;
        unsigned len = p[i++];
        if (tag == 6) {
            for (size_t j = 0; j < len; j += 4) {
                if (memcmp(p + i + j, dns, 4)) changed = 1;
                memcpy(p + i + j, dns, 4);
            }
        }
        i += len;
    }
    return changed;
}

int hpd_rewrite_dhcp(uint8_t *p, size_t n, const uint8_t dns[4], const uint8_t *server) {
    struct options o;
    int r = parse(p, n, &o);
    if (r != 1) return r;
    if (!o.dns_count) return 0;
    if (server && (!o.server_count || memcmp(server, o.server, 4))) return 0;
    int changed = replace(p + 240, n - 240, dns);
    if (o.overload & 1) changed |= replace(p + 108, 128, dns);
    if (o.overload & 2) changed |= replace(p + 44, 64, dns);
    return changed;
}

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] * 256 + p[1]; }
static uint32_t sum16(uint32_t s, const uint8_t *p, size_t n) {
    for (; n >= 2; p += 2, n -= 2) s += be16(p);
    if (n) s += (uint32_t)p[0] << 8;
    return s;
}

int hpd_rewrite_ethernet(uint8_t *p, size_t n, const uint8_t dns[4], const uint8_t *server) {
    if (n < 14) return 0;
    size_t off = 14;
    unsigned type = be16(p + 12);
    unsigned tags = 0;
    while ((type == 0x8100 || type == 0x88a8) && tags++ < 2) {
        if (n - off < 4) return -1;
        type = be16(p + off + 2);
        off += 4;
    }
    if (type != 0x0800) return 0;
    if (n - off < 20) return -1;
    uint8_t *ip = p + off;
    if ((ip[0] >> 4) != 4 || ip[9] != 17) return 0;
    size_t ihl = (ip[0] & 15) * 4;
    size_t total = be16(ip + 2);
    if (ihl < 20 || total < ihl + 8 || total > n - off) return -1;
    if (be16(ip + 6) & 0x3fff) return 0; /* No fragments. */
    uint8_t *udp = ip + ihl;
    if (be16(udp) != 67 || be16(udp + 2) != 68) return 0;
    size_t ulen = be16(udp + 4);
    if (ulen < 8 || ulen != total - ihl) return -1;
    int had_checksum = udp[6] || udp[7];
    int r = hpd_rewrite_dhcp(udp + 8, ulen - 8, dns, server);
    if (r == 1 && had_checksum) {
        udp[6] = udp[7] = 0;
        uint32_t s = sum16(0, ip + 12, 8) + 17 + (uint32_t)ulen;
        s = sum16(s, udp, ulen);
        while (s >> 16) s = (s & 65535) + (s >> 16);
        unsigned result = (~s) & 65535;
        if (!result) result = 65535;
        udp[6] = (uint8_t)(result >> 8);
        udp[7] = (uint8_t)result;
    }
    return r;
}
