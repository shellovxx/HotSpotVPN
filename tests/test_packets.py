"""Tests the actual C parser through ctypes; build dhcp_dns_test.dll first."""
import ctypes, ipaddress, random, struct, sys, unittest
from pathlib import Path
DLL = ctypes.CDLL(str(Path(sys.argv.pop(1)).resolve()))
PTR = ctypes.POINTER(ctypes.c_uint8)
for name in ('hpd_rewrite_dhcp','hpd_rewrite_ethernet'):
    fn = getattr(DLL,name)
    fn.argtypes = [PTR,ctypes.c_size_t,PTR,PTR]
    fn.restype = ctypes.c_int
DNS = ipaddress.IPv4Address('1.1.1.1').packed
SERVER = ipaddress.IPv4Address('172.20.10.1').packed
OLD = ipaddress.IPv4Address('10.0.0.53').packed

def option(tag, value): return bytes((tag,len(value))) + value
def packet(options=None, kind=5):
    p = bytearray(240)
    p[:3] = bytes((2,1,6))
    p[236:240] = bytes((99,130,83,99))
    if options is None: options = option(6,OLD)
    return p + option(53,bytes((kind,))) + option(54,SERVER) + options + b'\xff'
def rewrite(p, frame=False, server=SERVER):
    buf = (ctypes.c_uint8 * len(p)).from_buffer_copy(p)
    dns = (ctypes.c_uint8 * 4).from_buffer_copy(DNS)
    srv = None if server is None else (ctypes.c_uint8 * 4).from_buffer_copy(server)
    r = getattr(DLL,'hpd_rewrite_ethernet' if frame else 'hpd_rewrite_dhcp')(buf,len(p),dns,srv)
    return r,bytes(buf)
