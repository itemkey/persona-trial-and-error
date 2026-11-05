"""Northern forest playtest through the opt-in Development driver.

Run against a fresh diagnostic sector. Use --checkpoint to stop during the first
ascent and --resume after loading that sector. Placement fixtures shorten the
walk between sites; item use, interaction, transport and saves use game code.
"""
import argparse
import math
import shutil
import time
from phase4_checks import ROOT, DIAG, state, me, vec, send, place, wait, report

parser=argparse.ArgumentParser()
parser.add_argument('--players',type=int,choices=[1,2,3,4],default=2)
parser.add_argument('--scenario',choices=['trip','resume','legacy','thirst','map-death','scenery'],default='trip')
parser.add_argument('--checkpoint',action='store_true')
parser.add_argument('--pictures',action='store_true')
args=parser.parse_args()
roles=range(args.players)
checks=[]
def check(name,fn,limit=15):
    try: wait(fn,limit)
    except AssertionError:
        checks.append(dict(check=name,passed=False));raise AssertionError(name) from None
    print('PASS',name,flush=True);checks.append(dict(check=name,passed=True))
def run(i=0):return state(i)['run']
def person(i):return next(p for p in run()['forestPlayers'] if p['id']==me(i)['persistentId'])
def aim(i,p):
    eye=vec(me(i)['camera']);d=[b-a for a,b in zip(eye,p)]
    send(i,op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2],math.hypot(*d[:2]))))
def interact(i,p,actor):
    aim(i,p)
    # Reacquired loose objects can lie beside the flag, another player or roots.
    if 'PersonaProp_' in actor or actor=='ForestFlag':
        time.sleep(.35)
        for dx,dy in [(-115,55),(100,-110),(0,125)]:
            if actor in me(i)['interactionActor']:break
            place(i,p[0]+dx,p[1]+dy,p[2]+(6 if actor=='ForestFlag' else 97));time.sleep(.5);aim(i,p);time.sleep(.35)
    check('Aim at '+actor,lambda:actor in me(i)['interactionActor'],5)
    send(i,op='interact')
def photo(name):
    if not args.pictures:return
    dest=ROOT/'Artifacts/Previews';dest.mkdir(parents=True,exist_ok=True)
    src=DIAG.parent.parent/'Artifacts/Previews/Player0_game.png'
    stamp=time.time();send(op='shot');wait(lambda:src.exists() and src.stat().st_mtime>=stamp-.35,8)
    shutil.copy2(src,dest/('NorthernForest-'+name+'.png'))
def choose(trial):
    place(0,-300,-393,-400,yaw=180,pitch=-15);time.sleep(.8)
    send(op='decision',accept=True,trialId=trial)
def start():
    choose('SurvivalAgility')
    check('Every peer streams the northern level',lambda:all(run(i)['loaded'] and run(i)['generation']==1 and run(i)['phase']==1 for i in roles),60)
    send(op='board');time.sleep(.8);send(op='lift')
    check('Descent manifest',lambda:run()['lift']==1 and run()['passengers']==args.players)
    if args.pictures:
        for direction,pitch in [(0,-80),(95,0),(180,55),(270,0)]:
            send(op='look',yaw=direction,pitch=pitch);time.sleep(.25);photo('shaft-'+str(direction))
    check('Arrive at clearing',lambda:all(run(i)['phase']==7 and not me(i)['transit'] for i in roles),40)
    send(op='forest_fixture',quiet=True)
    check('No finds in starting cube',lambda:all(max(abs(vec(n['position'])[0]),abs(vec(n['position'])[1]-490))>2100 for n in run()['sites']))
def take_maps():
    case=vec(run()['case'])
    for i in roles:
        place(i,case[0]+130,case[1]+65,case[2]+98);time.sleep(.65);interact(i,[case[0],case[1],case[2]+16],'MedicalCase')
        check('Take personal chart',lambda:person(i)['hasMap'])
        place(i,case[0]+300+i*110,case[1]+80,case[2]+98)
    send(op='key',key='M');check('Paper chart opens',lambda:me()['mapOpen'])
    photo('paper');send(op='key',key='M')
def flag():
    p=vec(run()['center']);place(0,p[0]-150,p[1],p[2]+6);time.sleep(.65)
    interact(0,p,'ForestFlag');check('Flag follows its carrier',lambda:run()['carrier']==me()['persistentId'])
def dropflag():
    send(op='charge',held=True);check('Set flag down',lambda:not run()['carrier'])
