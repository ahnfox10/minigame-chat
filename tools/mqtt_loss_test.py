import socket, time, threading, sys, os
def s16(b): return bytes([len(b)>>8, len(b)&255])+b
def pkt(h, body):
    n=len(body); out=bytes([h])
    while True:
        b=n%128; n//=128
        if n: b|=0x80
        out+=bytes([b])
        if not n: break
    return out+body
def rd(s,n):
    d=b''
    while len(d)<n:
        c=s.recv(n-len(d))
        if not c: raise EOFError
        d+=c
    return d
def read(s):
    h=rd(s,1)[0]; l=0; m=1
    while True:
        c=rd(s,1)[0]; l+=(c&127)*m; m*=128
        if not c&128: break
    return h, rd(s,l) if l else b''
def conn(host, cid, sub=None):
    s=socket.create_connection((host,1883),timeout=8)
    s.sendall(pkt(0x10, s16(b'MQTT')+bytes([4,2,0,60])+s16(cid)))
    h,b=read(s); assert h>>4==2 and b[1]==0
    if sub:
        s.sendall(pkt(0x82, bytes([0,1])+s16(sub)+bytes([0]))); read(s)
    s.settimeout(None)
    return s
host=sys.argv[1]; rate=float(sys.argv[2]); n=int(sys.argv[3])
topic=('mgtest-'+os.urandom(4).hex()).encode()
rx=conn(host, b'rx'+os.urandom(4).hex().encode(), topic)
got=[]
def loop():
    try:
        while True:
            h,b=read(rx)
            if h>>4==3:
                tl=(b[0]<<8)|b[1]; got.append(int(b[2+tl:].split(b'|')[1]))
    except Exception: pass
threading.Thread(target=loop,daemon=True).start()
tx=conn(host, b'tx'+os.urandom(4).hex().encode())
t0=time.time()
for i in range(n):
    tx.sendall(pkt(0x30, s16(topic)+b'abcd1234|%d|100|200|314|55|1|0|some-padding-here'%i))
    time.sleep(1/rate)
time.sleep(2)
missing=[i for i in range(n) if i not in set(got)]
print(host, 'rate', rate, 'sent', n, 'got', len(got), 'missing', len(missing), missing[:20])
