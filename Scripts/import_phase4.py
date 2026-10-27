"""UE editor Python: import Phase 4 meshes and assemble hub/streamed arena.
Use -ExecutePythonScript (not -run=pythonscript).
"""
import unreal as u, json, shutil, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; DATA=json.loads((ROOT/'SourceArt/Phase4/manifest.json').read_text(encoding='utf-8'))
MAPS_ONLY='-Phase4MapsOnly' in u.SystemLibrary.get_command_line()
GEOMETRY_ONLY='-Phase4GeometryOnly' in u.SystemLibrary.get_command_line()
LIB=u.EditorAssetLibrary; TOOLS=u.AssetToolsHelpers.get_asset_tools(); M=u.MaterialEditingLibrary
LEVEL=u.get_editor_subsystem(u.LevelEditorSubsystem); ACTORS=u.get_editor_subsystem(u.EditorActorSubsystem); MESHES=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
BASE='/Game/Persona'; DEST=BASE+'/Hub'; FX=BASE+'/FX'
def cls(n):return u.load_class(None,'/Script/PersonaTrial.'+n)
def asset(n,p,c,f):return u.load_asset(p+'/'+n) or TOOLS.create_asset(n,p,c,f)
def data_asset(n,p,c):
    f=u.DataAssetFactory();f.set_editor_property('data_asset_class',c);return asset(n,p,c,f)
def save(a):LIB.save_loaded_asset(a)
def safe(s):return re.sub(r'[^a-zA-Z0-9_]+','_',s).strip('_')
def v(p):return u.Vector(*p)
for wav in sorted((ROOT/'SourceArt/Audio').glob('*.wav')):
    if (MAPS_ONLY or GEOMETRY_ONLY) and u.EditorAssetLibrary.does_asset_exist('/Game/Persona/Audio/'+wav.stem):continue
    task=u.AssetImportTask();task.filename=str(wav);task.destination_path=BASE+'/Audio';task.destination_name=wav.stem
    task.automated=True;task.replace_existing=True;task.save=True;TOOLS.import_asset_tasks([task])
    sound=u.load_asset(BASE+'/Audio/'+wav.stem)
    if sound:
        sound.set_editor_property('looping',wav.stem in ['Water','Motor'])
        attenuation=asset('ATT_'+wav.stem,BASE+'/Audio',u.SoundAttenuation,u.SoundAttenuationFactory())
        settings=attenuation.get_editor_property('attenuation');settings.set_editor_property('attenuation_shape_extents',u.Vector(120,0,0));settings.set_editor_property('falloff_distance',9000 if wav.stem=='ShotgunFire' else 1300)
        attenuation.set_editor_property('attenuation',settings);save(attenuation);sound.set_editor_property('attenuation_settings',attenuation);save(sound)
