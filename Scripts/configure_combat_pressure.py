"""Update only NPC stance/motion/hit zones and shotgun tuning; retain ragdoll."""
import json
from pathlib import Path
import unreal as u
ROOT=Path(u.Paths.project_dir());BASE='/Game/Persona';DEST=BASE+'/Animation/NPC'
L=u.EditorAssetLibrary;T=u.AssetToolsHelpers.get_asset_tools()
def save(a):
    if not a:raise RuntimeError('Missing combat asset')
    L.save_loaded_asset(a,only_if_is_dirty=False)
skeleton=u.load_asset(BASE+'/Characters/SK_Persona_Skeleton')
for clip in json.loads((ROOT/'SourceArt/NPCMotion/stances.json').read_text()):
    name=clip['name'];options=u.FbxImportUI()
    for k,v in dict(import_mesh=False,import_as_skeletal=True,import_animations=True,automated_import_should_detect_type=False,
                    mesh_type_to_import=u.FBXImportType.FBXIT_ANIMATION,import_materials=False,import_textures=False,skeleton=skeleton).items():options.set_editor_property(k,v)
    data=options.get_editor_property('anim_sequence_import_data');data.set_editor_property('convert_scene',True);data.set_editor_property('force_front_x_axis',False)
    task=u.AssetImportTask();task.filename=str(ROOT/'SourceArt/NPCMotion'/f'{name}.fbx');task.destination_path=DEST;task.destination_name=name
    task.automated=True;task.replace_existing=True;task.save=True;task.options=options;task.factory=u.FbxFactory();T.import_asset_tasks([task])
    a=u.load_asset(DEST+'/'+name);a.set_editor_property('enable_root_motion',False);a.set_editor_property('force_root_lock',True)
    a.set_editor_property('root_motion_root_lock',u.RootMotionRootLock.ANIM_FIRST_FRAME);save(a)
for name,crouch in [('BS_Locomotion',False),('BS_Crouch',True)]:
    blend=u.load_asset(DEST+'/'+name);u.PersonaAssetTools.configure_npc_blend(blend,crouch);save(blend)
profile=u.load_asset(BASE+'/AI/DA_Combat')
values=dict(patrol_speed=220,sneak_speed=180,combat_speed=780,sprint_speed=1150,acceleration=2600,braking=3000,turn_rate=540,
    crouch_half_height=65,burst_duration=2,burst_recovery=3,progress_interval=1,progress_distance=30,strike_radius=20,
    attack_spacing=.12,attack_slots=3,attack_reach=150,recovery_time=.4,memory_time=15)
for k,v in values.items():profile.set_editor_property(k,v)
u.PersonaAssetTools.configure_npc_hit_volumes(profile,u.load_asset(BASE+'/Characters/SK_Persona'));save(profile)
bp=u.load_asset(BASE+'/AI/BP_PersonaNPC');d=u.get_default_object(bp.generated_class())
d.set_editor_property('walk_speed',220);d.set_editor_property('sprint_speed',1150)
move=d.get_editor_property('character_movement')
for k,v in dict(max_walk_speed=220,max_walk_speed_crouched=180,max_acceleration=2600,braking_deceleration_walking=3000,
               rotation_rate=u.Rotator(yaw=540)).items():move.set_editor_property(k,v)
save(bp)
gun=u.load_asset(BASE+'/Items/DA_Shotgun')
for k,v in dict(pellets=24,pellet_damage=24,spread_degrees=5,falloff_start=600,falloff_end=2000,minimum_damage_scale=.2).items():gun.set_editor_property(k,v)
save(gun)
zones=[{k:str(h.get_editor_property(k)) for k in ('bone','center','extent')} for h in profile.get_editor_property('hit_volumes')]
out=ROOT/'Artifacts/Validation';out.mkdir(parents=True,exist_ok=True)
(out/'phase8-assets.json').write_text(json.dumps(dict(profile=values,hitVolumes=zones),indent=2))
u.log('PERSONA_COMBAT_PRESSURE_ASSETS_OK')
