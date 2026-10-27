"""Moving lift cargo, paired arena doors and the sliding spawn gate."""
import argparse,math,shutil
from phase4_checks import *
parser=argparse.ArgumentParser();parser.add_argument('--players',type=int,default=4);args=parser.parse_args()
checks=[]

def check(name,test,seconds=8):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False,state=state()));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)

def hub(name,role=0):return next(h for h in state(role)['hub'] if h['id']==name)

def angles(name,opened,role=0):
    yaw=[float(p['rotation'].split('Y=')[1].split()[0]) for p in hub(name,role)['parts']]
    return len(yaw)==2 and (all(abs(abs(y)-105)<.3 for y in yaw) and yaw[0]*yaw[1]<0 if opened else all(abs(y)<.3 for y in yaw))

try:
    check('All geometry-test participants have joined',lambda:sum(not c['npc'] and not c['offline'] for c in state()['characters'])==args.players,90)
    send(op='npcs',value=0);send(op='clear_inventory');send(op='vitals',health=100);send(op='close_loot')
    check('Geometry fixture starts with an offered run',lambda:state()['run']['phase']==0)
    send(op='decision',accept=True);wait(lambda:state()['run']['loaded'],60);send(op='board');time.sleep(.7)
    before={p['itemId'] for p in state()['props']}
    send(op='fixture',definition='DA_Ammo12GA',quantity=7,store=False);time.sleep(1)
    identity=next(p['itemId'] for p in state()['props'] if p['itemId'] not in before)
    send(op='lift')
    check('Moving lift captures loose ammunition',lambda:state()['run']['lift']==1 and state()['run']['cargo']>=1)
    wait(lambda:state()['run']['phase']==2,35)
    check('Loose cargo arrives above the arena floor without tunnelling',lambda:any(p['itemId']==identity and -12605<vec(p['position'])[2]<-12450 for p in state()['props']))
    for c in state()['characters']:
        if c['npc']:send(op='ai_pause',actor=c['actor'],paused=True)
    gate='TRIAL01_NPC_Gate_ROOT'
    check('Spawn gate leaves slide in opposite directions',lambda:hub(gate)['open'] and abs(vec(hub(gate)['parts'][0]['location'])[0])>1300 and abs(vec(hub(gate)['parts'][1]['location'])[0])>1300)
    manifest={a['id']:a for a in json.loads((ROOT/'SourceArt/Phase4/manifest.json').read_text())['actors']}
    peer=1 if sum(not c['npc'] for c in state()['characters'])>1 else 0
    for name in ('TRIAL01_Container_A_ROOT','TRIAL01_Container_B_ROOT'):
        data=manifest[name];base=vec(hub(name)['position']);left,right=[p['offset'] for p in data['parts']]
        target=[base[i]+left[i]+(right[i]-left[i])*.25 for i in range(3)];target[2]+=120
        outward=[(left[i]+right[i])*.5 for i in range(2)];length=math.hypot(*outward)
        place(0,target[0]+outward[0]/length*145,target[1]+outward[1]/length*145,-12505);time.sleep(.4)
        eye=vec(me()['camera']);delta=[target[i]-eye[i] for i in range(3)]
        send(op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))));time.sleep(.2)
        check(name+': both doors start closed',lambda name=name:angles(name,False))
        send(op='interact')
        check(name+': doors open to opposing 105-degree hinges',lambda name=name:hub(name)['open'] and angles(name,True))
        check(name+': peer agrees on paired door poses',lambda name=name:angles(name,True,peer))
        send(op='shot');time.sleep(.3)
        root=ROOT/'Artifacts/Windows/PersonaTrial' if os.environ.get('PERSONA_PACKAGED')=='1' else ROOT
        shutil.copy2(root/'Artifacts/Previews/Player0_game.png',ROOT/f'Artifacts/Previews/PERSONA_05_{name}.png')
        send(op='key',key='Tab')
        check(name+': Tab closes both doors',lambda name=name:not hub(name)['open'] and angles(name,False))
    for c in state()['characters']:
        if not c['npc']:send(op='kill_player',player=c['id'])
    check('Geometry fixture cleans up through the normal wipe',lambda:state()['run']['phase']==0,25)
finally:report('phase5-trial-geometry',checks)
