"""Bounded DNS/STUN/NTP probes over a selected Windows Ethernet interface.

Npcap injection avoids interference from a separate Windows VPN. Only responses
matching our client address, source ports and transaction tokens are reported.
No packet capture or credentials are saved by this script.
"""
import argparse, ctypes as C, ipaddress, json, os, secrets, socket, struct, time
from pathlib import Path

def checksum(b):
    if len(b)%2: b+=b'\0'
    total=sum(struct.unpack('!'+str(len(b)//2)+'H',b))
    while total>>16: total=(total&65535)+(total>>16)
    return (~total)&65535

def mac(text):
    b=bytes.fromhex(text.replace(':','').replace('-',''))
    if len(b)!=6: raise ValueError('MAC must contain six octets')
    return b

def probes(google_stun):
    tests=[]
    for dns,domain in [('1.1.1.1','youtube.com'),('8.8.8.8','youtube.com'),('1.1.1.1','example.com')]:
        ident=secrets.token_bytes(2)
        query=ident+struct.pack('!HHHHH',0x100,1,0,0,0)+b''.join(bytes((len(x),))+x.encode() for x in domain.split('.'))+b'\0'+struct.pack('!HH',1,1)
        tests.append(dict(protocol='dns',host=dns,port=53,domain=domain,token=ident,data=query))
    for host,port in [(google_stun,19302),('162.159.207.0',3478),('162.159.207.0',53)]:
        token=secrets.token_bytes(12)
        tests.append(dict(protocol='stun',host=host,port=port,token=token,data=struct.pack('!HHI',1,0,0x2112a442)+token))
    for host in ['162.159.200.123','162.159.200.1']:
        token=struct.pack('!II',int(time.time())+2208988800,secrets.randbits(32))
        tests.append(dict(protocol='ntp',host=host,port=123,token=token,data=bytes((0x23,))+bytes(39)+token))
    return tests

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for arg in ['adapter-guid','client-ip','client-mac','gateway-mac','output']: parser.add_argument('--'+arg,required=True)
    parser.add_argument('--google-stun',default='74.125.250.129',help='Resolve stun.l.google.com via an independent resolver before use')
    parser.add_argument('--label',default='hotspot-vpn')
    args=parser.parse_args()
    client=ipaddress.IPv4Address(args.client_ip).packed
    os.add_dll_directory(r'C:\Windows\System32\Npcap')
    lib=C.CDLL(r'C:\Windows\System32\Npcap\wpcap.dll')
    lib.pcap_open_live.argtypes=[C.c_char_p,C.c_int,C.c_int,C.c_int,C.c_char_p];lib.pcap_open_live.restype=C.c_void_p
    lib.pcap_sendpacket.argtypes=[C.c_void_p,C.c_void_p,C.c_int]
    lib.pcap_next_ex.argtypes=[C.c_void_p,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)]
    lib.pcap_setnonblock.argtypes=[C.c_void_p,C.c_int,C.c_char_p]
    lib.pcap_close.argtypes=[C.c_void_p]
    err=C.create_string_buffer(256)
    name=('\\Device\\NPF_'+args.adapter_guid).encode()
    handle=lib.pcap_open_live(name,65535,0,100,err)
    if not handle: raise RuntimeError(err.value.decode())
    if lib.pcap_setnonblock(handle,1,err): lib.pcap_close(handle); raise RuntimeError(err.value.decode())
    tests=probes(args.google_stun);pending={};results=[]
    for i,test in enumerate(tests):
        target=ipaddress.IPv4Address(test['host']).packed;port=55000+i
        udp=struct.pack('!HHHH',port,test['port'],len(test['data'])+8,0)+test['data']
        pseudo=client+target+b'\0\x11'+struct.pack('!H',len(udp))
        udp=udp[:6]+struct.pack('!H',checksum(pseudo+udp) or 65535)+udp[8:]
        ip=struct.pack('!BBHHHBBH4s4s',0x45,0,20+len(udp),0xd900+i,0,64,17,0,client,target)
        ip=ip[:10]+struct.pack('!H',checksum(ip))+ip[12:]
        test['frame']=mac(args.gateway_mac)+mac(args.client_mac)+b'\x08\0'+ip+udp
        test['sent']=0;pending[port]=test
    started=time.monotonic();schedule=[0,1,3]
    try:
        while pending and time.monotonic()-started<10:
            elapsed=time.monotonic()-started
            for test in pending.values():
                attempts=len(schedule)
                if test['sent']<attempts and elapsed>=schedule[test['sent']]:
                    if lib.pcap_sendpacket(handle,test['frame'],len(test['frame'])): raise RuntimeError('send failed')
                    test['sent']+=1
            header=C.c_void_p();packet=C.c_void_p()
            status=lib.pcap_next_ex(handle,C.byref(header),C.byref(packet))
            if status<0: raise RuntimeError('capture failed')
            if not status: time.sleep(.01);continue
            n=struct.unpack('<IIII',C.string_at(header,16))[2];b=C.string_at(packet,n)
            if len(b)<42 or b[12:14]!=b'\x08\0' or b[23]!=17 or b[30:34]!=client: continue
            off=14+(b[14]&15)*4
            if len(b)<off+8:continue
            sport,dport,ulen,ck=struct.unpack('!HHHH',b[off:off+8])
            test=pending.get(dport)
            if not test or sport!=test['port'] or b[26:30]!=socket.inet_aton(test['host']) or ulen<8 or len(b)<off+ulen:continue
            data=b[off+8:off+ulen];r={k:test[k] for k in ['protocol','host','port']}
            if 'domain' in test:r['domain']=test['domain']
            if test['protocol']=='dns':
                if len(data)<12 or data[:2]!=test['token'] or not(data[2]&128):continue
                r.update(rcode=data[3]&15,answers=struct.unpack('!H',data[6:8])[0],truncated=bool(data[2]&2))
            elif test['protocol']=='stun':
                if len(data)<20 or data[4:8]!=bytes.fromhex('2112a442') or data[8:20]!=test['token']:continue
                r['message_type']=hex(struct.unpack('!H',data[:2])[0])
            else:
                if len(data)<48 or data[24:32]!=test['token'] or data[0]&7!=4:continue
                r['stratum']=data[1]
            r.update(response=True,attempts=test['sent'],elapsed_ms=round(elapsed*1000),checksum_valid=ck==0 or checksum(b[26:34]+b'\0\x11'+struct.pack('!H',ulen)+b[off:off+ulen])==0)
            results.append(r);del pending[dport];print(json.dumps(r),flush=True)
    finally:lib.pcap_close(handle)
    for test in pending.values():
        r={k:test[k] for k in ['protocol','host','port']}
        if 'domain' in test:r['domain']=test['domain']
        r.update(response=False,attempts=test['sent'],error='timeout');results.append(r);print(json.dumps(r),flush=True)
    Path(args.output).write_text(json.dumps({'label':args.label,'results':results},indent=2)+'\n')

if __name__=='__main__':main()
