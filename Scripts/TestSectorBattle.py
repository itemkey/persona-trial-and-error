"""Two real listen-server sectors: challenge, roster transfer, combat and receipts."""
import argparse,time,json,math,uuid
from phase4_checks import ROOT,DIAG,state,send,wait,me,vec,report
p=argparse.ArgumentParser();p.add_argument('--team-size',type=int,default=1);p.add_argument('--stop-at',choices=['gathering','fight','return'],default='return');p.add_argument('--loot',action='store_true');p.add_argument('--cargo',action='store_true');p.add_argument('--weapon',action='store_true');p.add_argument('--legacy-tools',action='store_true');args=p.parse_args()
n=args.team_size;roles=list(range(2*n));hosts=(0,n);checks=[];saved={};ids={};slots={}
def check(label,fn,seconds=40):
    try:wait(fn,seconds)
    except Exception:
        checks.append(dict(check=label,passed=False))
        (ROOT/'.work/battle-failure.json').write_text(json.dumps({i:state(i) for i in roles},ensure_ascii=False,indent=2),encoding='utf-8')
        raise AssertionError(label) from None
    checks.append(dict(check=label,passed=True));print('PASS',label,flush=True)
def owned(role):return [c for c in state(role)['characters'] if not c['npc'] and c['persistentId']==ids[role]]
def on_server(role):return next(c for c in state(0)['characters'] if c['persistentId']==ids[role])
def place_on(role,server,x,y,z):
    c=next(c for c in state(server)['characters'] if c['persistentId']==ids[role])
    send(server,op='place',player=c['id'],x=x,y=y,z=z,yaw=0,pitch=0)
def qty(role,item):return sum(x['quantity'] for x in me(role)['slots'] if x['definition']==item)
def gun():return next(p for p in state()['props'] if p['actor']==me()['held'] and p['definition']=='DA_Shotgun')
cargo={};gun_before=None
def content(value,sector=''):
    if not value:return []
    if isinstance(value,str):value=json.loads(value)
    result=[]
    for r in value.get('items',[])+[r for p in value.get('compartments',[]) for r in p['items']]:
        identity=r.get('id',r.get('iD'))
        if sector and not identity.startswith('sector:'):
            try:uuid.UUID(identity)
            except ValueError:identity='sector:'+sector+':'+identity
        result.append((identity,r['definition'],r['quantity'],content(r.get('containerState',''),sector)))
    return result
