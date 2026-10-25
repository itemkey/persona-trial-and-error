"""Focused item import/update. Does not rebuild existing levels or character assets."""
import unreal as u
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DATA=json.loads((ROOT/'SourceArt/Items/export_manifest.json').read_text(encoding='utf-8'))
DEST='/Game/Persona/Items'
LIB=u.EditorAssetLibrary
TOOLS=u.AssetToolsHelpers.get_asset_tools()
M=u.MaterialEditingLibrary
materials={}
for name,record in DATA['materials'].items():
    path=DEST+'/Materials/'+record['asset']
    mat=u.load_asset(path)
    if not mat: mat=TOOLS.create_asset(record['asset'],DEST+'/Materials',u.Material,u.MaterialFactoryNew())
    M.delete_all_material_expressions(mat)
    color=M.create_material_expression(mat,u.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant',u.LinearColor(*record['color']))
    M.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
    for value,prop in [(1,u.MaterialProperty.MP_ROUGHNESS),(.08,u.MaterialProperty.MP_SPECULAR)]:
        node=M.create_material_expression(mat,u.MaterialExpressionConstant)
        node.set_editor_property('r',value); M.connect_material_property(node,'',prop)
    mat.set_editor_property('two_sided',True)
    M.recompile_material(mat); LIB.save_loaded_asset(mat); materials[name]=mat

meshes={}
for name,record in DATA['items'].items():
    options=u.FbxImportUI(); options.import_as_skeletal=False
    options.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
    options.automated_import_should_detect_type=False
    options.import_materials=False; options.import_textures=False; options.import_animations=False
    data=options.static_mesh_import_data
    data.combine_meshes=True; data.auto_generate_collision=True
    data.convert_scene=True; data.force_front_x_axis=False
    data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS
    task=u.AssetImportTask(); task.filename=str(ROOT/'SourceArt/Items'/(name+'.fbx'))
    task.destination_path=DEST; task.destination_name=name; task.automated=True
    task.replace_existing=True; task.save=True; task.options=options; task.factory=u.FbxFactory()
    TOOLS.import_asset_tasks([task]); mesh=u.load_asset(DEST+'/'+name)
    if not mesh: raise RuntimeError('Import failed '+name)
    # FBX retains material-slot order from the evaluated export.
    for i,mat_name in enumerate(record['materials']): mesh.set_material(i,materials[mat_name])
    sub=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    sub.remove_collisions(mesh); sub.add_simple_collisions(mesh,u.ScriptingCollisionShapeType.BOX)
    LIB.save_loaded_asset(mesh); meshes[name]=mesh
    u.log('ITEM_BOUNDS '+name+' '+str(mesh.get_bounds()))

definition_class=u.load_class(None,'/Script/PersonaTrial.PersonaItemDefinition')
definitions={}
for name,mesh,label,stack,offset,left,right in [
    ('DA_Shotgun','SM_Shotgun','Дробовик',1,(45,13,-25),(8,-4,-4),(-22,4,-9)),
    ('DA_Ammo12GA','SM_Ammo12GA','12-Gauge Buckshot',20,(55,8,-22),(-8,-10,-5),(-8,10,-5))]:
    definition=u.load_asset(DEST+'/'+name)
    if not definition:
        factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',definition_class)
        definition=TOOLS.create_asset(name,DEST,definition_class,factory)
    for key,value in [('display_name',label),('max_stack',stack),('mesh',meshes[mesh]),
                      ('carry_offset',u.Vector(*offset)),('left_grip',u.Vector(*left)),('right_grip',u.Vector(*right))]:
        definition.set_editor_property(key,value)
    LIB.save_loaded_asset(definition); definitions[name]=definition

level=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
prop_class=u.load_class(None,'/Script/PersonaTrial.PersonaProp')
for map_name in ['L_Hub','L_TestChamber']:
    level.load_level('/Game/Persona/Maps/'+map_name)
    existing={a.get_actor_label():a for a in actors.get_all_level_actors()}
    # In clear view to the right of the hub spawn; separate pickups exercise
    # 12+12 -> 20+4, a third ammo stack, and rejection when all slots are full.
    for i,(kind,count) in enumerate([('DA_Shotgun',1),('DA_Ammo12GA',12),('DA_Ammo12GA',12),('DA_Ammo12GA',20),('DA_Shotgun',1)]):
        label='Inventory test '+str(i+1)
        actor=existing.get(label)
        if not actor:
            actor=actors.spawn_actor_from_class(prop_class,u.Vector(-1200+i*150,-550,75))
            actor.set_actor_label(label)
        actor.set_editor_property('definition',definitions[kind]); actor.set_editor_property('quantity',count)
        actor.mesh.set_static_mesh(meshes['SM_Shotgun' if kind=='DA_Shotgun' else 'SM_Ammo12GA'])
        actor.set_actor_scale3d(u.Vector(1,1,1))
    level.save_current_level()
LIB.save_directory(DEST,only_if_is_dirty=True,recursive=True)
u.log('PERSONA_ITEMS_IMPORT_OK')
