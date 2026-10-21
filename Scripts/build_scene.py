"""Create the owned foundation maps/materials. Existing maps are kept on reruns."""
import unreal as u
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary
M = u.MaterialEditingLibrary
LEVEL = u.get_editor_subsystem(u.LevelEditorSubsystem)
ACTORS = u.get_editor_subsystem(u.EditorActorSubsystem)
BASE = '/Game/Persona'

def create(name, path, cls, factory):
    return u.load_asset(path+'/'+name) or TOOLS.create_asset(name,path,cls,factory)

def const(material,value):
    node=M.create_material_expression(material,u.MaterialExpressionConstant)
    node.set_editor_property('r',value)
    return node

def surface(name, tone, emissive=0):
    path=BASE+'/Materials/'+name
    old=u.load_asset(path)
    if old:return old
    mat=create(name,BASE+'/Materials',u.Material,u.MaterialFactoryNew())
    color=M.create_material_expression(mat,u.MaterialExpressionVectorParameter)
    color.set_editor_property('parameter_name','BaseTone')
    color.set_editor_property('default_value',u.LinearColor(*tone,1))
    M.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
    M.connect_material_property(const(mat,1),'',u.MaterialProperty.MP_ROUGHNESS)
    M.connect_material_property(const(mat,0),'',u.MaterialProperty.MP_SPECULAR)
    M.connect_material_property(const(mat,0),'',u.MaterialProperty.MP_METALLIC)
    if emissive:
        mult=M.create_material_expression(mat,u.MaterialExpressionMultiply)
        M.connect_material_expressions(color,'',mult,'A')
        M.connect_material_expressions(const(mat,emissive),'',mult,'B')
        M.connect_material_property(mult,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    M.recompile_material(mat);LIB.save_loaded_asset(mat)
    return mat

mat_dark=surface('M_Charcoal',(.045,.048,.052))
mat_gray=surface('M_Concrete',(.22,.23,.25))
mat_pale=surface('M_Paper',(.55,.56,.58))
mat_black=surface('M_Void',(.007,.008,.01))
mat_white=surface('M_WhiteLight',(.92,.96,1),8)
mat_persona=surface('M_Persona',(.52,.54,.56))
mat_npc=surface('M_Observer',(.018,.020,.023))

def post_material():
    path=BASE+'/Materials/M_GraphicAtmosphere'
    if LIB.does_asset_exist(path):return u.load_asset(path)
    mat=create('M_GraphicAtmosphere',BASE+'/Materials',u.Material,u.MaterialFactoryNew())
    mat.set_editor_property('material_domain',u.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property('blendable_location',u.BlendableLocation.BL_SCENE_COLOR_BEFORE_BLOOM)
    custom=M.create_material_expression(mat,u.MaterialExpressionCustom)
    custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    nodes=[]
    for name,kind in [('Color',u.SceneTextureId.PPI_POST_PROCESS_INPUT0),('Depth',u.SceneTextureId.PPI_SCENE_DEPTH),('Stencil',u.SceneTextureId.PPI_CUSTOM_STENCIL),('CustomDepth',u.SceneTextureId.PPI_CUSTOM_DEPTH)]:
        node=M.create_material_expression(mat,u.MaterialExpressionSceneTexture)
        node.set_editor_property('scene_texture_id',kind)
        inp=u.CustomInput();inp.set_editor_property('input_name',name);nodes.append((inp,node))
    custom.set_editor_property('inputs',[x[0] for x in nodes])
    for inp,node in nodes:M.connect_material_expressions(node,'Color',custom,str(inp.get_editor_property('input_name')))
    custom.set_editor_property('code',r'''
float3 c=Color.rgb;
float d=Depth.r;
float visible=step(CustomDepth.r,d+3.0);
float mask=step(0.5,Stencil.r)*visible;
float fade=1.0-smoothstep(1200.0,4500.0,d);
float l=max(dot(c,float3(0.2126,0.7152,0.0722)),0.0001);
float bands=pow(floor(sqrt(saturate(l))*3.0+0.5)/3.0,2.0);
float3 result=lerp(c,c*(bands/l),0.32*mask*fade*(1-smoothstep(0.9,1.8,l)));
float2 uv=GetDefaultSceneTextureUV(Parameters,PPI_CustomStencil);
float2 px=View.ViewSizeAndInvSize.zw;
float edge=0;
float2 offsets[4]={float2(1,0),float2(-1,0),float2(0,1),float2(0,-1)};
for(int i=0;i<4;i++){
 float s=SceneTextureLookup(uv+offsets[i]*px,PPI_CustomStencil,false).r;
 edge=max(edge,step(0.5,abs(s-Stencil.r)));
}
return lerp(result,result*0.15,edge*mask*fade*0.85);
''')
    M.connect_material_property(custom,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    M.recompile_material(mat); LIB.save_loaded_asset(mat)
    return mat
graphic=post_material()

cube=u.load_asset('/Engine/BasicShapes/Cube')
def spawn(cls, name, loc, rot=(0,0,0)):
    actor=ACTORS.spawn_actor_from_class(cls,u.Vector(*loc),u.Rotator(*rot))
    actor.set_actor_label(name)
    actor.set_editor_property('tags',['PersonaGenerated'])
    return actor

def box(name,loc,size,material=mat_gray,stencil=0):
    a=spawn(u.StaticMeshActor,name,loc)
    c=a.static_mesh_component;c.set_static_mesh(cube);c.set_material(0,material)
    a.set_actor_scale3d(u.Vector(*(v/100 for v in size)))
    c.set_mobility(u.ComponentMobility.STATIC)
    c.set_editor_property('override_light_map_res',True)
    c.set_editor_property('overridden_light_map_res',128 if max(size)<3000 else 256)
    if stencil:c.set_render_custom_depth(True);c.set_custom_depth_stencil_value(stencil)
    return a

def light(name,loc,rot,intensity,width,height):
    a=spawn(u.RectLight,name,loc,rot)
    c=a.get_component_by_class(u.RectLightComponent)
    c.set_mobility(u.ComponentMobility.STATIONARY)
    c.set_intensity(intensity)
    c.set_light_color(u.LinearColor(.88,.93,1))
    c.set_editor_property('source_width',width);c.set_editor_property('source_height',height)
    c.set_editor_property('attenuation_radius',5000)
    c.set_editor_property('indirect_lighting_intensity',.3)
    return a

def text(name,label,loc,size=30):
    a=spawn(u.TextRenderActor,name,loc,(0,180,0))
    c=a.get_component_by_class(u.TextRenderComponent)
    c.set_text(label);c.set_world_size(size);c.set_text_render_color(u.Color(170,178,185))
    return a

def atmosphere():
    fog=spawn(u.ExponentialHeightFog,'Atmosphere - smooth distance layers',(0,0,-500))
    c=fog.get_component_by_class(u.ExponentialHeightFogComponent)
    c.set_editor_property('fog_density',.018)
    c.set_editor_property('fog_height_falloff',.04)
    c.set_editor_property('fog_max_opacity',.91)
    c.set_editor_property('start_distance',500)
    c.set_editor_property('fog_inscattering_luminance',u.LinearColor(.18,.19,.21))
    c.set_editor_property('enable_volumetric_fog',False)
    pp=spawn(u.PostProcessVolume,'Graphic atmosphere',(0,0,0));pp.set_editor_property('unbound',True)
    s=pp.get_editor_property('settings')
    settings={
      'auto_exposure_method':u.AutoExposureMethod.AEM_MANUAL,
      'auto_exposure_bias':0.0,'auto_exposure_apply_physical_camera_exposure':False,
      'bloom_intensity':.35,'motion_blur_amount':0,'vignette_intensity':.12,
      'color_saturation':u.Vector4(.1,.1,.1,1),
      'dynamic_global_illumination_method':u.DynamicGlobalIlluminationMethod.NONE,
      'reflection_method':u.ReflectionMethod.NONE,
    }
    for name,value in settings.items():
        s.set_editor_property('override_'+name,True);s.set_editor_property(name,value)
    blend=u.WeightedBlendable();blend.set_editor_property('object',graphic);blend.set_editor_property('weight',1)
    weighted=u.WeightedBlendables();weighted.set_editor_property('array',[blend]);s.set_editor_property('weighted_blendables',weighted)
    pp.set_editor_property('settings',s)

def navigation(extent,center):
    nav=spawn(u.NavMeshBoundsVolume,'Navigation - play area',center)
    # The default brush is populated by the editor spawn factory.
    nav.set_actor_scale3d(u.Vector(*(x/200 for x in extent)))

def make_map(trial):
    name='L_TestChamber' if trial else 'L_Hub'
    path=BASE+'/Maps/'+name
    if LIB.does_asset_exist(path):
        existing=u.load_asset(path)
        if LIB.get_metadata_tag(existing,'PersonaGeneratorComplete')=='1':
            print('PERSONA_MAP_EXISTS',path);return
        LEVEL.load_level(path)
        for a in ACTORS.get_all_level_actors():
            if 'PersonaGenerated' in [str(t) for t in a.tags]:ACTORS.destroy_actor(a)
    elif not LEVEL.new_level(path):raise RuntimeError('Cannot create map '+path)
    atmosphere()
    if trial:
        box('Chamber foundation',(1600,0,-60),(9000,6000,120),mat_dark)
        box('West wall',(-2800,0,1400),(120,6000,2800),mat_black)
        box('North mass',(1800,-2900,1900),(9000,220,3800),mat_gray)
        box('South mass',(1800,2900,1900),(9000,220,3800),mat_dark)
        box('Entry left',(-800,-620,450),(3000,180,900),mat_gray,3)
        box('Entry right',(-800,620,450),(3000,180,900),mat_gray,3)
        box('Entry ceiling',(-800,0,920),(3000,1400,120),mat_dark)
        box('Light well',(3400,0,1800),(90,1300,2800),mat_white)
        light('Light well illumination',(3300,0,1500),(0,180,0),17000,1300,2400)
        light('Corridor fluorescent',(-900,0,820),(-90,0,0),3000,60,1100)
        box('Corridor luminous strip',(-900,0,850),(1100,35,15),mat_white)
        for i,x in enumerate([500,1700,2900]):
            box('Monolith %d'%i,(x,-1700,1800),(300,700,3600),mat_dark)
            box('Low cover %d'%i,(x,950,90),(450,180,180),mat_gray,3)
        text('Sector label','T R I A L  /  0 1',(-2350,500,300),45)
        start=(-2200,0,105)
        navigation((8800,5600,1000),(1500,0,350))
    else:
        box('Hub foundation',(0,0,-60),(5000,3600,120),mat_dark)
        box('Hub back wall',(-2450,0,900),(160,3600,1800),mat_gray)
        box('Hub north',(0,-1750,1400),(5000,160,2800),mat_dark)
        box('Hub south',(0,1750,1400),(5000,160,2800),mat_gray)
        box('Hub ceiling',(-500,0,1850),(4000,3600,120),mat_black)
        box('White portal',(2200,0,900),(60,600,1700),mat_white)
        box('Portal left mass',(2300,-1100,2000),(300,1500,4000),mat_black)
        box('Portal right mass',(2300,1100,2000),(300,1500,4000),mat_black)
        light('Portal light',(2100,0,900),(0,180,0),9000,500,1500)
        light('Waiting area lamp',(-1400,0,1200),(-90,0,0),4500,80,600)
        box('Fluorescent panel',(-1400,0,1250),(650,40,20),mat_white)
        for y in [-900,900]:box('Bench',(0,y,35),(700,70,70),mat_pale,3)
        text('Persona title','P E R S O N A',(-2280,600,340),55)
        text('Hub subtitle','ASSEMBLY / 01',(-2280,600,250),22)
        start=(-1600,0,105)
        navigation((4700,3300,900),(0,0,350))
    for i in range(4):spawn(u.PlayerStart,'Player start %d'%(i+1),(start[0],start[1]+(i-1.5)*110,start[2]))
    # Distant slabs give the chamber an implied extent without a large playable world.
    for i,(x,y,h,w) in enumerate([(8500,-2600,9000,1500),(11500,2000,16000,2500),(15000,-5000,22000,3000),(18000,6500,26000,3500)]):
        box('Distant tower %d'%i,(x,y,h/2),(w,w,h),mat_gray)
    for i in range(5):
        box('Speaker %d'%i,(-1800+i*850,-1550,550),(40,70,110),mat_black)
    LIB.set_metadata_tag(u.EditorLevelLibrary.get_editor_world(),'PersonaGeneratorComplete','1')
    LEVEL.save_current_level()
    print('PERSONA_MAP_OK',path)

make_map(False)
make_map(True)
if not LIB.does_asset_exist(BASE+'/Maps/L_Transition'):
    LEVEL.new_level(BASE+'/Maps/L_Transition');LEVEL.save_current_level()
for mesh_name in ['SK_Persona','SK_Persona_Arms']:
    mesh=u.load_asset(BASE+'/Characters/'+mesh_name)
    if mesh:
        materials=mesh.get_editor_property('materials')
        for i in range(len(materials)):
            slot=materials[i]
            slot.set_editor_property('material_interface',mat_persona)
            materials[i]=slot  # Unreal struct-array elements are returned by value.
        mesh.set_editor_property('materials',materials);LIB.save_loaded_asset(mesh)
LIB.save_directory(BASE,only_if_is_dirty=False,recursive=True)
print('PERSONA_SCENES_OK')
