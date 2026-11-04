"""Thirty live AI with a rendered host/client, 100 ms lag and 2% loss."""
import math,statistics
from phase4_checks import *
checks=[];samples=[]
def check(name,ok):
    checks.append(dict(check=name,passed=bool(ok)));print('PASS' if ok else 'FAIL',name,flush=True)
    assert ok,name
try:
    wait(lambda:me(1).get('id',-1)>=0,180)
    check('Both peers use 100 ms lag and 2% loss',all(state(i).get('packetLagMs')==100 and state(i).get('packetLossPercent')==2 for i in (0,1)))
    send(op='clear_obstacles');send(op='npcs',value=0);send(op='npcs',value=30)
    wait(lambda:sum(c['npc'] and not c['dead'] for c in state()['characters'])==30)
    # The generic hub spawn can project onto its isolated 400 cm roof. Place
    # all subjects on the verified open floor before measuring active AI.
    enemies=[c['actor'] for c in state()['characters'] if c['npc'] and not c['dead']]
    send(op='vitals',health=100000);place(0,-1700,-1150,100,yaw=-90);place(1,-1100,-1300,100,yaw=-145)
    for i,name in enumerate(enemies):
        send(op='ai_pause',actor=name,paused=True)
        send(op='place_npc',actor=name,x=-2100+130*(i%6),y=-2200+130*(i//6),z=100,yaw=90)
    for i,name in enumerate(enemies):send(op='ai_pause',actor=name,paused=False,seed=i)
    send(op='noise');time.sleep(4)
    end=time.monotonic()+30
    while time.monotonic()<end:
        peers=[state(i) for i in (0,1)];row=[]
        for s in peers:
            enemies=[c for c in s['characters'] if c['npc'] and not c['dead']]
            row.append(dict(utc=s['utc'],count=len(enemies),frameMs=s['frameMs'],gameThreadMs=s['gameThreadMs'],gpuMs=s['gpuMs'],
                bytesOut=s.get('outBytesSec'),moving=sum(c['speed']>10 for c in enemies),attacking=sum(c['attacking'] for c in enemies),
                maxPelvisRadius=max((math.dist(vec(c['pelvis'])[:2],vec(c['rootPosition'])[:2]) for c in enemies),default=0)))
        samples.append(row);time.sleep(.1)
    check('Thirty NPCs remain replicated throughout the run',all(r['count']==30 for row in samples for r in row))
    check('Host and client both render during the run',all(max(row[i]['gpuMs'] for row in samples)>0 for i in (0,1)))
    check('AI continues moving under load',statistics.mean(row[0]['moving'] for row in samples)>3)
    check('Neither peer displays a detached live pelvis',max(r['maxPelvisRadius'] for row in samples for r in row)<70)
    metrics=[]
    for i in (0,1):
        values=sorted(row[i]['frameMs'] for row in samples)
        metrics.append(dict(role=i,frameMedianMs=statistics.median(values),frameP95Ms=values[int(len(values)*.95)],
            frameMaxMs=max(values),gameThreadMedianMs=statistics.median(row[i]['gameThreadMs'] for row in samples)))
    report('phase7-network-load',checks,metrics=metrics,samples=samples)
finally:
    send(op='npcs',value=0)
    if not checks or not all(c['passed'] for c in checks):report('phase7-network-load',checks,samples=samples)
