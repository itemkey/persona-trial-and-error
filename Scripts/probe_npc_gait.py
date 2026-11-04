import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());parents=json.loads((root/'SourceArt/Persona/export_manifest.json').read_text())['parents'];names=['root']+list(parents);result={}
for path in u.EditorAssetLibrary.list_assets('/Game/Persona/Animation/NPC',recursive=False):
    a=u.load_asset(path)
    if not isinstance(a,u.AnimSequence) or not any(s in a.get_name() for s in ('Walk_Fwd','Jog_Fwd','CrouchMove_0')):continue
    rows=[]
    for i in range(81):
        cs={}
        for bone in names:
            local=u.AnimationLibrary.get_bone_pose_for_time(a,bone,a.get_play_length()*i/80,False)
            cs[bone]=u.MathLibrary.compose_transforms(local,cs[parents[bone]]) if bone in parents else local
        rows.append(dict(phase=i/80,**{b:[cs[b].translation.x,cs[b].translation.y,cs[b].translation.z] for b in ('foot_l','foot_r','pelvis')}))
    result[a.get_name()]=dict(seconds=a.get_play_length(),rows=rows)
(root/'.work/npc-gait.json').write_text(json.dumps(result,indent=2));u.log('NPC_GAIT_PROBE_OK')
