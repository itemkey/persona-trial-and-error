"""Frame-by-frame movement checks; collisions and stance changes are labelled."""
import argparse,math,shutil
from phase4_checks import *
p=argparse.ArgumentParser();p.add_argument('--label',default='after');args=p.parse_args()
path=DIAG/'Player0_motion.json';frames=json.loads(read_snapshot(path))['frames'];pairs=[];previous={};contact_speeds=[]
for b in frames:
    a=previous.get(b['id']);previous[b['id']]=b
    if not a or a['dead'] or b['dead'] or a['climbing'] or b['climbing']:continue
    dt=b['serverTime']-a['serverTime']
    if dt<=0:continue
    va,vb=vec(a['velocity']),vec(b['velocity']);ca,cb=vec(a['capsule']),vec(b['capsule'])
    pa,pb=vec(a['pelvis']),vec(b['pelvis']);ra,rb=vec(a['root']),vec(b['root'])
    move=math.dist(ca[:2],cb[:2]);speed_a=math.hypot(*va[:2]);speed_b=math.hypot(*vb[:2])
    yaw=abs((b['yaw']-a['yaw']+180)%360-180)
    local_a=[pa[i]-ra[i] for i in range(3)];local_b=[pb[i]-rb[i] for i in range(3)]
    # Contact-height samples complement visual inspection; they can include
    # toe-off/landing, so this is reported separately from teleport assertions.
    if a['crouched']==b['crouched'] and a['floorContact'] and b['floorContact'] and not a['attacking'] and not b['attacking']:
        for side in ('footL','footR'):
            fa,fb=vec(a[side]),vec(b[side]);floor_a=a.get('floorZ',ca[2]-(75 if a['crouched'] else 90)-2.15);floor_b=b.get('floorZ',cb[2]-(75 if b['crouched'] else 90)-2.15)
            flag='footContact'+side[-1]
            planted=(a.get(flag) and b.get(flag)) if flag in a else (8<fa[2]-floor_a<14 and 8<fb[2]-floor_b<14 and abs(fb[2]-fa[2])/dt<10)
            if planted and speed_a>30 and speed_b>30:
                contact_speeds.append(math.dist(fa[:2],fb[:2])/dt)
    # Rotation changes component-space axes; compare radial offsets as well as
    # raw pose displacement rather than calling every turn a teleport.
    pairs.append(dict(dt=dt,travel=move,speed=speed_b,yawRate=yaw/dt,
        movementExcess=move-max(speed_a,speed_b)*dt,
        pelvisStep=math.dist(local_a,local_b),pelvisRadius=math.hypot(*local_b[:2]),
        collision=b['collisionRevision']!=a['collisionRevision'],stance=b['crouched']!=a['crouched'],
        speedChange=(speed_b-speed_a)/dt,pathChange=b.get('pathRevision')!=a.get('pathRevision')))
assert len(pairs)>100,'Not enough live frames'
stable=[r for r in pairs if not r['collision'] and not r['stance']]
metrics={name:max(r[key] for r in rows) for name,key,rows in [
    ('maxSpeedCmS','speed',pairs),('maxYawDegreesS','yawRate',pairs),
    ('maxMovementExcessCm','movementExcess',stable),('maxPelvisStepCm','pelvisStep',stable),
    ('maxPelvisRadiusCm','pelvisRadius',pairs),('maxAccelerationCmS2','speedChange',stable)]}
if contact_speeds:
    values=sorted(contact_speeds);metrics['footContactHeightSamples']=len(values)
    metrics['footContactSpeedMedianCmS']=values[len(values)//2];metrics['footContactSpeedP95CmS']=values[int(len(values)*.95)]
checks=[dict(check=name,passed=bool(ok)) for name,ok in [
    ('NPC speed stays within sprint profile',metrics['maxSpeedCmS']<660),
    ('Turning obeys 360 degrees/s',metrics['maxYawDegreesS']<375),
    ('Capsule has no unexplained horizontal jump',metrics['maxMovementExcessCm']<6),
    ('Animation never moves pelvis metres away from root',metrics['maxPelvisRadiusCm']<50),
    ('Continuous pose has no large visual jump',metrics['maxPelvisStepCm']<20)]]
out=ROOT/'Artifacts/Validation'/('phase7-motion-trace-'+args.label+'.json');shutil.copyfile(path,out)
report('phase7-motion-metrics-'+args.label,checks,metrics=metrics,frames=len(frames),trace=str(out))
print(json.dumps(dict(checks=checks,metrics=metrics),indent=2));assert all(c['passed'] for c in checks)
