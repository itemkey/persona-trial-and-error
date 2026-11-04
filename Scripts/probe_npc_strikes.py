import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());parents=json.loads((root/'SourceArt/Persona/export_manifest.json').read_text())['parents'];names=['root']+list(parents)
result={}
for name in ['MM_Attack_01','MM_Attack_02','MM_Attack_03','MM_ChargedAttack']:
 a=u.load_asset('/Game/Persona/Animation/NPC/P_'+name);rows=[]
 for i in range(41):
  t=a.get_play_length()*i/40;cs={}
  for bone in names:
   local=u.AnimationLibrary.get_bone_pose_for_time(a,bone,t,False)
   cs[bone]=u.MathLibrary.compose_transforms(local,cs[parents[bone]]) if bone in parents else local
  rows.append(dict(fraction=i/40,**{b:[cs[b].translation.x,cs[b].translation.y,cs[b].translation.z] for b in ['pelvis','hand_l','hand_r','foot_l','foot_r']}))
 result[name]=rows
(root/'.work/npc-strikes.json').write_text(json.dumps(result,indent=2));u.log('NPC_STRIKE_PROBE_OK')
