"""Inspect saved assets in a fresh editor process; fail if bindings were not persisted."""
import json
from pathlib import Path
import unreal as u

BASE='/Game/Persona'
report={}
def asset_path(asset):return asset.get_path_name() if asset else None
for name,path in [('BP_Persona','Characters'),('BP_PersonaNPC','AI')]:
    bp=u.load_asset(f'{BASE}/{path}/{name}')
    before=u.get_default_object(bp.generated_class()).get_editor_property('mesh')
    report[name+'_before_compile']=[asset_path(before.get_material(i)) for i in range(before.get_num_materials())]
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    defaults=u.get_default_object(bp.generated_class())
    mesh=defaults.get_editor_property('mesh')
    arms=defaults.get_editor_property('first_person_arms')
    assert mesh.get_editor_property('skeletal_mesh') is not None, name+' body missing'
    assert arms.get_editor_property('skeletal_mesh') is not None, name+' arms missing'
    assert mesh.get_editor_property('anim_class') is not None, name+' animation missing'
    assert all(mesh.get_material(i) for i in range(mesh.get_num_materials())),name+' missing body material'
    assert all(arms.get_material(i) for i in range(arms.get_num_materials())),name+' missing arms material'
    report[name]=dict(body=mesh.get_editor_property('skeletal_mesh').get_path_name(),
        arms=arms.get_editor_property('skeletal_mesh').get_path_name(),
        animation=mesh.get_editor_property('anim_class').get_path_name(),
        materials=[asset_path(mesh.get_material(i)) for i in range(mesh.get_num_materials())],
        overrides=[asset_path(m) for m in mesh.get_editor_property('override_materials')],
        mesh_slots=[asset_path(m.get_editor_property('material_interface')) for m in mesh.get_editor_property('skeletal_mesh').get_editor_property('materials')],
        npc=defaults.get_editor_property('npc'))
report['material_tones']={name:str(u.MaterialEditingLibrary.get_material_default_vector_parameter_value(u.load_asset(BASE+'/Materials/'+name),'BaseTone')) for name in ['M_Persona','M_Observer']}
blend=u.load_asset(BASE+'/Animation/Retargeted/P_BS_Idle_Walk_Run')
report['blend_axes']=[dict(name=str(p.get_editor_property('display_name')),minimum=p.get_editor_property('min'),maximum=p.get_editor_property('max')) for p in blend.get_editor_property('blend_parameters')]
report['navigation']={}
level=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
for name in ['L_Hub','L_TestChamber']:
    level.load_level(BASE+'/Maps/'+name)
    bounds=[]
    for actor in actors.get_all_level_actors():
        if isinstance(actor,u.NavMeshBoundsVolume):
            origin,extent=actor.get_actor_bounds(False)
            bounds.append(dict(origin=str(origin),extent=str(extent)))
    report['navigation'][name]=bounds
out=Path(__file__).resolve().parents[1]/'Artifacts'/'Validation'
out.mkdir(parents=True,exist_ok=True)
(out/'assets.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('PERSONA_ASSET_VALIDATION '+json.dumps(report))