def checksum(data):
    if len(data) % 2: data += b'\0'
    s = sum(struct.unpack('!'+'H'*(len(data)//2),data))
    while s >> 16: s = (s & 65535) + (s >> 16)
    return (~s) & 65535
def frame(p, checksum_on=True, vlan=0, ip_options=b''):
    udp = bytearray(struct.pack('!HHHH',67,68,len(p)+8,0) + p)
    src, dst = SERVER,b'\xff'*4
    if checksum_on:
        c = checksum(src+dst+b'\0\x11'+struct.pack('!H',len(udp))+udp)
        udp[6:8] = struct.pack('!H',c or 65535)
    ip = bytearray(struct.pack('!BBHHHBBH4s4s',0x45+len(ip_options)//4,0,20+len(ip_options)+len(udp),7,0,64,17,0,src,dst)+ip_options)
    ip[10:12] = struct.pack('!H',checksum(ip))
    eth = b'\xff'*6+b'\x02\0\0\0\0\x01'
    for _ in range(vlan): eth += b'\x81\x00\x00\x01'
    eth += b'\x08\x00'
    return eth+ip+udp

class PacketTests(unittest.TestCase):
    def assert_unchanged(self,p,frame=False,expected=None,server=SERVER):
        r,out = rewrite(p,frame,server)
        if expected is not None: self.assertEqual(r,expected)
        self.assertEqual(out,bytes(p))
    def test_offer_and_ack(self):
        for kind in (2,5):
            p = packet(kind=kind)
            r,out = rewrite(p)
            self.assertEqual(r,1)
            self.assertEqual(out,p.replace(OLD,DNS))
            self.assertEqual(len(out),len(p))
    def test_multiple_dns_no_carrier_fallback(self):
        p = packet(option(6,OLD+b'\x08\x08\x08\x08'))
        r,out = rewrite(p)
        self.assertEqual(r,1)
        self.assertIn(option(6,DNS*2),out)
        self.assertNotIn(OLD,out)
    def test_already_selected(self): self.assert_unchanged(packet(option(6,DNS)),expected=0)
    def test_non_reply(self):
        p=packet(); p[0]=1; self.assert_unchanged(p,expected=0)
    def test_non_ethernet(self):
        p=packet(); p[1]=6; self.assert_unchanged(p,expected=0)
    def test_nak(self): self.assert_unchanged(packet(kind=6),expected=0)
    def test_bad_cookie(self):
        p=packet(); p[236]=0; self.assert_unchanged(p,expected=0)
    def test_server_mismatch(self): self.assert_unchanged(packet(),expected=0,server=b'\x0a\0\0\x01')
    def test_no_dns(self): self.assert_unchanged(packet(b''),expected=0)
    def test_no_server_with_scope(self):
        p=packet().replace(option(54,SERVER),b'')
        self.assert_unchanged(p,expected=0)
    def test_invalid_dns_lengths(self):
        for n in (0,1,2,3,5,6,7): self.assert_unchanged(packet(option(6,bytes(n))),expected=-1)
    def test_truncated_tlv_after_dns_is_atomic(self):
        p=packet()[:-1]+b'\x42\x08\x00'
        self.assert_unchanged(p,expected=-1)
    def test_no_end(self): self.assert_unchanged(packet()[:-1],expected=-1)
    def test_pad(self):
        p=packet(b'\0\0'+option(6,OLD)+b'\0')
        self.assertEqual(rewrite(p)[1],p.replace(OLD,DNS))
    def test_duplicate_type(self): self.assert_unchanged(packet(option(53,b'\x05')+option(6,OLD)),expected=-1)
    def test_zero_type_cannot_hide_duplicate(self):
        p=packet(option(6,OLD)).replace(option(53,b'\x05'),option(53,b'\0')+option(53,b'\x05'))
        self.assert_unchanged(p,expected=-1)
    def test_duplicate_dns(self):
        p=packet(option(6,OLD)*2)
        self.assertEqual(rewrite(p)[1],p.replace(OLD,DNS))
    def test_overload_both_fields(self):
        p=packet(option(52,b'\x03')+option(6,OLD))
        field=option(6,OLD)+b'\xff'
        p[44:44+len(field)]=field
        p[108:108+len(field)]=field
        self.assertEqual(rewrite(p)[1],p.replace(OLD,DNS))
    def test_bad_overload_leaves_every_field_unchanged(self):
        p=packet(option(52,b'\x01')+option(6,OLD)); p[108:236]=b'\0'*128
        self.assert_unchanged(p,expected=-1)
    def test_ethernet_checksums(self):
        for vlan in (0,1,2):
            for ip_options in (b'',b'\x01\x01\x01\x00'):
                p=frame(packet(),vlan=vlan,ip_options=ip_options)
                r,out=rewrite(p,True)
                self.assertEqual(r,1)
                off=14+4*vlan; ihl=(out[off]&15)*4
                self.assertEqual(out[off:off+ihl],p[off:off+ihl])
                udp=out[off+ihl:]
                pseudo=out[off+12:off+20]+b'\0\x11'+struct.pack('!H',len(udp))
                self.assertEqual(checksum(pseudo+udp),0)
                self.assertIn(DNS,out)
    def test_zero_udp_checksum_preserved(self):
        r,out=rewrite(frame(packet(),False),True)
        self.assertEqual(r,1); self.assertEqual(out[40:42],b'\0\0')
    def test_odd_udp_length(self):
        p=frame(packet()+b'\0'); r,out=rewrite(p,True)
        self.assertEqual(r,1)
        udp=out[34:]; pseudo=out[26:34]+b'\0\x11'+struct.pack('!H',len(udp))
        self.assertEqual(checksum(pseudo+udp),0)
    def test_ipv6_unchanged(self):
        p=bytearray(frame(packet())); p[12:14]=b'\x86\xdd'; self.assert_unchanged(p,True,0)
    def test_fragments_unchanged(self):
        p=bytearray(frame(packet())); p[20:22]=b'\x20\x00'; self.assert_unchanged(p,True,0)
    def test_wrong_port_unchanged(self):
        p=bytearray(frame(packet())); p[36:38]=b'\x00\x35'; self.assert_unchanged(p,True,0)
    def test_truncated_frames(self):
        p=frame(packet())
        for n in range(len(p)): self.assert_unchanged(p[:n],True)
    def test_truncated_dhcp(self):
        p=packet()
        for n in range(len(p)-1): self.assert_unchanged(p[:n])
    def test_fuzz_failures_never_partially_mutate(self):
        rng=random.Random(1661)
        for i in range(10000):
            p=packet(bytes(rng.randrange(256) for _ in range(rng.randrange(90))))
            r,out=rewrite(p)
            if r != 1: self.assertEqual(out,p)
            else: self.assertEqual(len(out),len(p))
            raw=bytes(rng.randrange(256) for _ in range(rng.randrange(400)))
            r,out=rewrite(raw,True)
            if r != 1: self.assertEqual(out,raw)

if __name__=='__main__': unittest.main()