try:
    check('All clients start in menu',lambda:all(state(i)['map']=='L_MainMenu' for i in roles),300)
    for h in hosts:send(h,op='menu',action='new')
    check('Two independent sectors hosted',lambda:all(state(i)['map']=='L_Hub' for i in hosts),150)
    for i in roles:
        if i not in hosts:send(i,op='connect',address='127.0.0.1:'+str(7777 if i<n else 7787))
    check('Both groups joined their own hubs',lambda:all(state(i)['map']=='L_Hub' and me(i).get('persistentId') for i in roles),90)
    ids={i:me(i)['persistentId'] for i in roles};saved={h:state(h)['sector'] for h in hosts}
    for h in hosts:
        send(h,op='fixture',definition='DA_Scrap.DA_Scrap',quantity=2,store=False)
        time.sleep(.3)
        send(h,op='admin_give',item='/Game/Persona/Items/DA_Scrap.DA_Scrap',quantity=3+h)
        check(f'Host {h} brings tracked resources',lambda h=h:qty(h,'DA_Scrap')==3+h)
        slots[h]=[(s['actor'],s['quantity']) for s in me(h)['slots'] if s['definition']=='DA_Scrap']
        if args.cargo:
            def record(name,q,container=''):return dict(actorClass='/Script/PersonaTrial.PersonaProp',definition='/Game/Persona/Items/'+name+'.'+name,id=name+'Legacy' if args.legacy_tools else str(uuid.uuid4()),quantity=q,containerState=container)
            inner=json.dumps(dict(items=[record('DA_Flask',2)],compartments=[],capacity=12))
            contents=json.dumps(dict(items=[record('DA_Cloth',5),record('DA_HUB_Toolbox',1,inner)],compartments=[],capacity=12))
            x,y,z=vec(next(x for x in state(h)['hub'] if x['kind']==7)['position'])
            send(h,op='fixture',definition='DA_HUB_Toolbox.DA_HUB_Toolbox',quantity=1,store=False,x=x+170,y=y+140,z=z+70,containerState=contents,id='LegacyPlacedToolbox' if args.legacy_tools else '')
            check('Nested cargo created on departure lift',lambda h=h:any(p.get('containerState') for p in state(h)['props']))
            box=next(p for p in state(h)['props'] if p.get('containerState'));cargo[h]=('sector:'+saved[h]+':'+box['itemId'] if args.legacy_tools else box['itemId'],content(box['containerState'],saved[h] if args.legacy_tools else ''))
    if args.weapon:
        send(op='admin_give',item='/Game/Persona/Items/DA_Shotgun.DA_Shotgun',quantity=1);send(op='admin_give',item='/Game/Persona/Items/DA_Ammo12GA.DA_Ammo12GA',quantity=10)
        check('Bring weapon and ammunition from hub',lambda:qty(0,'DA_Shotgun')==1 and qty(0,'DA_Ammo12GA')==10)
        send(op='slot',value=next(i for i,s in enumerate(me()['slots']) if s['definition']=='DA_Shotgun'));time.sleep(.6);send(op='reload',held=True)
        check('Load shotgun through ordinary weapon actions',lambda:gun()['tube']>=3);send(op='reload',held=False);time.sleep(1);send(op='pump')
        check('Chamber live round before departure',lambda:gun()['chamber']==1)
        gun_before={k:gun()[k] for k in ('itemId','tube','chamber','safety')}
    send(n,op='battle',action='open');check('Second sector advertises challenge readiness',lambda:state(n)['battle']['open'])
    time.sleep(3);send(0,op='battle',action='find')
    check('First host finds second ready sector',lambda:len(state(0)['battle']['listings'])>0,40)
    index=next(i for i,x in enumerate(state(0)['battle']['listings']) if x['owner']==ids[n])
    send(0,op='battle',action='challenge',index=index)
    check('Incoming challenge reaches other host',lambda:state(n)['battle']['phase']==1)
    send(n,op='battle',action='accept')
    check('Both hosts enter gathering phase',lambda:all(state(i)['battle']['phase']==2 for i in hosts))
    if args.stop_at=='gathering':raise SystemExit(0)
    for i in roles:send(i,op='battle',action='ready')
    check('Both groups individually ready',lambda:all(state(h)['battle']['ready']==n for h in hosts))
    for h in hosts:send(h,op='board')
    time.sleep(.7)
    for h in hosts:send(h,op='battle',action='lift')
    check('All participants move into one arena',lambda:all(state(i)['map']=='L_SectorBattle' for i in roles),160)
    check('Battle starts after both lifts arrive',lambda:state(0)['battle']['phase']==7,150)
    if args.weapon:send(op='shot');send(op='record',seconds=12,fps=60)
    check('Arena has exactly registered roster',lambda:len([c for c in state(0)['characters'] if not c['npc']])==2*n)
    check('Original personal cargo survives transfer',lambda:all(qty(h,'DA_Scrap')==3+h for h in hosts))
    check('Each participant retains its assigned team',lambda:all(owned(i)[0]['team']==(0 if i<n else 1) for i in roles))
    if args.cargo:check('Both nested cargo manifests survive arena transfer',lambda:all(any(p['itemId']==v[0] and content(p['containerState'])==v[1] for p in state()['props']) for v in cargo.values()))
    if args.weapon:check('Loaded weapon identity and state survive transfer',lambda:all(gun()[k]==v for k,v in gun_before.items()))
    if args.stop_at=='fight':raise SystemExit(0)
    if args.weapon and args.loot:
        # Free a real inventory slot before taking more than one material stack.
        # Keep the ammunition as physical export cargo on our own platform.
        place_on(0,0,-3100,-100,95);time.sleep(.4)
        send(op='slot',value=next(i for i,s in enumerate(me()['slots']) if s['definition']=='DA_Ammo12GA'));time.sleep(.4);send(op='drop')
        check('Ammunition staged as lift cargo to free a loot slot',lambda:qty(0,'DA_Ammo12GA')==0)
        send(op='slot',value=next(i for i,s in enumerate(me()['slots']) if s['definition']=='DA_Shotgun'));time.sleep(.4)
    # Eliminate team B through the production damage/death path.
    if args.weapon:
        place_on(0,0,-1550,-2600,95);place_on(n,0,-1050,-2600,95);time.sleep(.8)
        for _ in range(5):
            send(op='place',player=on_server(0)['id'],x=-1550,y=-2600,z=95,yaw=0,pitch=-4);time.sleep(.3);send(op='fire');time.sleep(.8)
            if on_server(n)['dead']:break
            send(op='pump');time.sleep(1)
        check('Production shotgun fire eliminates opposing player',lambda:on_server(n)['dead'])
    for i in range(n+(1 if args.weapon else 0),2*n):send(0,op='hit',actor=on_server(i)['actor'],bone='spine_02',damage=10000)
    check('Winner determined after all deaths',lambda:state(0)['battle']['phase']==8 and state(0)['battle']['winner']==0)
    if args.loot:
        corpse=on_server(n);loot=next(x for x in corpse['lootItems'] if 'DA_Scrap' in x['definition'])
        target=vec(corpse['meshPosition']);opened=False
        for angle in (0,90,180,270):
            x=target[0]-140*math.cos(math.radians(angle));y=target[1]-140*math.sin(math.radians(angle))
            for pitch in (-35,-50,-65,-20):
                c=on_server(0);send(0,op='place',player=c['id'],x=x,y=y,z=95,yaw=angle,pitch=pitch);time.sleep(.2)
                send(0,op='interact');time.sleep(.25)
                if me()['lootTarget']==corpse['actor']:opened=True;break
                send(0,op='close_loot')
            if opened:break
        check('Winner can inspect actual player corpse',lambda:opened)
        send(0,op='loot',actor=corpse['actor'],index=corpse['lootItems'].index(loot),deposit=False,expectedId=loot['id'])
        check('Loser resources are taken through ordinary loot RPC',lambda:qty(0,'DA_Scrap')==6+n)
        send(0,op='close_loot');time.sleep(.3)
    # Own resources plus winner inventory are exported; uncollected loser gear is lost.
    for i in range(n):place_on(i,0,-3100+(i%2)*100-50,(i//2)*110-50,95)
    time.sleep(.7);send(0,op='battle',action='lift')
    check('Both groups return to separate hubs',lambda:all(state(i)['map']=='L_Hub' for i in roles),160)
    check('Each host restores its original sector',lambda:all(state(h)['sector']==saved[h] for h in hosts))
    check('Winner keeps carried resources once',lambda:qty(0,'DA_Scrap')==(6+n if args.loot else 3))
    check('Loser inventory is empty after defeat',lambda:qty(n,'DA_Scrap')==0)
    check('Both hubs retain resources left outside departure platform',lambda:all(sum(p['quantity'] for p in state(h)['props'] if p['definition']=='DA_Scrap' and p['holder']<0)==2 for h in hosts))
    check('All players recover in their own sector',lambda:all(not me(i)['dead'] and me(i)['health']>0 for i in roles))
    if args.cargo:
        check('Winner unloads original nested cargo exactly once',lambda:sum(p['itemId']==cargo[0][0] and content(p['containerState'])==cargo[0][1] for p in state()['props'])==1)
        check('Uncollected opposing cargo is lost without touching hub stocks',lambda:not any(p['itemId']==cargo[n][0] for h in hosts for p in state(h)['props']))
finally:
    report('v0110-battle-'+str(n)+'v'+str(n)+('-cargo' if args.cargo else '')+('-weapon' if args.weapon else '')+('-setup-'+args.stop_at if args.stop_at!='return' else ''),checks,ids=ids,sectors=saved)
