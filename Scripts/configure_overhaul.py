"""Idempotent FPS tuning/animation/hinge/cover upgrade; retains existing map lighting."""
import unreal as u
import json
from pathlib import Path

ROOT=Path(u.Paths.project_dir())
LIB=u.EditorAssetLibrary
TOOLS=u.AssetToolsHelpers.get_asset_tools()
ACTORS=u.get_editor_subsystem(u.EditorActorSubsystem)
LEVEL=u.get_editor_subsystem(u.LevelEditorSubsystem)
BASE='/Game/Persona'

def save(asset):
    if not asset:raise RuntimeError('Missing required overhaul asset')
    LIB.save_loaded_asset(asset,only_if_is_dirty=False)

def native(name):return u.load_class(None,'/Script/PersonaTrial.'+name)

# Reuse the established retargeter, including its calibrated PERSONA proportions.
paths=[f'/Game/Characters/Mannequins/Anims/Unarmed/Attack/{n}' for n in
       ['MM_Attack_01','MM_Attack_02','MM_Attack_03','MM_ChargedAttack']]
paths+=['/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land',
        '/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Med_01']
inputs=u.IKRetargetBatchOperationInputs()
inputs.assets_to_retarget=[LIB.find_asset_data(p) for p in paths]
inputs.source_mesh=u.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
inputs.target_mesh=u.load_asset(BASE+'/Characters/SK_Persona')
inputs.ik_retarget_asset=u.load_asset(BASE+'/Animation/RTG_Manny_Persona')
inputs.prefix='P_';inputs.target_path=BASE+'/Animation/Retargeted'
inputs.include_referenced_assets=True;inputs.overwrite_existing_files=True
if any(not LIB.does_asset_exist(BASE+'/Animation/Retargeted/P_'+p.rsplit('/',1)[1]) for p in paths) or '-PersonaRetargetCombat' in u.SystemLibrary.get_command_line():
    if not u.IKRetargetBatchOperation.run_batch_retarget(inputs):raise RuntimeError('Combat retarget failed')
    LIB.save_directory(BASE+'/Animation/Retargeted',only_if_is_dirty=False,recursive=True)
anim=u.load_asset(BASE+'/Animation/ABP_Persona')
defaults=u.get_default_object(anim.generated_class())
defaults.set_editor_property('attack_clips',[u.load_asset(BASE+'/Animation/Retargeted/P_'+p.rsplit('/',1)[1]) for p in paths[:4]])
defaults.set_editor_property('land_clip',u.load_asset(BASE+'/Animation/Retargeted/P_MM_Land'))
defaults.set_editor_property('hit_clip',u.load_asset(BASE+'/Animation/Retargeted/P_MM_HitReact_Front_Med_01'))
save(anim)

profile=u.load_asset(BASE+'/AI/DA_Combat')
if not profile:
    factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',native('PersonaCombatProfile'))
    profile=TOOLS.create_asset('DA_Combat',BASE+'/AI',native('PersonaCombatProfile'),factory)
save(profile)
for path,npc in [(BASE+'/Characters/BP_Persona',False),(BASE+'/AI/BP_PersonaNPC',True)]:
    bp=u.load_asset(path);d=u.get_default_object(bp.generated_class())
    d.set_editor_property('walk_speed',300 if npc else 600);d.set_editor_property('sprint_speed',980)
    d.set_editor_property('combat_profile',profile)
    for key,value in dict(normal_fov=100,sprint_fov=105,aim_fov=85,viewmodel_fov=90,crouch_transition_time=.2).items():d.set_editor_property(key,value)
    move=d.get_editor_property('character_movement')
    for key,value in dict(max_walk_speed=600,max_walk_speed_crouched=280,max_acceleration=4200,braking_deceleration_walking=5200,ground_friction=6,air_control=.55,air_control_boost_multiplier=1).items():move.set_editor_property(key,value)
    camera=d.get_editor_property('camera');camera.set_editor_property('use_pawn_control_rotation',False)
    camera.set_field_of_view(100);camera.set_enable_first_person_field_of_view(True);camera.set_first_person_field_of_view(90)
    camera.set_enable_first_person_scale(True);camera.set_first_person_scale(.35)
    d.get_editor_property('first_person_arms').set_first_person_primitive_type(u.FirstPersonPrimitiveType.FIRST_PERSON)
    save(bp)