materials={}
for name,record in DATA['materials'].items():
    if (MAPS_ONLY or GEOMETRY_ONLY) and LIB.does_asset_exist(DEST+'/Materials/'+record['asset']):
        materials[name]=u.load_asset(DEST+'/Materials/'+record['asset']);continue
    mat=asset(record['asset'],DEST+'/Materials',u.Material,u.MaterialFactoryNew());M.delete_all_material_expressions(mat)
    color=M.create_material_expression(mat,u.MaterialExpressionConstant3Vector);color.set_editor_property('constant',u.LinearColor(*record['color']))
    M.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
    for value,prop in [(1,u.MaterialProperty.MP_ROUGHNESS),(.03,u.MaterialProperty.MP_SPECULAR)]:
        c=M.create_material_expression(mat,u.MaterialExpressionConstant);c.set_editor_property('r',value);M.connect_material_property(c,'',prop)
    if record['emission']>0:
        e=M.create_material_expression(mat,u.MaterialExpressionConstant);e.set_editor_property('r',min(record['emission'],2.5)*180)
        mult=M.create_material_expression(mat,u.MaterialExpressionMultiply);M.connect_material_expressions(color,'',mult,'A');M.connect_material_expressions(e,'',mult,'B');M.connect_material_property(mult,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        # A faint ink tone preserves readable silhouettes before/between light bakes.
        floor=M.create_material_expression(mat,u.MaterialExpressionConstant3Vector);floor.set_editor_property('constant',u.LinearColor(.003,.0035,.004,1));M.connect_material_property(floor,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    mat.set_editor_property('two_sided',True);M.set_material_usage(mat,u.MaterialUsage.MATUSAGE_STATIC_LIGHTING);M.recompile_material(mat);save(mat);materials[name]=mat
meshes={}
for name,record in DATA['meshes'].items():
    path=FX+'/Meshes' if name.startswith('SM_FX_') else DEST+'/Meshes'
    if GEOMETRY_ONLY and name.startswith('SM_FX_') and LIB.does_asset_exist(path+'/'+name):
        meshes[name]=u.load_asset(path+'/'+name);continue
    if MAPS_ONLY and not name.startswith('SM_Architecture_') and name not in ['SM_HUB_BenchVice','SM_ViceMovingJaw'] and LIB.does_asset_exist(path+'/'+name):
        meshes[name]=u.load_asset(path+'/'+name);continue
    options=u.FbxImportUI();options.import_as_skeletal=False;options.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
    options.automated_import_should_detect_type=False;options.import_materials=False;options.import_textures=False;options.import_animations=False
    d=options.static_mesh_import_data;d.combine_meshes=True;d.auto_generate_collision=False;d.convert_scene=True;d.force_front_x_axis=False
    d.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS;d.generate_lightmap_u_vs=True
    task=u.AssetImportTask();task.filename=str(ROOT/'SourceArt/Phase4'/(name+'.fbx'));task.destination_path=path;task.destination_name=name
    task.automated=True;task.replace_existing=True;task.save=True;task.options=options;task.factory=u.FbxFactory();TOOLS.import_asset_tasks([task])
    mesh=u.load_asset(path+'/'+name)
    if not mesh:raise RuntimeError('Missing mesh '+name)
    for i,mat in enumerate(record['materials']):mesh.set_material(i,materials[mat])
    MESHES.remove_collisions(mesh)
    body=mesh.get_editor_property('body_setup')
    if record['physics']:
        MESHES.add_simple_collisions(mesh,u.ScriptingCollisionShapeType.BOX);body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_DEFAULT)
    elif not name.startswith('SM_FX_'):body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    mesh.set_editor_property('light_map_resolution',1024 if 'Architecture' in name else 128)
    if name.startswith('SM_Architecture_'):
        # The export already packs unique architectural charts in UV0. Reuse
        # them directly; a second repack can collapse very thin wall islands.
        build=MESHES.get_lod_build_settings(mesh,0);build.set_editor_property('generate_lightmap_u_vs',False)
        MESHES.set_lod_build_settings(mesh,0,build);mesh.set_editor_property('light_map_coordinate_index',0)
    save(mesh);meshes[name]=mesh
for kind,names in DATA['fx'].items():
    a=data_asset('DA_'+kind,FX,cls('PersonaFXFrames'));a.set_editor_property('frames',[meshes[n] if n else None for n in names]);save(a)
gun=u.load_asset(BASE+'/Items/DA_Shotgun');gun.set_editor_property('shotgun',True)
gun.set_editor_property('mesh',meshes['SM_Shotgun_Body']);gun.set_editor_property('pump_mesh',meshes['SM_Shotgun_Pump']);gun.set_editor_property('bolt_mesh',meshes['SM_Shotgun_Bolt']);save(gun)
definitions={};items={}
labels={'HUB_Workbench':'Верстак — осмотреть оружие','HUB_BenchVice':'Тиски','HUB_Stove_Oven':'Плита / духовка','HUB_Kitchen_Extractor':'Вытяжка','HUB_Kitchen_SinkUnit':'Раковина','HUB_Kitchen_WallCabinet':'Шкаф','HUB_Refrigerator':'Холодильник','HUB_BasementTerminal':'Терминал Персоны','HUB_Lift_CTRL':'Лифт','HUB_Hammer':'Молоток','HUB_Wrench':'Гаечный ключ','HUB_Toolbox':'Ящик инструментов','HUB_ChoppingBoard':'Разделочная доска','HUB_CookingPot':'Кастрюля'}
for r in DATA['actors']:
    if r['kind']=='STATIC':continue
    if r['kind']=='CARRY':
        a=data_asset('DA_'+safe(r['id']),BASE+'/Items',cls('PersonaItemDefinition'));a.set_editor_property('mesh',meshes[r['mesh']]);a.set_editor_property('display_name',labels.get(r['id'],r['id']));a.set_editor_property('max_stack',1);save(a);items[r['id']]=a;continue
    a=data_asset('DA_'+safe(r['id']),DEST,cls('PersonaHubAsset'));a.set_editor_property('mesh',meshes[r['mesh']]);a.set_editor_property('kind',getattr(u.PersonaHubKind,r['kind']));a.set_editor_property('label',labels.get(r['id'],'Контейнер' if 'Container' in r['id'] else 'Хранилище' if 'Chest' in r['id'] else r['id']))
    parts=[]
    for p in r['parts']:
        part=u.PersonaHubPart();part.set_editor_property('mesh',meshes[p['mesh']]);part.set_editor_property('offset',v(p['offset']));part.set_editor_property('closed_rotation',u.Quat(*p.get('closed_rotation',[0,0,0,1])).rotator());part.set_editor_property('open_rotation',u.Quat(*p['rotation']).rotator());part.set_editor_property('open_offset',v(p['open_offset']));parts.append(part)
    a.set_editor_property('parts',parts);save(a);definitions[r['id']]=a

# Preserve previous map files before the authorized replacement.
backup=ROOT/'.work/phase4-map-backup';backup.mkdir(parents=True,exist_ok=True)
for n in ['L_Hub.umap','L_Hub_BuiltData.uasset']:
    src=ROOT/'Content/Persona/Maps'/n
    if src.exists() and not (backup/n).exists():shutil.copy2(src,backup/n)
def spawn(c,label,position):
    a=ACTORS.spawn_actor_from_class(c,v(position));a.set_actor_label(label);a.set_editor_property('tags',['PersonaPhase4']);return a
for trial,map_name in [(True,'L_Shooting'),(False,'L_Hub')]:
    path=BASE+'/Maps/'+map_name
    if LIB.does_asset_exist(path):
        LEVEL.load_level(path)
        for a in ACTORS.get_all_level_actors():
            if a.get_level().get_outer().get_name()==map_name and not isinstance(a,(u.WorldSettings,u.LevelScriptActor,u.Brush)):ACTORS.destroy_actor(a)
    else:LEVEL.new_level(path)
    world=u.EditorLevelLibrary.get_editor_world();world.get_world_settings().set_editor_property('default_game_mode',cls('PersonaGameMode'))
    for r in DATA['actors']:
        if r['trial']!=trial:continue
        if r['kind']=='STATIC':
            a=spawn(u.StaticMeshActor,r['id'],r['position']);a.static_mesh_component.set_static_mesh(meshes[r['mesh']]);a.static_mesh_component.set_mobility(u.ComponentMobility.STATIC)
        elif r['kind']=='CARRY':
            a=spawn(cls('PersonaProp'),r['id'],r['position']);a.set_editor_property('definition',items[r['id']]);a.mesh.set_static_mesh(meshes[r['mesh']]);a.set_actor_scale3d(u.Vector(1,1,1))
        else:
            a=spawn(cls('PersonaHubObject'),r['id'],r['position']);a.configure(definitions[r['id']]);a.set_editor_property('persistent_id',r['id']);a.set_editor_property('trial_object',trial)
            if r['kind']=='SCREEN':
                display=a.get_editor_property('display');display.set_editor_property('relative_location',u.Vector(0,-12,0) if trial else u.Vector(-12,0,0));display.set_editor_property('relative_rotation',u.Rotator(pitch=0,yaw=-90 if trial else 180,roll=0));display.set_world_size(360);display.set_vertical_alignment(u.VerticalTextAligment.EVRTA_TEXT_CENTER);display.set_text('PERSONA\nSHOOTING')
    for r in DATA['lights']:
        if r['trial']!=trial:continue
        typ=u.RectLight if r['type']=='AREA' else u.PointLight
        a=spawn(typ,r['id'],r['position']);c=a.get_component_by_class(u.LightComponent)
        c.set_mobility(u.ComponentMobility.STATIC)
        # Runtime setters silently reject changes to registered static lights.
        c.set_editor_property('intensity_units',u.LightUnits.LUMENS)
        c.set_editor_property('intensity',max(300,r['energy']*12));c.set_editor_property('light_color',u.LinearColor(*r['color'],1).to_color());c.set_editor_property('cast_shadows',True)
        c.set_editor_property('cast_dynamic_shadows',False)
        c.set_editor_property('attenuation_radius',max(1500,min(15000,r['size']*8)))
        if r['type']=='AREA':
            c.set_editor_property('source_width',r['size']);c.set_editor_property('source_height',r['size']);normal=u.Quat(*r['rotation']).rotate_vector(u.Vector(0,0,-1));a.set_actor_rotation(u.MathLibrary.make_rot_from_x(normal),False)
            # Blender's emitter sits behind the luminaire housing/diffuser.
            # Lightmass treats that opaque geometry as a blocker, so place the
            # light on the emitting face while retaining its direction.
            a.set_actor_location(v(r['position'])+normal*20,False,False)
    nav=spawn(u.NavMeshBoundsVolume,'Navigation arena' if trial else 'Navigation hub',(0,0,-12400 if trial else 500));nav.set_actor_scale3d(u.Vector(65,75,12 if trial else 15))
    importance=spawn(u.LightmassImportanceVolume,'Playable lighting bounds',(0,0,-12100 if trial else 500));importance.set_actor_scale3d(u.Vector(65,75,8 if trial else 15))
    if trial:
        marker=DATA['markers']['TRIAL01_NPC_ONLY_PlayerBlock_MARKER'];barrier=spawn(cls('PersonaPlayerBarrier'),'NPC service area barrier',marker['position']);barrier.get_editor_property('barrier').set_box_extent(v([x/2 for x in marker['dimensions']]))
    if not trial:
        for i in range(4):spawn(u.PlayerStart,'Subject arrival '+str(i),(-180+i*120,1100,100)).set_actor_rotation(u.Rotator(pitch=0,yaw=-90,roll=0),False)
        pp=spawn(u.PostProcessVolume,'Graphic atmosphere',(0,0,0));pp.set_editor_property('unbound',True);s=pp.get_editor_property('settings')
        for n,value in {'auto_exposure_method':u.AutoExposureMethod.AEM_MANUAL,'auto_exposure_bias':-7.5,'auto_exposure_apply_physical_camera_exposure':False,'motion_blur_amount':0.0,'bloom_intensity':.25,'color_saturation':u.Vector4(.1,.1,.1,1)}.items():s.set_editor_property('override_'+n,True);s.set_editor_property(n,value)
        blend=u.WeightedBlendable();blend.set_editor_property('object',u.load_asset(BASE+'/Materials/M_GraphicAtmosphere'));blend.set_editor_property('weight',1);weighted=u.WeightedBlendables();weighted.set_editor_property('array',[blend]);s.set_editor_property('weighted_blendables',weighted);pp.set_editor_property('settings',s)
        fog=spawn(u.ExponentialHeightFog,'Depth atmosphere',(0,0,-100));fog.component.set_editor_property('fog_density',.002);fog.component.set_editor_property('fog_height_falloff',.12);fog.component.set_editor_property('fog_inscattering_luminance',u.LinearColor(.06,.065,.07))
        definition=data_asset('DA_Shooting',BASE+'/Trials',cls('PersonaTrialDefinition'));definition.set_editor_property('display_name','Shooting');definition.set_editor_property('map',u.load_asset(BASE+'/Maps/L_Shooting'));save(definition)
        director=spawn(cls('PersonaTrialDirector'),'Shooting director',(0,0,0));director.set_editor_property('definition',definition)
        director.set_editor_property('npc_spawns',[v(r['position']) for n,r in DATA['markers'].items() if 'ExitAnchor' in n])
    u.PersonaAssetTools.build_navigation(world);LEVEL.save_current_level()
LIB.save_directory(BASE,only_if_is_dirty=True,recursive=True)
stream=u.GameplayStatics.get_streaming_level(u.EditorLevelLibrary.get_editor_world(),BASE+'/Maps/L_Shooting')
if not stream:stream=u.EditorLevelUtils.add_level_to_world(u.EditorLevelLibrary.get_editor_world(),BASE+'/Maps/L_Shooting',u.LevelStreamingDynamic)
if stream:
    stream.set_editor_property('initially_loaded',False);stream.set_editor_property('initially_visible',False)
    LEVEL.save_all_dirty_levels()
exec(compile((ROOT/'Scripts/configure_overhaul.py').read_text(encoding='utf-8'),str(ROOT/'Scripts/configure_overhaul.py'),'exec'))
u.log('PERSONA_PHASE4_IMPORT_OK')
