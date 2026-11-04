"""Sequential mantles, concurrent reservations and losing support mid-mantle."""
from phase4_checks import *
checks=[]; captures=[]

def check(name,predicate,seconds=25):
    try: wait(predicate,seconds)
    except Exception:
        checks.append(dict(check=name,passed=False,state=state())); raise
    checks.append(dict(check=name,passed=True)); print('PASS',name,flush=True)

def npc(identity,role=0): return next(c for c in state(role)['characters'] if c['persistentId']==identity)

def position(identity,x,y,z):
    name=npc(identity)['actor']; send(op='ai_pause',actor=name,paused=True)
    send(op='place_npc',actor=name,x=x,y=y,z=z,yaw=90)

def release(identity): send(op='ai_pause',actor=npc(identity)['actor'],paused=False)

try:
    send(op='npcs',value=0); send(op='vitals',health=10000); send(op='clear_inventory')
    if state()['run']['phase']==0:
        accept_trial(); wait(lambda:state()['run']['loaded'],60)
        send(op='board'); time.sleep(.7); send(op='lift'); wait(lambda:state()['run']['phase']==2,40)
    enemies=[c for c in state()['characters'] if c['npc'] and not c['dead']]
    for enemy in enemies: send(op='ai_pause',actor=enemy['actor'],paused=True)
    a,b=[c['persistentId'] for c in enemies[:2]]
    # The second peer observes replication; keep it outside perception range
    # so the pursuit subject cannot legitimately select that player instead.
    place(1,0,1100,100)
    links=state()['climbLinks']
    send(op='obstacle',x=-2500,y=-3500,z=-12530,sx=400,sy=400,sz=140)
    send(op='obstacle',x=-2500,y=-3120,z=-12420,sx=400,sy=400,sz=360)
    check('Geometry generates sequential navigation transitions',lambda:state()['climbLinks']>links,40)
    position(a,-2500,-3900,-12508); place(0,-2500,-3120,-12148,yaw=-90)
    time.sleep(.7); release(a); send(op='noise')
    ends=set(); deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        c=npc(a)
        if c['climbing']: ends.add(round(vec(c['climbEnd'])[2]/10)*10)
        if vec(c['position'])[2]>-12170: break
        time.sleep(.04)
    check('NPC reaches a 360 cm platform using at least two valid mantles',lambda:len(ends)>=2 and vec(npc(a)['position'])[2]>-12170)
    position(a,-2670,-3900,-12508); position(b,-2330,-3900,-12508)
    release(a); release(b); send(op='noise')
    overlaps=[]; climbs=0; deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        ca,cb=npc(a),npc(b)
        climbs+=int(ca['climbing'])+int(cb['climbing'])
        if ca['climbing'] and cb['climbing']:
            pa,pb=vec(ca['climbEnd']),vec(cb['climbEnd'])
            overlaps.append(sum((pa[i]-pb[i])**2 for i in range(3))<90**2)
        time.sleep(.06)
    check('Concurrent NPCs reserve distinct landing spaces',lambda:climbs>0 and not any(overlaps))
    position(a,-2500,-3900,-12508); send(op='ai_pause',actor=npc(b)['actor'],paused=True)
    release(a); send(op='noise'); wait(lambda:npc(a)['climbing'],20)
    send(op='hit',actor=npc(a)['actor'],bone='spine_02',damage=1000)
    check('Death cancels mantle and hands the body to physics',lambda:npc(a)['dead'] and not npc(a)['climbing'] and npc(a)['ragdoll'])
    check('Peer receives death during mantle',lambda:npc(a,1)['dead'] and npc(a,1)['corpseFrameBones']>0)
    check('Interrupted corpse reaches supported rest',lambda:npc(a)['poseBones']>0,35)
    position(b,-2500,-3900,-12508); release(b); send(op='noise')
    wait(lambda:npc(b)['climbing'],25)
    before=vec(npc(b)['position'])[2]; send(op='clear_obstacles')
    check('Removing support interrupts traversal without hovering',lambda:not npc(b)['climbing'] and vec(npc(b)['position'])[2]<=-12500,10)
finally:
    send(op='clear_obstacles')
    for role in (0,1): send(op='kill_player',player=state(role)['localPlayer'])
    wait(lambda:state()['run']['phase']==0,30)
    report('phase6-traversal-edges',checks,captures=captures)
