"""Production UI, item and moving-base regression checks on diagnostic peers."""
import argparse, math, time, shutil
from phase4_checks import ROOT,DIAG,state,me,vec,send,place,wait,report

parser=argparse.ArgumentParser()
parser.add_argument('--scenario',choices=['ui','lift','fall','syringe'],default='ui')
parser.add_argument('--players',type=int,default=2)
args=parser.parse_args();checks=[]

def check(name,fn,seconds=15):
    try:wait(fn,seconds)
    except Exception:
        checks.append(dict(check=name,passed=False));raise AssertionError(name) from None
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)

def key(name,role=0,ui=False,**extra):send(role,op='key',key=name,ui=ui,**extra)
def path(name):return '/Game/Persona/Items/'+name+'.'+name
def quantity(name,role=0):return sum(s['quantity'] for s in me(role)['slots'] if s['definition']==name)
def photo(name,role=0):
    started=time.time();send(role,op='shot');src=DIAG.parent.parent/'Artifacts/Previews'/f'Player{role}_game.png'
    check('Screenshot '+name,lambda:src.exists() and src.stat().st_mtime>=started)
    shutil.copy2(src,ROOT/'Artifacts/Previews'/('V0101-'+name+'.png'))

def ui():
    key('Tab');time.sleep(.3);check('Tab does not open administration',lambda:not state()['panelOpen'])
    key('Tilde');check('Host opens administration',lambda:state()['panelOpen'])
    key('Tilde',1);time.sleep(.4);check('Client cannot open administration',lambda:not state(1)['panelOpen'])
    send(1,op='admin_give',item=path('DA_Syringe'),quantity=1);time.sleep(.7)
    check('Server rejects client item grant',lambda:quantity('DA_Syringe',1)==0)
    send(op='ui_text',field='item',text='ШПРИЦ');check('Administrator searches actual item catalog',lambda:state()['ui']['adminItems']==1)
    send(op='ui_click',text='Выдать выбранный предмет');check('Administrator grants selected item',lambda:quantity('DA_Syringe')==1)
    send(op='ui_text',field='console',text='persona.CameraMotion 0',commit=True)
    check('Integrated console executes and records command',lambda:'persona.CameraMotion 0' in state()['ui']['console'])
    photo('admin');key('Tilde',ui=True);check('Backtick closes focused console panel',lambda:not state()['panelOpen'])
    send(op='terminal');check('Terminal opens',lambda:state()['terminalOpen'])
    for text,expected,icons in [('дробовик',['Shooting'],3),('ПАТРОН',['Shooting','SurvivalAgility'],3),('вода',['SurvivalAgility'],6),('ШПРИЦ',['Shooting','SurvivalAgility'],6),('несуществующий',[],0),('', ['Shooting','SurvivalAgility'],3)]:
        send(op='ui_text',field='trial',text=text)
        check('Catalog query '+repr(text),lambda expected=expected:set(state()['ui']['trials'])==set(expected))
        if len(expected)==1:check('Actual item icon row '+text,lambda icons=icons:state()['ui']['icons']==icons)
    send(op='ui_text',field='trial',text='вода');photo('catalog')
    key('Tab',ui=True);check('Tab closes terminal with search focused',lambda:not state()['terminalOpen'] and not state()['panelOpen'])
    key('Escape');check('Esc opens social and pause menu',lambda:state()['pauseOpen']);photo('pause');key('Escape',ui=True)
    check('Esc closes pause menu',lambda:not state()['pauseOpen'])
    place(0,-800,1100,100);time.sleep(.5);before=vec(me()['position'])
    send(op='move',x=1,y=0);time.sleep(.5);send(op='move',x=0,y=0)
    check('Movement restored after UI closes',lambda:vec(me()['position'])[0]>before[0]+60)

def lift():
    place(0,-300,-393,-400,yaw=180,pitch=-15);time.sleep(.8);send(op='decision',accept=True,trialId='SurvivalAgility')
    check('Forest accepted and streamed',lambda:all(state(i)['run']['loaded'] and state(i)['run']['phase']==1 for i in range(args.players)),60)
    check('Accepting trial adds no visible cabin box',lambda:state()['run']['visibleShutters']==0)
    place(0,-700,-700,-408,yaw=45,pitch=-8);time.sleep(.5);photo('hub-accepted');send(op='board');time.sleep(.8)
    key('LeftShift',1,held=True);send(op='lift')
    check('Lift starts with manifest',lambda:state()['run']['lift']==1 and state()['run']['passengers']==args.players)
    time.sleep(2);send(1,op='move',x=0,y=1);time.sleep(.2);send(1,op='move',x=0,y=0)
    before=vec(me(1)['position']);time.sleep(.5)
    check('Passenger can move relative to platform',lambda:abs(vec(me(1)['position'])[1]+70)>40)
    key('SpaceBar',1);time.sleep(.15);photo('shaft')
    send(1,op='crouch',value=True);time.sleep(.4);send(1,op='crouch',value=False)
    send(op='save');time.sleep(.5)
    check('Passengers arrive alive',lambda:all(not me(i)['transit'] and me(i)['health']==100 for i in range(args.players)),40)
    send(op='forest_fixture',quiet=True)
    send(1,op='move',x=0,y=1);check('Shift held before departure still runs after arrival',lambda:me(1)['sprinting'] and me(1)['speed']>650)
    send(1,op='move',x=0,y=0);key('LeftShift',1,held=False)
    check('Shutters fully retract after arrival',lambda:state()['run']['visibleShutters']==0)
    photo('landing')

def fall():
    # Hub upper floor is at Z=0; impact comes from real gravity and collision.
    for height,expected in [(200,100),(500,100-(500-300)/700*100)]:
        send(op='vitals',health=100);time.sleep(.3);place(0,-900,1100,height+92)
        time.sleep(2)
        check('Fall damage at '+str(height)+'cm',lambda expected=expected:abs(me()['health']-expected)<5)
    send(op='vitals',health=100);time.sleep(.3);place(0,-900,1100,1192);time.sleep(2)
    check('High fall is lethal and recoverable in hub',lambda:me()['dead'] and me()['dry'])

def syringe():
    send(op='admin_give',item=path('DA_Syringe'),quantity=3);check('Syringe granted through inventory',lambda:quantity('DA_Syringe')>=3)
    slot=next(i for i,s in enumerate(me()['slots']) if s['definition']=='DA_Syringe');send(op='slot',value=slot);time.sleep(.4)
    before=quantity('DA_Syringe');key('LeftMouseButton');time.sleep(.4);check('Full health does not consume syringe',lambda:quantity('DA_Syringe')==before)
    send(op='hit',actor=me()['actor'],bone='spine_02',damage=80);check('Damage fixture reached real health path',lambda:me()['health']==20)
    key('LeftMouseButton');check('Syringe heals one ordinary hit',lambda:me()['health']==55 and quantity('DA_Syringe')==before-1)
    key('LeftMouseButton');check('Second syringe heals correctly',lambda:me()['health']==90 and quantity('DA_Syringe')==before-2)
    key('LeftMouseButton');check('Healing clamps to maximum',lambda:me()['health']==100 and quantity('DA_Syringe')==before-3)

try:
    check('Diagnostic participants connected',lambda:len([c for c in state()['characters'] if not c['npc'] and c['id']>=0])==args.players,60)
    globals()[args.scenario]()
finally:report('v0101-'+args.scenario,checks)
