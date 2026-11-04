"""Import independently authored NPC stances, in-place clips and body physics."""
import unreal as u,json
from pathlib import Path
ROOT=Path(u.Paths.project_dir());BASE='/Game/Persona';DEST=BASE+'/Animation/NPC'
L=u.EditorAssetLibrary;T=u.AssetToolsHelpers.get_asset_tools()
def save(a):
    if not a:raise RuntimeError('Missing NPC asset')
    L.save_loaded_asset(a,only_if_is_dirty=False)
def copy(src,dst):return u.load_asset(dst) or L.duplicate_asset(src,dst)
skeleton=u.load_asset(BASE+'/Characters/SK_Persona_Skeleton')
for clip in json.loads((ROOT/'SourceArt/NPCMotion/stances.json').read_text()):
    name=clip['name'];options=u.FbxImportUI()
    for key,value in dict(import_mesh=False,import_as_skeletal=True,import_animations=True,automated_import_should_detect_type=False,
                          mesh_type_to_import=u.FBXImportType.FBXIT_ANIMATION,import_materials=False,import_textures=False,skeleton=skeleton).items():options.set_editor_property(key,value)
    data=options.get_editor_property('anim_sequence_import_data')
    data.set_editor_property('convert_scene',True);data.set_editor_property('force_front_x_axis',False)
    task=u.AssetImportTask();task.filename=str(ROOT/'SourceArt/NPCMotion'/f'{name}.fbx');task.destination_path=DEST;task.destination_name=name
    task.automated=True;task.replace_existing=True;task.save=True;task.options=options;task.factory=u.FbxFactory()
    T.import_asset_tasks([task])
    a=u.load_asset(DEST+'/'+name)
    if not a:raise RuntimeError(name+' import failed '+str(task.imported_object_paths))
    a.set_editor_property('enable_root_motion',False);a.set_editor_property('force_root_lock',True)
    a.set_editor_property('root_motion_root_lock',u.RootMotionRootLock.ANIM_FIRST_FRAME);save(a)
for path in L.list_assets(BASE+'/Animation/Retargeted',recursive=False):
    a=u.load_asset(path)
    if isinstance(a,u.AnimSequence):
        out=copy(path,DEST+'/'+a.get_name());u.PersonaAssetTools.normalize_npc_clip(a,out);out.set_editor_property('enable_root_motion',False)
        out.set_editor_property('force_root_lock',True);out.set_editor_property('root_motion_root_lock',u.RootMotionRootLock.ANIM_FIRST_FRAME);save(out)
for name,crouch in [('BS_Locomotion',False),('BS_Crouch',True)]:
    blend=copy(BASE+'/Animation/Retargeted/P_BS_Idle_Walk_Run',DEST+'/'+name)
    u.PersonaAssetTools.configure_npc_blend(blend,crouch);save(blend)
pm=u.load_asset(BASE+'/Characters/PM_NPCBody')
if not pm:pm=T.create_asset('PM_NPCBody',BASE+'/Characters',u.PhysicalMaterial,u.PhysicalMaterialFactoryNew())
pm.set_editor_property('friction',.8);pm.set_editor_property('restitution',0)
pm.set_editor_property('sleep_linear_velocity_threshold',3)
pm.set_editor_property('sleep_angular_velocity_threshold',.2)
pm.set_editor_property('sleep_counter_threshold',30);save(pm)
physics=copy(BASE+'/Characters/SK_Persona_PhysicsAsset',BASE+'/Characters/PHYS_NPC')
u.PersonaAssetTools.configure_npc_physics(physics,u.load_asset(BASE+'/Characters/SK_Persona'));save(physics)
bp=u.load_asset(BASE+'/AI/BP_PersonaNPC');d=u.get_default_object(bp.generated_class())
d.get_editor_property('mesh').set_physics_asset(physics,True)
d.set_editor_property('use_controller_rotation_yaw',False)
d.set_editor_property('walk_speed',180);d.set_editor_property('sprint_speed',650)
move=d.get_editor_property('character_movement')
for key,value in dict(max_walk_speed=420,max_walk_speed_crouched=120,max_acceleration=1200,braking_deceleration_walking=1600,
                      ground_friction=2,braking_friction=0,use_separate_braking_friction=True,
                      use_controller_desired_rotation=True,orient_rotation_to_movement=False,rotation_rate=u.Rotator(yaw=360)).items():move.set_editor_property(key,value)
save(bp)
profile=u.load_asset(BASE+'/AI/DA_Combat')
for key,value in dict(patrol_speed=180,sneak_speed=120,combat_speed=420,sprint_speed=650,acceleration=1200,braking=1600,turn_rate=360,
                      attack_stand_distance=95,strike_fractions=[.4,.45,.3,.625],strike_bones=['hand_r','hand_l','foot_r','hand_r'],
                      stance_hold=1.2,crouch_half_height=75,pose_transition_time=.2,ragdoll_blend_time=.12,
                      normal_windup=.22,heavy_windup=.45,impact_body_delta_speed=150,impact_total_delta_speed=60).items():profile.set_editor_property(key,value)
save(profile)
u.log('PERSONA_NPC_MOTION_ASSETS_OK')
