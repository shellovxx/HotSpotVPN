"""Send DHCP probes without applying the reply to the client's network settings."""
import argparse, ipaddress, json, secrets, socket, struct, time
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-ip', required=True)
    parser.add_argument('--server-ip', required=True)
    parser.add_argument('--client-mac', required=True)
    parser.add_argument('--kind', choices=('discover', 'request', 'inform'), default='inform')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--expect-dns')
    args = parser.parse_args()
    mac = bytes.fromhex(args.client_mac.replace(':', '').replace('-', ''))
    if len(mac) != 6: parser.error('client MAC must contain six octets')
    client = ipaddress.IPv4Address(args.client_ip).packed
    server = ipaddress.IPv4Address(args.server_ip).packed
    xid = secrets.randbits(32)
    packet = bytearray(240)
    packet[:4] = bytes((1, 1, 6, 0))
    packet[4:8] = struct.pack('!I', xid)
    if args.kind == 'inform': packet[12:16] = client
    else: packet[10:12] = b'\x80\x00'
    packet[28:34] = mac
    packet[236:240] = bytes((99, 130, 83, 99))
    kind = {'discover': 1, 'request': 3, 'inform': 8}[args.kind]
    packet += bytes((53, 1, kind, 55, 3, 6, 3, 1, 61, 7, 1)) + mac
    if args.kind == 'request': packet += bytes((50, 4)) + client + bytes((54, 4)) + server
    packet += b'\xff'
    packet += bytes(max(0, 300 - len(packet)))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind((args.client_ip, 68))
        for attempt in range(3):
            sock.sendto(packet, (args.server_ip, 67))
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                sock.settimeout(max(.01, deadline - time.monotonic()))
                try: data, peer = sock.recvfrom(4096)
                except socket.timeout: break
                if len(data) < 240 or struct.unpack('!I', data[4:8])[0] != xid: continue
                options = {}; offset = 240
                while offset < len(data):
                    tag = data[offset]; offset += 1
                    if tag == 0: continue
                    if tag == 255: break
                    if offset == len(data): raise ValueError('truncated DHCP option length')
                    length = data[offset]; offset += 1
                    if length > len(data) - offset: raise ValueError('truncated DHCP option')
                    options.setdefault(tag, b'')
                    options[tag] += data[offset:offset+length]; offset += length
                raw = options.get(6, b'')
                if len(raw) % 4: raise ValueError('invalid DNS option length')
                result = {'xid': xid, 'kind': args.kind, 'peer': peer, 'message_type': int.from_bytes(options.get(53, b'\0'), 'big'),
                          'dns': [str(ipaddress.IPv4Address(raw[i:i+4])) for i in range(0, len(raw), 4)],
                          'options': {str(k): v.hex() for k, v in options.items()}}
                expected_type = 2 if args.kind == 'discover' else 5
                if result['message_type'] != expected_type: raise RuntimeError(result)
                print(json.dumps(result))
                if args.output: args.output.write_text(json.dumps(result, indent=2)+'\n')
                if args.expect_dns and result['dns'] != args.expect_dns.split(','): raise AssertionError(result)
                return
    raise TimeoutError('No matching DHCP reply after three probes')

if __name__ == '__main__': main()
