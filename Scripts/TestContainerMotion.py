"""Real interaction input, quaternion endpoints, reversal and peer agreement."""
import math,shutil
from phase4_checks import *
checks=[];samples={}
manifest={a['id']:a for a in json.loads((ROOT/'SourceArt/Phase4/manifest.json').read_text(encoding='utf-8'))['actors']}
def hub(name,role=0):return next(h for h in state(role)['hub'] if h['id']==name)
def quat(text):
    pitch,yaw,roll=[math.radians(float(text.split(k+'=')[1].split()[0]))/2 for k in ('P','Y','R')]
    sp,cp,sy,cy,sr,cr=math.sin(pitch),math.cos(pitch),math.sin(yaw),math.cos(yaw),math.sin(roll),math.cos(roll)
    return [cr*sp*sy-sr*cp*cy,-cr*sp*cy-sr*cp*sy,cr*cp*sy-sr*sp*cy,cr*cp*cy+sr*sp*sy]
def error(a,b):return math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(a,b))))))
def check(name,test,seconds=5):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def aim(view,target):
    place(0,*view);time.sleep(.3);eye=vec(me()['camera']);delta=[target[i]-eye[i] for i in range(3)]
    send(op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))));time.sleep(.15)
def close(name):
    if me()['lootTarget']!='None':send(op='key',key='Tab')
    else:
        if name=='HUB_Stove_Oven':
            # The open door is horizontal; target its moving surface.
            angle=abs(float(hub(name)['parts'][0]['rotation'].split('R=')[1].split()[0]))
            radians=math.radians(angle)
            eye=vec(me()['camera']);target=(265,-5+35*math.sin(radians),17+35*math.cos(radians));delta=[target[i]-eye[i] for i in range(3)]
            send(op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))))
        send(op='interact')
def endpoints(name,opened,role=0):
    expected={p['mesh']:p for p in manifest[name]['parts']}
    return all(error(quat(p['rotation']),expected[p['mesh']]['rotation'] if opened else [0,0,0,1])<.3 for p in hub(name,role)['parts'])
try:
    send(op='npcs',value=0);send(op='close_loot');send(op='slot',value=2);send(op='sample_interval',seconds=.025)
    fixtures=[(f'HUB_Chest_0{i+1}',(x,-480,100),(x,-650,60)) for i,x in enumerate((-480,-250,-20,210))]
    fixtures += [('HUB_Refrigerator',(915,340,100),(915,500,140)),('HUB_Kitchen_WallCabinet',(515,120,100),(515,-55,210)),('HUB_Kitchen_SinkUnit',(540,140,100),(540,-2,50)),('HUB_Stove_Oven',(265,130,100),(265,-20,55))]
    for name,view,target in fixtures:
        send(op='close_loot');aim(view,target)
        if hub(name)['open']:
            send(op='interact');time.sleep(.3);close(name);time.sleep(.7)
        check(name+': closed hinge pose',lambda name=name:endpoints(name,False))
        send(op='interact');wait(lambda:hub(name)['open'])
        history=[];end=time.monotonic()+.25
        while time.monotonic()<end:history.append(hub(name)['parts']);time.sleep(.025)
        close(name);time.sleep(.7)
        check(name+': reverses smoothly to closed pose',lambda name=name:not hub(name)['open'] and endpoints(name,False))
        check(name+': intermediate rotation exists',lambda name=name,history=history:any(.5<error(quat(p['rotation']),[0,0,0,1])<85 for frame in history for p in frame))
        aim(view,target);send(op='interact')
        check(name+': signed opening endpoint matches Blender hinge',lambda name=name:hub(name)['open'] and endpoints(name,True))
        check(name+': remote peer agrees on open pose',lambda name=name:hub(name,1)['open'] and endpoints(name,True,1))
        samples[name]=history
        if name in ('HUB_Chest_01','HUB_Stove_Oven'):
            send(op='shot');time.sleep(.4)
            base=ROOT/'Artifacts/Windows/PersonaTrial' if os.environ.get('PERSONA_PACKAGED')=='1' else ROOT
            shutil.copy2(base/'Artifacts/Previews/Player0_game.png',ROOT/f'Artifacts/Previews/PERSONA_05_{name}.png')
        close(name);time.sleep(.7)
finally:send(op='sample_interval',seconds=.1);send(op='close_loot');report('phase5-container-motion',checks,samples=samples)
