"""Bind compiled gameplay classes to imported assets and generated levels."""
import unreal as u
TOOLS=u.AssetToolsHelpers.get_asset_tools()
LIB=u.EditorAssetLibrary
LEVEL=u.get_editor_subsystem(u.LevelEditorSubsystem)
ACTORS=u.get_editor_subsystem(u.EditorActorSubsystem)
BASE='/Game/Persona'

def cls(name):
    result=u.load_class(None,'/Script/PersonaTrial.'+name)
    if not result:raise RuntimeError('Build PersonaTrialEditor before configuring gameplay: '+name)
    return result

def blueprint(name,path,parent):
    existing=u.load_asset(path+'/'+name)
    if existing:return existing
    factory=u.BlueprintFactory();factory.set_editor_property('parent_class',parent)
    return TOOLS.create_asset(name,path,u.Blueprint,factory)

def cdo(bp):return u.get_default_object(bp.generated_class())
body=u.load_asset(BASE+'/Characters/SK_Persona')
arms=u.load_asset(BASE+'/Characters/SK_Persona_Arms')
skeleton=body.get_editor_property('skeleton')
u.PersonaAssetTools.ensure_attachment_sockets(skeleton)
LIB.save_loaded_asset(skeleton)
anim=u.load_asset(BASE+'/Animation/ABP_Persona')
if not anim:
    factory=u.AnimBlueprintFactory()
    factory.set_editor_property('parent_class',cls('PersonaAnimInstance'))
    factory.set_editor_property('target_skeleton',skeleton)
    factory.set_editor_property('preview_skeletal_mesh',body)
    anim=TOOLS.create_asset('ABP_Persona',BASE+'/Animation',u.AnimBlueprint,factory)
    u.BlueprintEditorLibrary.compile_blueprint(anim)
anim_defaults=cdo(anim)
anim_defaults.set_editor_property('locomotion',u.load_asset(BASE+'/Animation/Retargeted/P_BS_Idle_Walk_Run'))
anim_defaults.set_editor_property('jump_clip',u.load_asset(BASE+'/Animation/Retargeted/P_MM_Jump'))
anim_defaults.set_editor_property('hands_clip',u.load_asset(BASE+'/Animation/Retargeted/P_MF_Pistol_Idle_ADS'))
LIB.save_loaded_asset(anim)

for name,path,npc in [('BP_Persona',BASE+'/Characters',False),('BP_PersonaNPC',BASE+'/AI',True)]:
    bp=blueprint(name,path,cls('PersonaCharacter'))
    defaults=cdo(bp)
    defaults.set_editor_property('npc',npc)
    mesh=defaults.get_editor_property('mesh')
    mesh.set_skeletal_mesh_asset(body)
    mesh.set_anim_instance_class(anim.generated_class())
    first=defaults.get_editor_property('first_person_arms')
    first.set_skeletal_mesh_asset(arms)
    first.set_anim_instance_class(anim.generated_class())
    first.set_relative_location(u.Vector(30,0,-168),False,False)
    mesh.set_editor_property('override_materials',[u.load_asset(BASE+'/Materials/'+('M_Observer' if npc else 'M_Persona'))]*mesh.get_num_materials())
    first.set_editor_property('override_materials',[u.load_asset(BASE+'/Materials/M_Persona')]*first.get_num_materials())
    if npc:
        defaults.set_editor_property('ai_controller_class',cls('PersonaAIController'))
        defaults.set_editor_property('auto_possess_ai',u.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED)
        defaults.set_editor_property('walk_speed',190)
    LIB.save_loaded_asset(bp)

for name,native in [('BP_CarryProp','PersonaProp'),('BP_Door','PersonaDoor'),('BP_TrialTerminal','PersonaTerminal')]:
    bp=blueprint(name,BASE+'/Interaction',cls(native))
    cdo(bp).get_editor_property('mesh').set_material(0,u.load_asset(BASE+'/Materials/M_Concrete'))
    LIB.save_loaded_asset(bp)

definition=u.load_asset(BASE+'/Trials/DA_FoundationTrial')
if not definition:
    factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',cls('PersonaTrialDefinition'))
    definition=TOOLS.create_asset('DA_FoundationTrial',BASE+'/Trials',cls('PersonaTrialDefinition'),factory)
definition.set_editor_property('display_name','Foundation / Trial 01')
definition.set_editor_property('map',u.load_asset(BASE+'/Maps/L_TestChamber'))
LIB.save_loaded_asset(definition)

for map_name,trial in [('L_Hub',False),('L_TestChamber',True)]:
    LEVEL.load_level(BASE+'/Maps/'+map_name)
    world=u.EditorLevelLibrary.get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',cls('PersonaGameMode'))
    existing={a.get_actor_label() for a in ACTORS.get_all_level_actors()}
    def placed(bp_path,label,loc):
        if label in existing:return None
        bp=u.load_asset(bp_path)
        actor=ACTORS.spawn_actor_from_class(bp.generated_class(),u.Vector(*loc),u.Rotator())
        actor.set_actor_label(label);actor.set_editor_property('tags',['PersonaGameplay'])
        return actor
    terminal=placed(BASE+'/Interaction/BP_TrialTerminal','Trial terminal',(1800 if not trial else -2350,350,80))
    if terminal:terminal.set_editor_property('go_to_trial',not trial)
    for i in range(3):placed(BASE+'/Interaction/BP_CarryProp','Carry object %d'%i,(-1200+i*150,250,60))
    placed(BASE+'/Interaction/BP_Door','Interaction door',(-400,-400,0))
    if not u.PersonaAssetTools.build_navigation(world):
        raise RuntimeError('Navigation build did not produce a reachable surface: '+map_name)
    LEVEL.save_current_level()
LIB.save_directory(BASE,only_if_is_dirty=False,recursive=True)
print('PERSONA_GAMEPLAY_ASSETS_OK')
