"""Import the field kit and author the streamed forest arena. Run export_survival.py first."""
import json
import random
from pathlib import Path
import unreal as u

base = '/Game/Persona/Survival'
map_path = '/Game/Persona/Maps/L_SurvivalAgility'
source = Path(__file__).resolve().parents[1] / '.work/SurvivalArt'
kit = json.loads((source/'kit.json').read_text())
tools = u.AssetToolsHelpers.get_asset_tools()
lib = u.EditorAssetLibrary
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
materials = u.MaterialEditingLibrary

def asset(name, directory, cls, factory):
    path = directory+'/'+name
    return lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name,directory,cls,factory)

def save(obj):
    if not lib.save_loaded_asset(obj,only_if_is_dirty=False):
        raise RuntimeError('Could not save '+obj.get_path_name())

def data(name, directory, cls_name):
    cls=u.load_class(None,'/Script/PersonaTrial.'+cls_name)
    factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',cls)
    return asset(name,directory,cls,factory)

mats={}
for name,rgb in kit['colors'].items():
    mat=asset('M_'+name,base,u.Material,u.MaterialFactoryNew())
    materials.delete_all_material_expressions(mat)
    color=materials.create_material_expression(mat,u.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant',u.LinearColor(*rgb,1))
    materials.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
    ambient=materials.create_material_expression(mat,u.MaterialExpressionMultiply)
    ambient.set_editor_property('const_b',.45)
    materials.connect_material_expressions(color,'',ambient,'A')
    materials.connect_material_property(ambient,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    mat.set_editor_property('two_sided',True)
    materials.recompile_material(mat); save(mat); mats[name]=mat

# A softly lit grid gives all six faces scale and depth even at a standstill.
mat=asset('M_CubeFace',base,u.Material,u.MaterialFactoryNew())
materials.delete_all_material_expressions(mat)
mat.set_editor_property('two_sided',True)
mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
tint=materials.create_material_expression(mat,u.MaterialExpressionVectorParameter)
tint.set_editor_property('parameter_name','Tint'); tint.set_editor_property('default_value',u.LinearColor(.13,.27,.28,1))
uv=materials.create_material_expression(mat,u.MaterialExpressionTextureCoordinate)
grid=materials.create_material_expression(mat,u.MaterialExpressionCustom)
grid.set_editor_property('code','float2 p=abs(frac(UV*24.0+0.5)-0.5); float edge=1-smoothstep(.008,.035,min(p.x,p.y)); return Tint*(.48+.38*UV.y+.8*edge);')
grid.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
inputs=[]
for name in ('UV','Tint'):
    inp=u.CustomInput(); inp.set_editor_property('input_name',name); inputs.append(inp)
grid.set_editor_property('inputs',inputs)
materials.connect_material_expressions(uv,'',grid,'UV'); materials.connect_material_expressions(tint,'',grid,'Tint')
materials.connect_material_property(grid,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
materials.recompile_material(mat); save(mat)

meshes={}
for name,slots in kit['meshes'].items():
    opts=u.FbxImportUI(); opts.automated_import_should_detect_type=False
    opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
    opts.import_materials=False; opts.import_textures=False; opts.import_animations=False
    opts.static_mesh_import_data.combine_meshes=True
    opts.static_mesh_import_data.auto_generate_collision=True
    task=u.AssetImportTask(); task.filename=str(source/(name+'.fbx')); task.destination_path=base
    task.destination_name='SM_'+name; task.automated=True; task.replace_existing=True
    task.options=opts; task.factory=u.FbxFactory(); task.save=True
    if not lib.does_asset_exist(base+'/SM_'+name):
        tools.import_asset_tasks([task])
    mesh=lib.load_asset(base+'/SM_'+name)
    for index,slot in enumerate(mesh.get_editor_property('static_materials')):
        material_name=str(slot.get_editor_property('imported_material_slot_name'))
        mesh.set_material(index,mats.get(material_name,mats[slots[index]]))
    if name in ('ForestFloor','Pine0','Pine1','Pine2','Bridge','Shelter','Cliff'):
        mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    save(mesh); meshes[name]=mesh

for name,label in [('Ration','Еда'),('Flask','Питьевая вода'),('Scrap','Металл'),('Cloth','Ткань')]:
    item=data('DA_'+name,'/Game/Persona/Items','PersonaItemDefinition')
    item.set_editor_property('display_name',label); item.set_editor_property('max_stack',8)
    item.set_editor_property('mesh',meshes[name]); save(item)

if lib.does_asset_exist(map_path):
    levels.load_level(map_path)
    for actor in actors.get_all_level_actors():
        if 'SurvivalForest' in [str(t) for t in actor.get_editor_property('tags')]:
            actors.destroy_actor(actor)
elif not levels.new_level(map_path):
    raise RuntimeError('Could not create forest map')
base_z=-12600
rng=random.Random(78118)
def spawn(cls,label,x,y,z):
    actor=actors.spawn_actor_from_class(cls,u.Vector(x,y,base_z+z))
    actor.set_actor_label(label); actor.set_editor_property('tags',['SurvivalForest'])
    return actor

def shape(name,x,y,z,scale=1,yaw=0):
    actor=spawn(u.StaticMeshActor,name,x,y,z)
    actor.static_mesh_component.set_static_mesh(meshes[name])
    actor.set_actor_rotation(u.Rotator(yaw=yaw),False)
    actor.set_actor_scale3d(u.Vector(*(scale if isinstance(scale,tuple) else (scale,)*3)))
    return actor

# A continuous 380 m forest. The missing column is a gorge, with three safe crossings.
camps=[(850,400),(-4800,3600),(8400,4600),(-7200,-5200),(10900,-6700),(1800,9200),(-9000,9600)]
for ix in range(-9,10):
    for iy in range(-9,10):
        x,y=2000*ix,2000*iy
        if ix==2:
            shape('ForestFloor',x,y,-3010)
            river=spawn(u.StaticMeshActor,'Creek',x,y,-2980)
            river.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
            river.static_mesh_component.set_material(0,mats['water'])
            river.set_actor_scale3d(u.Vector(19.5,20,.1))
            for side in (-1,1): shape('Cliff',x+side*996,y,0,yaw=90)
            if iy in (-3,0,3): shape('Bridge',x,y,2)
            continue
        shape('ForestFloor',x,y,0,yaw=rng.randrange(4)*90)
        for _ in range(3):
            tx,ty=x+rng.uniform(-860,860),y+rng.uniform(-860,860)
            if min((tx-a)**2+(ty-b)**2 for a,b in [(0,0)]+camps)<650**2: continue
            shape('Pine'+str(rng.randrange(3)),tx,ty,-12,rng.uniform(.75,1.35),rng.uniform(0,360))
        if rng.random()<.55 and min((x-a)**2+(y-b)**2 for a,b in [(0,0)]+camps)>900**2:
            shape('Boulder',x+330,y+260,-8,rng.uniform(.6,2.1),rng.uniform(0,360))
# No terrain in the lift shaft. The platform meets the clearing at grade.
for actor in actors.get_all_level_actors():
    if actor.get_actor_label().startswith('ForestFloor') and abs(actor.get_actor_location().x)+abs(actor.get_actor_location().y)<5:
        actors.destroy_actor(actor)
for x,y,sx,sy in [(-660,0,.34,1),(660,0,.34,1),(0,-660,.32,.34),(0,660,.32,.34)]:
    shape('ForestFloor',x,y,0,(sx,sy,1))
shape('ForestFloor',0,0,-18,(.325,.325,1))
for i,(x,y) in enumerate(camps):
    shape('Shelter',x,y,0)
    chest=spawn(u.load_class(None,'/Script/PersonaTrial.PersonaHubObject'),'Field cache '+str(i),x+280,y,5)
    chest.configure(lib.load_asset('/Game/Persona/Hub/DA_HUB_Chest_01'))
    chest.set_editor_property('persistent_id','ForestCamp'+str(i)); chest.set_editor_property('trial_object',True)

# Bound exposure and lighting to the underground forest volume so the hub keeps its lighting.
pp=spawn(u.PostProcessVolume,'Forest exposure',0,0,0)
pp.set_actor_scale3d(u.Vector(260,260,46)); pp.set_editor_property('priority',20)
settings=pp.get_editor_property('settings')
for k,v in [('auto_exposure_method',u.AutoExposureMethod.AEM_MANUAL),('auto_exposure_bias',0.0),
            ('auto_exposure_apply_physical_camera_exposure',False),('indirect_lighting_intensity',0.0),('color_saturation',u.Vector4(.86,.9,.9,1))]:
    settings.set_editor_property('override_'+k,True); settings.set_editor_property(k,v)
pp.set_editor_property('settings',settings)
for x in range(-18000,18001,6000):
    for y in range(-18000,18001,6000):
        lamp=spawn(u.PointLight,'Canopy light',x,y,1900)
        light=lamp.point_light_component; light.set_mobility(u.ComponentMobility.MOVABLE)
        light.set_editor_property('intensity',1400); light.set_editor_property('attenuation_radius',5300)
        light.set_editor_property('cast_shadows',False)
nav=spawn(u.NavMeshBoundsVolume,'Forest navigation',0,0,-600)
nav.set_actor_scale3d(u.Vector(195,195,26))
levels.save_current_level()
trial=data('DA_SurvivalAgility','/Game/Persona/Trials','PersonaTrialDefinition')
trial.set_editor_property('trial_id','SurvivalAgility'); trial.set_editor_property('display_name','Выживание-проворность')
trial.set_editor_property('map',lib.load_asset(map_path)); save(trial)
levels.load_level('/Game/Persona/Maps/L_Hub')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
stream=u.GameplayStatics.get_streaming_level(world,map_path)
if not stream:
    stream=u.EditorLevelUtils.add_level_to_world(world,map_path,u.LevelStreamingDynamic)
stream.set_editor_property('initially_loaded',False)
stream.set_editor_property('initially_visible',False)
director=next(a for a in actors.get_all_level_actors() if a.get_class().get_name()=='PersonaTrialDirector')
director.set_editor_property('trials',[lib.load_asset('/Game/Persona/Trials/DA_Shooting'),trial])
u.EditorLoadingAndSavingUtils.save_packages([world.get_outermost()],False)
u.log('PERSONA_SURVIVAL_ASSETS_OK')
