"""Black-box checks through the Development-only file diagnostics transport.
Run managed host + 3 clients with -Diagnostics before invoking this script.
"""
import json,time,argparse,datetime
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--players',type=int,default=4);p.add_argument('--report',default='phase4-cycle');p.add_argument('--diagnostics');args=p.parse_args()
ROOT=Path(__file__).resolve().parents[1];DIAG=Path(args.diagnostics) if args.diagnostics else ROOT/'.work/Diagnostics'
checks=[];states={};performance={}
def state(role=0):
 data=json.loads((DIAG/f'Player{role}.json').read_bytes())
 if time.time()-datetime.datetime.fromisoformat(data['utc'].replace('Z','+00:00')).timestamp()>5:raise ValueError('Stale diagnostics')
 return data
def send(role,**cmd):
 dest=DIAG/f'Player{role}.command';end=time.monotonic()+15
 while dest.exists():
  if time.monotonic()>end:raise RuntimeError('Unconsumed command '+str(dest))
  time.sleep(.1)
 tmp=dest.with_suffix('.tmp');tmp.write_text(json.dumps(cmd),encoding='utf-8');tmp.replace(dest)
def wait(name,test,seconds=30):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   states.update({i:state(i) for i in range(args.players)})
   if test():checks.append({'check':name,'passed':True});print('PASS',name,flush=True);return
  except (OSError,ValueError,KeyError,IndexError,StopIteration):pass
  time.sleep(.2)
 checks.append({'check':name,'passed':False,'states':states});raise AssertionError(name)
def me(role=0):return next((c for c in states[role]['characters'] if c['id']==states[role]['localPlayer']),{})
def run(role=0):return states[role]['run']
def live():return [c for c in states[0]['characters'] if c['npc'] and not c['dead']]
def coord(s,axis):return float(s.split(axis+'=')[1].split()[0])
def crates(role=0):return [h for h in states[role]['hub'] if 'Container_' in h['id']]
def sample_performance(label,seconds=6):
 samples=[];end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=state();samples.append({k:s.get(k) for k in ['utc','frameMs','gameThreadMs','renderThreadMs','gpuMs','inBytesSec','outBytesSec','viewportWidth','viewportHeight','screenPercentage','packetLagMs','packetLossPercent']});time.sleep(.5)
 performance[label]=samples
try:
 wait('All peers in the new hub with empty hands',lambda:all(states[i]['map']=='L_Hub' and not me(i)['armsVisible'] for i in range(args.players)),150)
 if args.players>1:
  send(1,op='decision',accept=True);time.sleep(1);wait('Non-host cannot accept trial',lambda:run()['phase']==0)
 send(0,op='decision',accept=False);wait('Refusal changes shared respect and difficulty',lambda:run()['respect']==-10 and run()['penalty']==1)
 send(0,op='decision',accept=True);wait('Arena streams on every peer',lambda:all(run(i)['phase']==1 and run(i)['loaded'] for i in range(args.players)),90)
 send(0,op='lift');time.sleep(1);wait('Lift rejects absent passengers',lambda:run()['lift']==0)
 send(0,op='board');time.sleep(3)
 send(0,op='fixture',definition='DA_Ammo12GA',quantity=7,store=False);time.sleep(2)
 send(0,op='lift');wait('All players and loose cargo captured for descent',lambda:run()['lift']==1 and run()['passengers']==args.players and run()['cargo']==1)
 send(0,op='save')
 wait('First wave starts at the bottom',lambda:run()['phase']==2 and run()['wave']==0 and len(live())==5,35)
 wait('Difficulty scales NPC MaxHealth above 100',lambda:all(c['maxHealth']==110 for c in live()))
 wait('All passengers arrive safely on every peer',lambda:all(coord(me(i)['position'],'Z')< -12000 and not me(i)['transit'] and me(i)['health']>0 for i in range(args.players)))
 wait('Exactly two stocked arena containers',lambda:len(crates())==2 and all(sum(r['quantity'] for r in h['items'] if 'DA_Ammo12GA' in r['definition'])==40 and sum(r['quantity'] for r in h['items'] if 'DA_Shotgun' in r['definition'])==1 for h in crates()))
 wait('NPCs navigate through the opened service gate',lambda:all(coord(c['position'],'Y')>-5650 for c in live()),25)
 # Lifecycle test uses authoritative damage to advance waves; shotgun hits are
 # exercised separately, so this must never be reported as a combat playthrough.
 for wave,count in enumerate([5,10,20,30]):
  wait(f'Wave {wave+1} has {count} living NPCs',lambda wave=wave,count=count:run()['wave']==wave and len(live())==count,35)
  if wave==3:
   send(0,op='look',yaw=-70,pitch=0);time.sleep(1)
   sample_performance('30_alive_35_corpses')
  send(0,op='finish_wave')
  if wave<3:wait(f'Wave {wave+1} enters 15-second break',lambda:run()['phase']==3 and 10<run()['remaining']<=15)
 wait('65 corpses remain after four waves',lambda:run()['phase']==4 and len([c for c in states[0]['characters'] if c['npc'] and c['dead']])==65)
 wait('Corpses settle and retain six rounds',lambda:all(c['poseBones']>0 and sum(r['quantity'] for r in c['lootItems'])==6 for c in states[0]['characters'] if c['npc']),12)
 sample_performance('65_corpses',4)
 # Only host evacuates. The other three remain below for the smoke deadline.
 for i in range(1,args.players):
  send(0,op='place',player=states[i]['localPlayer'],x=1000+i*120,y=2000,z=-12505,yaw=0,pitch=0)
 send(0,op='place',player=states[0]['localPlayer'],x=0,y=0,z=-12505,yaw=0,pitch=0);time.sleep(1)
 send(0,op='lift');wait('Partial-group return is allowed',lambda:run()['lift']==3 and run()['passengers']==1)
 wait('Arrival grants reward once',lambda:run()['respect']==0 and run()['bonus']==20 and run()['rewarded'],35)
 wait('Arena cleans up and dead participants return empty',lambda:run()['phase']==0 and not any(c['npc'] for c in states[0]['characters']) and all(me(i)['health']>0 and coord(me(i)['position'],'Z')> -5000 for i in range(args.players)),50)
 send(0,op='decision',accept=True);wait('A subsequent run loads cleanly',lambda:all(run(i)['loaded'] for i in range(args.players)),60)
 send(0,op='board');time.sleep(3);send(0,op='lift')
 wait('Bonus split once: fifty rounds in each container',lambda:run()['lift']==1 and len(crates())==2 and all(sum(r['quantity'] for r in h['items'] if 'DA_Ammo12GA' in r['definition'])==50 for h in crates()))
 send(0,op='save')
finally:
 out=ROOT/'Artifacts/Validation';out.mkdir(parents=True,exist_ok=True)
 (out/(args.report+'.json')).write_text(json.dumps({'checks':checks,'performance':performance,'note':'Wave lifecycle uses diagnostic damage; separate weapon validation required. Performance is a short local multi-process sample, not a midrange-PC guarantee.'},indent=2),encoding='utf-8')
