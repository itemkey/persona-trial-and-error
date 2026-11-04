"""Read-only animation probe: authored scale and root/pelvis track ranges."""
import unreal as u,json
from pathlib import Path
out={}
for directory in ['/Game/Persona/Animation/Stances','/Game/Persona/Animation/NPC']:
 for path in u.EditorAssetLibrary.list_assets(directory,recursive=False):
  a=u.load_asset(path)
  if not isinstance(a,u.AnimSequence):continue
  length=a.get_play_length();samples={}
  for bone in ['root','pelvis','thigh_l','calf_l','foot_l']:
   values=[]
   for i in range(9):
    t=u.AnimationLibrary.get_bone_pose_for_time(a,bone,length*i/8,False)
    values.append(dict(p=[t.translation.x,t.translation.y,t.translation.z],s=[t.scale3d.x,t.scale3d.y,t.scale3d.z]))
   samples[bone]=values
  out[path]=dict(seconds=length,bones=samples)
(Path(u.Paths.project_dir())/'.work/npc-animation-probe.json').write_text(json.dumps(out,indent=2))
u.log('PERSONA_NPC_ANIMATION_PROBE_OK')