def height(seed,x,y):
    phase=(seed%10007)*.037
    ch=9800+1350*math.sin(y*.00024+phase)+580*math.sin(y*.00057-phase)
    d=x-ch;d=abs(d-21000*round(d/21000))
    def smooth(a,b,v):
        t=max(0,min(1,(v-a)/(b-a)));return t*t*(3-2*t)
    h=118*math.sin(.00064*x+phase)*math.cos(.00048*y-phase)+205*math.sin(.00027*y+.00015*x+phase)+35*math.sin(.0022*x+.0013*y)
    return -12608+(h*smooth(600,1500,d)-(1-smooth(160,535,d))*(680+1500*math.sin(y*.00012+phase)**2))*smooth(640,2200,max(abs(x),abs(y)))
def travel(x,y):
    r=run();seed=r['seed'];p=vec(r['center'])
    steps=max(1,math.ceil(math.dist(p[:2],(x,y))/1000))
    for n in range(1,steps+1):
        px=p[0]+(x-p[0])*n/steps;py=p[1]+(y-p[1])*n/steps
        for i in roles:
            if person(i)['outcome']!=0:continue
            place(i,px+90*i,py+102*i,height(seed,px+90*i,py+102*i)+115)
        time.sleep(.15)
    time.sleep(.6)
def use_lift(i):
    place(i,0,-80,-12507,pitch=-84);time.sleep(.8)
    aim(i,[0,-65,-12600]);check('Target lift',lambda:'HubObject' in me(i)['interactionActor'],5);send(i,op='interact')
def finish():
    check('First passenger unloads on base',lambda:vec(me(0)['position'])[2]>-1000 and not me(0)['transit'],45)
    check('Loose cargo unloaded in hub',lambda:any(p['definition'] in ('DA_Scrap','DA_Cloth','DA_Ammo12GA') and vec(p['position'])[2]>-1000 and vec(p['position'])[1]>700 for p in state()['props']),12)
    if args.players>1:
        check('Empty lift returns to survivors',lambda:run()['phase']==7 and run()['lift']==2 and person(0)['outcome']==1 and person(1)['outcome']==0,30)
        for i in list(roles)[2:]:place(i,80*i,-70,-12507)
        use_lift(1)
    check('Last arrival closes expedition',lambda:run()['phase']==0 and all(vec(me(i)['position'])[2]>-1000 for i in roles),35)
    choose('SurvivalAgility');time.sleep(.7);check('Cooldown enforced for sector',lambda:run()['phase']==0 and run()['cooldown']>time.time()+500)
    choose('Shooting');check('Shooting available during cooldown',lambda:run()['trialId']=='Shooting' and run()['phase']==1,20)
def trip():
    start();take_maps();flag();travel(0,24100)
    check('Unbounded terrain beyond old perimeter',lambda:vec(me()['position'])[1]>23000 and not me()['dead'])
    send(op='look',yaw=30,pitch=0);time.sleep(.4);photo('birches')
    sites=[n for n in run()['sites'] if n['kind']>=3 and abs(vec(n['position'])[0])<5400]
    node=min(sites,key=lambda n:math.dist(vec(n['position'])[:2],vec(run()['center'])[:2]))
    p=vec(node['position']);travel(p[0]+145,p[1]+55);dropflag()
    place(0,p[0]+140,p[1]+50,p[2]+97);time.sleep(.55);interact(0,[p[0],p[1],p[2]+15],node['actor'])
    check('Harvested material reaches inventory',lambda:any(s['definition'] in ('DA_Scrap','DA_Cloth','DA_Ammo12GA') for s in me()['slots']))
    # Leave the physical find here, evict its tile and come back for it.
    send(op='drop');check('Loose material placed',lambda:any(v['definition'] in ('DA_Scrap','DA_Cloth','DA_Ammo12GA') and math.dist(vec(v['position'])[:2],p[:2])<420 for v in state()['props']))
    dropped_id=next(v['itemId'] for v in state()['props'] if v['definition'] in ('DA_Scrap','DA_Cloth','DA_Ammo12GA') and math.dist(vec(v['position'])[:2],p[:2])<420)
    flag();travel(p[0],p[1]+15300);check('Distant dropped item archived',lambda:run()['archivedDrops']>0)
    travel(p[0]+210,p[1]+90);dropflag()
    check('Cache remains empty on revisit',lambda:all(math.dist(vec(n['position']),p)>30 for n in run()['sites']))
    check('Same dropped item finishes restoring with the chunk',lambda:any(v['itemId']==dropped_id for v in state()['props']))
    obj=next(v for v in state()['props'] if v['itemId']==dropped_id)
    pos=vec(obj['position']);place(0,pos[0]-112,pos[1]-50,pos[2]+95);time.sleep(.6)
    interact(0,pos,obj['actor']);check('Recover the same loose find',lambda:any(s['definition']==obj['definition'] for s in me()['slots']))
    check('Survey visible on every peer',lambda:all(run(i)['surveyCells']>3 for i in roles))
    flag();travel(0,435);dropflag()
    for i in list(roles)[1:]:place(i,560+i*95,430,-12502)
    place(0,-80,-70,-12503,yaw=0,pitch=-15);time.sleep(.55);send(op='drop');time.sleep(.75)
    use_lift(0);check('First ascent carries cargo',lambda:run()['lift']==3 and run()['cargo']>=1 and run()['passengers']==1)
    if args.checkpoint:send(op='save')
    else:finish()