gun=u.load_asset(BASE+'/Items/DA_Shotgun')
# Authored front bead top is z=13.382 cm; barrel bore is z=7.318 cm.
# Keep the bead on the camera ray and the receiver safely below it.
gun.set_editor_property('sight_alignment',u.Transform(location=u.Vector(75,0,-13.382)))
gun.set_editor_property('muzzle',u.Vector(49.66,0,7.318))
gun.set_editor_property('pump_duration',.45);gun.set_editor_property('load_duration',.6)
for index,kind in enumerate(['Translation','Rotation','Camera']):
    name='C_Shotgun'+kind
    curve=u.load_asset(BASE+'/Items/'+name)
    if not curve:curve=TOOLS.create_asset(name,BASE+'/Items',u.CurveFloat,u.CurveFloatFactory())
    u.PersonaAssetTools.configure_recoil_curve(curve,index);save(curve)
    gun.set_editor_property(kind.lower()+'_recoil_curve',curve)
save(gun)

manifest=json.loads((ROOT/'SourceArt/Phase4/manifest.json').read_text(encoding='utf-8'))
for record in manifest['actors']:
    if not record.get('parts'):continue
    asset=u.load_asset(BASE+'/Hub/DA_'+record['id'])
    if not asset:raise RuntimeError('Missing hub definition '+record['id'])
    parts=[]
    for p in record['parts']:
        part=u.PersonaHubPart()
        part.set_editor_property('mesh',u.load_asset(BASE+'/Hub/Meshes/'+p['mesh']))
        part.set_editor_property('offset',u.Vector(*p['offset']))
        part.set_editor_property('closed_rotation',u.Quat(*p.get('closed_rotation',[0,0,0,1])).rotator())
        part.set_editor_property('open_rotation',u.Quat(*p['rotation']).rotator())
        part.set_editor_property('open_offset',u.Vector(*p['open_offset']))
        parts.append(part)
    asset.set_editor_property('parts',parts);save(asset)

trial=u.load_asset(BASE+'/Trials/DA_Shooting')
for key,value in dict(additional_player_wave_scale=.45,starting_ammo=48,additional_player_ammo=24,reinforcement_interval=3).items():
    trial.set_editor_property(key,value)
trial.set_editor_property('max_np_cs',30) if hasattr(trial,'max_np_cs') else trial.set_editor_property('max_npcs',30)
save(trial)

LEVEL.load_level(BASE+'/Maps/L_Shooting')
existing={a.get_actor_label():a for a in ACTORS.get_all_level_actors()}
cube=u.load_asset('/Engine/BasicShapes/Cube')
material=u.load_asset(BASE+'/Materials/M_Concrete')
# Three open lanes at x=-1800/0/1800. Groups stay clear of both 12 m
# container footprints, the central lift, and the northern service vestibule.
cover=[(-900,-3300),(900,-3300),(-900,-1200),(900,-1200),(-900,1500),(900,1500)]
for index,(x,y) in enumerate(cover):
    for piece,(dx,dy,sx,sy,h) in enumerate([(0,0,400,120,140),(160,160,120,320,220)]):
        name=f'Combat cover {index+1}.{piece+1}'
        actor=existing.get(name) or ACTORS.spawn_actor_from_class(u.StaticMeshActor,u.Vector(x+dx,y+dy,-12600+h/2))
        actor.set_actor_label(name);actor.set_actor_location(u.Vector(x+dx,y+dy,-12600+h/2),False,False)
        actor.set_actor_scale3d(u.Vector(sx/100,sy/100,h/100))
        mesh=actor.static_mesh_component;mesh.set_static_mesh(cube);mesh.set_material(0,material)
        mesh.set_mobility(u.ComponentMobility.STATIC);mesh.set_collision_profile_name('BlockAll')
        actor.set_editor_property('tags',['PersonaPhase5Cover'])
if not u.PersonaAssetTools.build_navigation(u.EditorLevelLibrary.get_editor_world()):raise RuntimeError('Arena navigation failed')
LEVEL.save_current_level()
LEVEL.load_level(BASE+'/Maps/L_Hub')
LIB.save_directory(BASE,only_if_is_dirty=True,recursive=True)
exec(compile((ROOT/'Scripts/configure_combat_readability.py').read_text(encoding='utf-8'),'configure_combat_readability.py','exec'))
u.log('PERSONA_OVERHAUL_ASSETS_OK')
