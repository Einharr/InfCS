# -*- coding: utf-8 -*-
"""Is the login server alive: we send an SOE SessionRequest over UDP and wait for a SessionReply.
No account and no client - the protocol handshake only.
    python loginprobe.py            # one request
    python loginprobe.py wait 600   # wait up to 600 s, polling every 20 s
"""
import socket, struct, sys, time

HOST, PORT = 'game.swginfinity.com', 24453


def probe(timeout=3.0):
    # SOE: opcode 0x0001, crcLength=2, connectionId, udpSize=496
    pkt = struct.pack('>HIII', 0x0001, 2, 0x1234ABCD, 496)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(timeout)
    try:
        s.sendto(pkt, (HOST, PORT))
        data, _ = s.recvfrom(1024)
        return len(data) >= 2 and struct.unpack('>H', data[:2])[0] == 0x0002, data
    except (socket.timeout, OSError) as e:
        return False, repr(e)
    finally:
        s.close()


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'wait':
        limit = float(sys.argv[2]) if len(sys.argv) > 2 else 600
        t0 = time.time()
        while time.time() - t0 < limit:
            ok, info = probe()
            print(time.strftime('%H:%M:%S'), 'UP' if ok else 'down', info if ok else '')
            if ok: sys.exit(0)
            time.sleep(20)
        sys.exit(1)
    ok, info = probe(); print('UP' if ok else 'down', info); sys.exit(0 if ok else 1)