try:
    check('Connected sector',lambda:all(state(i)['map']=='L_Hub' and me(i).get('persistentId') for i in roles),160)
    if args.scenario=='trip':trip()
    elif args.scenario=='resume':finish()
    elif args.scenario=='scenery':
        start();take_maps();flag()
        seed=run()['seed'];ph=seed%10007*.037
        river=9800+1350*math.sin(ph)+580*math.sin(-ph)
        travel(river-1100,0);dropflag()
        place(0,river-870,90,height(seed,river-870,90)+98,yaw=-4,pitch=-6);time.sleep(1);photo('crossing')
        check('Footbridge connects riverbanks',lambda:not me()['dead'])
        flag()
        node=min((n for n in run()['sites'] if n['kind']==1),key=lambda n:math.dist(vec(n['position'])[:2],vec(run()['center'])[:2]))
        p=vec(node['position']);travel(p[0]+140,p[1]+60);dropflag()
        place(0,p[0]-135,p[1]+70,p[2]-7);time.sleep(.7);interact(0,p,node['actor'])
        check('Pick an apple from a wild tree',lambda:any(v['definition']=='DA_Ration' for v in me()['slots']))
        send(op='look',yaw=90,pitch=-5);time.sleep(.6);photo('apple')
        send(op='forest_fixture',id=me()['persistentId'],food=10);send(op='key',key='LeftMouseButton')
        check('Apple restores satiety',lambda:me()['hunger']>35)
        check('Picked fruit is depleted',lambda:all(math.dist(vec(n['position']),p)>30 for n in run()['sites']))
        send(op='forest_fixture',restock=True)
        check('Renewable source returns',lambda:any(math.dist(vec(n['position']),p)<30 for n in run()['sites']))
    elif args.scenario=='legacy':
        check('0.9 forest restored',lambda:run()['generation']==0 and run()['phase']==7)
        for i in roles:place(i,80*i,-70,-12507)
        use_lift(0);check('Legacy survivors extract on lift',lambda:run()['phase']==0,35)
    elif args.scenario=='map-death':
        start();take_maps()
        send(op='kill_player',player=state()['localPlayer']);check('Chart lost on death',lambda:person(0)['outcome']==2 and not person(0)['hasMap'])
        check('Remaining partner keeps shared chart',lambda:person(1)['hasMap'])
    else:
        if args.players>1:
            place(0,-600,900,110);place(1,-425,900,110,yaw=180);time.sleep(.8)
            send(op='water_fixture',id=me()['persistentId'],water=0);check('Fallen teammate needs help',lambda:me()['dry'] and me()['dead'])
            time.sleep(2);pelvis=vec(me()['pelvis']);place(1,pelvis[0]+125,pelvis[1]+70,pelvis[2]+75);time.sleep(.6)
            target=next(c['actor'] for c in state(1)['characters'] if c['persistentId']==me()['persistentId'])
            interact(1,pelvis,target);check('Partner revives at base',lambda:not me()['dead'] and me()['water']>95)
        for i in roles:
            send(op='water_fixture',id=me(i)['persistentId'],water=0);check('Hub dehydration',lambda:me(i)['dry'] and me(i)['dead'])
            if i==0 and args.players>1:
                send(i,op='interact');time.sleep(.6);check('Living teammate required',lambda:me(i)['dead'])
        send(0,op='key',key='E');check('Empty group can wake',lambda:not me()['dead'] and me()['water']>95)
finally:
    report('north-'+args.scenario+'-'+str(args.players),checks)

