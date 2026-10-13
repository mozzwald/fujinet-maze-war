#!/usr/bin/env python3
"""Read-only-to-production probe: isolated local actual server, auto-ACK peer.
No simulator/firmware/live server changes. Artificial peer is not a Lynx.
"""
import collections, importlib.util, json, pathlib, select, socket, subprocess, tempfile, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('wire',ROOT/'clients/lynx/bench/run.py');wire=importlib.util.module_from_spec(spec);spec.loader.exec_module(wire)
output=pathlib.Path(tempfile.mkdtemp(prefix='maze-traffic-review-'))
base=0
while not base:
    a=socket.socket();a.bind(('127.0.0.1',0));candidate=a.getsockname()[1]
    b=socket.socket()
    try:b.bind(('127.0.0.1',candidate+1));base=candidate
    except OSError:pass
    finally:a.close();b.close()
log=(output/'server.log').open('w')
server=subprocess.Popen([str(ROOT/'build/maze-war-server'),'--bind','127.0.0.1','--port-base',str(base),'--room-count','2','--room-zombies','0,2','--kill-limit','10','--debug'],cwd=ROOT,stdout=log,stderr=log)
peers=[]
try:
    time.sleep(.2)
    assert server.poll() is None
    for z in (0,2):
        s=socket.create_connection(('127.0.0.1',base+bool(z)));s.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);s.setblocking(False)
        peers.append(dict(socket=s,zombies=z,buffer=bytearray(),counts=collections.Counter(),bytes=0,bursts=[],round=1,pid=0,seq=0,lastack=0,applied=[]))
        s.sendall(wire.encode(bytes([0x46,1])))
    started=time.monotonic();end=started+24;next_input=started
    while time.monotonic()<end:
        now=time.monotonic()
        if now>=next_input:
            for p in peers:
                # 8s idle, 16s held fire in alternating cardinal directions.
                joy=15 if now-started<8 else (7,13,11,14)[int((now-started-8)/4)%4]|16
                p['socket'].sendall(wire.encode(bytes([0x41,p['seq'],p['pid'],joy,p['round']])));p['seq']=(p['seq']+1)&255
            next_input=now+.1
        ready,_,_=select.select([p['socket'] for p in peers],[],[],.01)
        for p in peers:
            if p['socket'] not in ready:continue
            data=p['socket'].recv(8192);assert data
            phase='idle' if now-started<8 else 'fire'
            p['counts'][(phase,'reads')]+=1;p['counts'][(phase,'wire_bytes')]+=len(data)
            p['bursts'].append((round(now-started,3),len(data)))
            p['buffer'].extend(data)
            while 0 in p['buffer']:
                at=p['buffer'].index(0);frame=bytes(p['buffer'][:at]);del p['buffer'][:at+1]
                if not frame:continue
                pkt=wire.decode(frame);kind=hex(pkt[0]);p['counts'][(phase,kind)]+=1
                if pkt[0]==0x47:p['round']=pkt[2]
                elif pkt[0]==0x40:p['pid']=(pkt[2]>>1)&3;p['round']=pkt[20]
                elif pkt[0]==0x53:
                    p['counts'][(phase,'reliable_'+hex(pkt[4]))]+=1
                    rev=int.from_bytes(pkt[2:4],'little')
                    if rev==p['lastack']+1:p['lastack']=rev
                    p['socket'].sendall(wire.encode(bytes([0x45,0,p['lastack']&255,p['lastack']>>8])))
    results=[]
    for p in peers:
        results.append(dict(zombies=p['zombies'],counts={f'{phase}:{kind}':v for (phase,kind),v in sorted(p['counts'].items())},largest_socket_read=max(v for _,v in p['bursts']),bursts=p['bursts']))
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps({'artifacts':str(output),'scenarios':[{k:v for k,v in r.items() if k!='bursts'} for r in results]},indent=2))
finally:
    for p in peers:p['socket'].close()
    server.terminate();server.wait(timeout=5);log.close()
