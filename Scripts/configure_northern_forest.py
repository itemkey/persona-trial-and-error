"""Import the northern field kit and its streamed lighting/navigation sublevel."""
import unreal as u, json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=root/'.work/NorthernArt'
kit=json.loads((src/'kit.json').read_text())
dest='/Game/Persona/North'
lib=u.EditorAssetLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
m=u.MaterialEditingLibrary
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
def asset(name,cls,factory):
    path=dest+'/'+name
    return lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name,dest,cls,factory)
def save(obj):
    if not lib.save_loaded_asset(obj,only_if_is_dirty=False):raise RuntimeError(obj.get_path_name())
def material(name,color):
    mat=asset('M_'+name,u.Material,u.MaterialFactoryNew())
    m.delete_all_material_expressions(mat);mat.set_editor_property('two_sided',True)
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    m.set_material_usage(mat,u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    c=m.create_material_expression(mat,u.MaterialExpressionConstant3Vector);c.set_editor_property('constant',u.LinearColor(*color,1))
    normal=m.create_material_expression(mat,u.MaterialExpressionPixelNormalWS)
    pos=m.create_material_expression(mat,u.MaterialExpressionWorldPosition)
    depth=m.create_material_expression(mat,u.MaterialExpressionPixelDepth)
    code=m.create_material_expression(mat,u.MaterialExpressionCustom)
    inputs=[]
    for arg in ('C','N','P','Depth'):
        i=u.CustomInput();i.set_editor_property('input_name',arg);inputs.append(i)
    code.set_editor_property('inputs',inputs); code.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    code.set_editor_property('code',"""float light=.34+.88*saturate(dot(normalize(N),normalize(float3(-.43,-.35,.82))));
float moss=sin(P.x*.0017+sin(P.y*.0023))*sin(P.y*.0013+P.x*.0006);
float3 ink=C*(light+.11*moss);
return lerp(ink,float3(.25,.281,.278),smoothstep(2200.,10000.,Depth)*.97);""" if name!='fog' else 'return C;')
    for node,arg in ((c,'C'),(normal,'N'),(pos,'P'),(depth,'Depth')):
        if not m.connect_material_expressions(node,'',code,arg): raise RuntimeError('Missing shader input '+name+'.'+arg)
    m.connect_material_property(code,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    m.recompile_material(mat);save(mat);return mat
mats={k:material(k,v) for k,v in kit['colors'].items()}
for name,slots in kit['meshes'].items():
    opts=u.FbxImportUI();opts.automated_import_should_detect_type=False;opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
    opts.import_materials=False;opts.import_textures=False;opts.import_animations=False
    opts.static_mesh_import_data.combine_meshes=True;opts.static_mesh_import_data.auto_generate_collision=True
    task=u.AssetImportTask();task.filename=str(src/(name+'.fbx'));task.destination_path=dest;task.destination_name='SM_'+name
    task.automated=True;task.replace_existing=True;task.options=opts;task.factory=u.FbxFactory();task.save=True
    if not lib.does_asset_exist(dest+'/SM_'+name) or globals().get('REIMPORT_NORTH',False):tools.import_asset_tasks([task])
    mesh=lib.load_asset(dest+'/SM_'+name)
    for index,slot in enumerate(mesh.get_editor_property('static_materials')):
        key=str(slot.get_editor_property('imported_material_slot_name')).removeprefix('N_')
        mesh.set_material(index,mats[key if key in mats else slots[index]])
    save(mesh)
apple=lib.load_asset('/Game/Persona/Items/DA_Ration')
apple.set_editor_property('mesh',lib.load_asset(dest+'/SM_Apple'));apple.set_editor_property('display_name','Яблоко')
apple.set_editor_property('carry_offset',u.Vector(43,14,-10));apple.set_editor_property('left_grip',u.Vector(-18,-34,-35));apple.set_editor_property('right_grip',u.Vector(0,6,-20));save(apple)
# Mist on the six planes is faint at rest and brightens close to a lethal face.
mat=asset('M_Boundary',u.Material,u.MaterialFactoryNew());m.delete_all_material_expressions(mat)
mat.set_editor_property('two_sided',True);mat.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT);mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
t=m.create_material_expression(mat,u.MaterialExpressionVectorParameter);t.set_editor_property('parameter_name','Tint');t.set_editor_property('default_value',u.LinearColor(.3,.36,.34,1))
o=m.create_material_expression(mat,u.MaterialExpressionScalarParameter);o.set_editor_property('parameter_name','Veil');o.set_editor_property('default_value',.095)
uv=m.create_material_expression(mat,u.MaterialExpressionTextureCoordinate)
edge=m.create_material_expression(mat,u.MaterialExpressionCustom)
inputs=[]
for key in ['UV','Veil']:
    i=u.CustomInput();i.set_editor_property('input_name',key);inputs.append(i)
edge.set_editor_property('inputs',inputs);edge.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT1)
edge.set_editor_property('code','float2 d=min(UV,1-UV); float rim=1-smoothstep(.001,.004,min(d.x,d.y)); float2 g=abs(frac(UV*6+.5)-.5); return saturate(Veil+rim*.48+(1-smoothstep(.004,.012,min(g.x,g.y)))*.13);')
assert m.connect_material_expressions(uv,'',edge,'UV') and m.connect_material_expressions(o,'',edge,'Veil')
m.connect_material_property(t,'',u.MaterialProperty.MP_EMISSIVE_COLOR);m.connect_material_property(edge,'',u.MaterialProperty.MP_OPACITY);m.recompile_material(mat);save(mat)
path='/Game/Persona/Maps/L_NorthernForest'
if not lib.does_asset_exist(path):levels.new_level(path)
else:levels.load_level(path)
def place(cls,label):
    a=next((a for a in actors.get_all_level_actors() if a.get_actor_label()==label),None)
    if a is None:a=actors.spawn_actor_from_class(cls,u.Vector(0,0,-12600));a.set_actor_label(label)
    a.set_editor_property('tags',[label]);return a
nav=place(u.NavMeshBoundsVolume,'NorthNav');nav.get_component_by_class(u.BrushComponent).set_mobility(u.ComponentMobility.MOVABLE);nav.set_actor_scale3d(u.Vector(108,108,30))
pp=place(u.PostProcessVolume,'NorthExposure');pp.get_component_by_class(u.BrushComponent).set_mobility(u.ComponentMobility.MOVABLE);pp.set_actor_scale3d(u.Vector(130,130,45));pp.set_editor_property('priority',25)
s=pp.get_editor_property('settings')
for k,v in [('auto_exposure_method',u.AutoExposureMethod.AEM_MANUAL),('auto_exposure_bias',0.),('auto_exposure_apply_physical_camera_exposure',False),('indirect_lighting_intensity',0.),('bloom_intensity',.10),('color_saturation',u.Vector4(.82,.85,.83,1))]:
    s.set_editor_property('override_'+k,True);s.set_editor_property(k,v)
pp.set_editor_property('settings',s);levels.save_current_level()
levels.load_level('/Game/Persona/Maps/L_Hub')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
stream=u.GameplayStatics.get_streaming_level(world,path)
if not stream:stream=u.EditorLevelUtils.add_level_to_world(world,path,u.LevelStreamingDynamic)
stream.set_editor_property('initially_loaded',False);stream.set_editor_property('initially_visible',False)
u.EditorLoadingAndSavingUtils.save_packages([world.get_outermost()],False)

