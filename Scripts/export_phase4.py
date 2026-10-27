"""Export evaluated hub parts and FX. The user's .blend is never saved.
Blender --background --disable-autoexec blender/persona_01.blend --python Scripts/export_phase4.py
"""
import bpy, bmesh, json, hashlib, math, re
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion

ROOT=Path(__file__).resolve().parents[1]
HINGES=json.loads((ROOT/'Scripts/hinges_phase5.json').read_text())
OUT=ROOT/'SourceArt'/'Phase4'; OUT.mkdir(parents=True,exist_ok=True)
source=Path(bpy.data.filepath); digest=hashlib.sha256(source.read_bytes()).hexdigest()
SCALE=180/7.298213005065918
GUN_SCALE=json.loads((ROOT/'SourceArt/Items/export_manifest.json').read_text())['centimetres_per_source_unit']
bpy.context.window.scene=bpy.data.scenes['location_experimental_hub']
def enable(layer):
    layer.exclude=False; layer.hide_viewport=False
    for c in layer.children:enable(c)
enable(bpy.context.view_layer.layer_collection)
for n,p in [('HUB_Lift_CTRL','travel'),('TRIAL01_NPC_Gate_ROOT','open')]:
    if n in bpy.data.objects:bpy.data.objects[n][p]=0
bpy.context.scene.unit_settings.system='METRIC'; bpy.context.scene.unit_settings.scale_length=.01
bpy.context.scene.frame_set(bpy.context.scene.frame_current); bpy.context.view_layer.update()
deps=bpy.context.evaluated_depsgraph_get()
collection=bpy.data.collections.new('PERSONA_PHASE4_EXPORT'); bpy.context.scene.collection.children.link(collection)
# The authored service vestibule extends through the northern enclosure wall.
# Cut its playable opening in the export scene only; preserve the source file.
walls=[o for o in bpy.data.objects if o.name.startswith('HUB lower cube wall')]
if walls:
    north=max(walls,key=lambda o:o.matrix_world.translation.y)
    bpy.ops.mesh.primitive_cube_add(size=1,location=(2200/SCALE,north.matrix_world.translation.y,-12050/SCALE))
    cutter=bpy.context.object;cutter.name='PERSONA_EXPORT_GATE_OPENING';cutter.dimensions=(1800/SCALE,650/SCALE,1160/SCALE)
    for owner in list(cutter.users_collection):owner.objects.unlink(cutter)
    collection.objects.link(cutter)
    cut=north.modifiers.new('Game gate opening','BOOLEAN');cut.operation='DIFFERENCE';cut.solver='EXACT';cut.object=cutter
    bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
manifest={'source':str(source),'sha256':digest,'scale':SCALE,'meshes':{},'materials':{},'actors':[],'lights':[],'fx':{},'markers':{}}
def safe(s):return re.sub(r'[^a-zA-Z0-9_]+','_',s).strip('_')
def pos(v,scale=SCALE):return [v.x*scale,-v.y*scale,v.z*scale]
def quat(q):return [-q.x,q.y,-q.z,q.w]
def world(o):
    # Excluded marker objects can have an unevaluated zero matrix_world.
    if o.matrix_world.translation.length<1e-6 and o.location.length>1e-6:
        return world(o.parent) @ o.matrix_parent_inverse @ o.matrix_basis if o.parent else o.matrix_basis.copy()
    return o.evaluated_get(deps).matrix_world.copy()
def export(name,objects,origin=Vector(),scale=SCALE,rotation=None,physics=False,raw=False):
    verts=[]; faces=[]; slots=[]; smooth=[]; materials=[]
    for o in objects:
        if o.type not in {'MESH','CURVE','FONT'}:continue
        e=o if raw else o.evaluated_get(deps)
        try:m=o.data if raw else e.to_mesh()
        except RuntimeError:continue
        if not m:continue
        transform=Matrix.Identity(4) if raw else Matrix.Translation(-origin) @ world(o)
        if rotation is not None:transform=rotation@transform
        transform=Matrix.Scale(scale,4)@transform
        offset=len(verts); verts.extend(transform@v.co for v in m.vertices)
        local=[]
        for em in m.materials:
            mat=bpy.data.materials.get(em.name) if em else None
            if mat not in materials:materials.append(mat)
            local.append(materials.index(mat))
            if mat and mat.name not in manifest['materials']:
                color=list(mat.diffuse_color); emission=0
                if mat.use_nodes:
                    p=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
                    if p:color=list(p.inputs['Base Color'].default_value); emission=p.inputs['Emission Strength'].default_value
                    emn=next((n for n in mat.node_tree.nodes if n.type=='EMISSION'),None)
                    if emn:color=list(emn.inputs['Color'].default_value); emission=emn.inputs['Strength'].default_value
                manifest['materials'][mat.name]={'asset':'M_'+safe(mat.name),'color':color,'emission':float(emission)}
        for p in m.polygons:
            indices=[offset+i for i in p.vertices]
            if transform.determinant()<0:indices.reverse()
            faces.append(indices);slots.append(local[p.material_index] if local else 0);smooth.append(p.use_smooth)
        if not raw:e.to_mesh_clear()
    if not verts:return None
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update()
    for m in materials:
        if m:mesh.materials.append(m)
    for p,s,sm in zip(mesh.polygons,slots,smooth):p.material_index=s;p.use_smooth=sm
    if not name.startswith('SM_FX_'):
        # Some authored solids have inward-facing polygon winding. Two-sided
        # Eevee materials hide this, but Lightmass and Recast require outward
        # surfaces (in particular upward-facing floors). Fix only this copy.
        bm=bmesh.new();bm.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        bm.to_mesh(mesh);bm.free();mesh.update()
    uv=mesh.uv_layers.new(name='UVMap')
    for p in mesh.polygons:
        axis=max(range(3),key=lambda i:abs(p.normal[i])); axes=[i for i in range(3) if i!=axis]
        for loop in p.loop_indices:
            co=mesh.vertices[mesh.loops[loop].vertex_index].co; uv.data[loop].uv=(co[axes[0]]*.01,co[axes[1]]*.01)
    obj=bpy.data.objects.new(name,mesh);collection.objects.link(obj)
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    if name.startswith('SM_Architecture_'):
        # Lightmass cannot separate overlapping planar faces during repacking.
        # Supply independent, packed charts for the merged architectural mesh.
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.004)
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',bake_anim=False,axis_forward='-Y',axis_up='Z',use_mesh_modifiers=False,mesh_smooth_type='FACE')
    manifest['meshes'][name]={'materials':[m.name for m in materials if m],'physics':physics,'vertices':len(verts)}
    bpy.data.objects.remove(obj,do_unlink=True);bpy.data.meshes.remove(mesh)
    return name

hub=bpy.data.collections['location_experimental_hub']; trial=bpy.data.collections['HUB | Test: shooting']; trial_set=set(trial.all_objects)
used=set()
kinds={'HUB_Workbench':'WORKBENCH','HUB_BenchVice':'VICE','HUB_Stove_Oven':'STOVE','HUB_Kitchen_Extractor':'SWITCH','HUB_Kitchen_SinkUnit':'SINK','HUB_Kitchen_WallCabinet':'STORAGE','HUB_Refrigerator':'STORAGE','HUB_BasementTerminal':'TERMINAL','HUB_Lift_CTRL':'LIFT','TRIAL01_NPC_Gate_ROOT':'GATE','TRIAL01_Container_A_ROOT':'STORAGE','TRIAL01_Container_B_ROOT':'STORAGE'}
carry={'HUB_Hammer','HUB_Wrench','HUB_Toolbox','HUB_ChoppingBoard','HUB_CookingPot'}
for i in range(1,5):kinds['HUB_Chest_0'+str(i)]='STORAGE'
for name in list(kinds)+list(carry):
    root=bpy.data.objects.get(name)
    if not root:raise RuntimeError('Missing root '+name)
    descendants=[root]+list(root.children_recursive);used.update(descendants);origin=world(root).translation
    parts=[]; part_objects=set()
    pivots=[o for o in descendants if o.type=='EMPTY' and any(x in o.name for x in ['_Lid','_Door','Gate_Left','Gate_Right'])]
    for p in pivots:
        children=list(p.children_recursive);part_objects.update(children);po=world(p).translation
        if p.name not in HINGES:raise RuntimeError('Missing explicit hinge definition: '+p.name)
        hinge=HINGES[p.name];axis=Vector(hinge['axis'])
        world_axis=world(p).to_quaternion()@axis;angle=math.radians(hinge['degrees'])
        shift=Vector()
        if 'Gate_' in p.name:
            # Retract each complete leaf into its side pocket. The Blender
            # preview's ten-unit travel leaves most of this 18 m gate blocked.
            points=[(world(o) @ Vector(corner)).x for o in children if o.type=='MESH' for corner in o.bound_box]
            travel=(max(points)-min(points))*SCALE+40 if points else 1000
            angle=0;shift=Vector((-travel if 'Left' in p.name else travel,0,0))
        parts.append({'mesh':export('SM_'+safe(p.name),children,po),'offset':pos(po-origin),'closed_rotation':[0,0,0,1],'rotation':quat(Quaternion(world_axis,angle)),'open_offset':list(shift),'hinge':p.name,'signed_degrees':hinge['degrees']})
    if name=='HUB_BenchVice':
        jaw=bpy.data.objects['Vice jaw.001'];part_objects.add(jaw);po=world(jaw).translation
        parts.append({'mesh':export('SM_ViceMovingJaw',[jaw],po),'offset':pos(po-origin),'rotation':[0,0,0,1],'open_offset':[0,-8,0]})
    mesh=export('SM_'+safe(name),[o for o in descendants if o not in part_objects],origin,physics=name in carry)
    manifest['actors'].append({'id':name,'mesh':mesh,'position':pos(origin),'kind':'CARRY' if name in carry else kinds[name],'parts':parts,'trial':root in trial_set})

# Screens use a readable runtime text surface while keeping all authored supports.
for o in list(hub.all_objects):
    if o.type=='MESH' and ('InstructionScreen' in o.name or o.name=='TRIAL01_InstructionScreen'):
        used.add(o);origin=world(o).translation
        manifest['actors'].append({'id':o.name,'mesh':export('SM_'+safe(o.name),[o],origin),'position':pos(origin),'kind':'SCREEN','parts':[],'trial':o in trial_set})
for c in list(hub.children):
    candidates=[o for o in c.all_objects if o not in used and o.type in {'MESH','CURVE','FONT'} and 'atmosphere' not in o.name.lower() and 'MARKER' not in o.name]
    if not candidates:continue
    # One mesh per architectural collection keeps collision openings and reduces draw overhead.
    mesh=export('SM_Architecture_'+safe(c.name),candidates)
    manifest['actors'].append({'id':c.name,'mesh':mesh,'position':[0,0,0],'kind':'STATIC','parts':[],'trial':c==trial})
for o in hub.all_objects:
    if o.type=='LIGHT':
        manifest['lights'].append({'id':o.name,'position':pos(world(o).translation),'rotation':quat(world(o).to_quaternion()),'type':o.data.type,'energy':o.data.energy,'color':list(o.data.color),'size':getattr(o.data,'size',1)*SCALE,'trial':o in trial_set})
    if 'ExitAnchor' in o.name or 'PlayerBlock_MARKER' in o.name:manifest['markers'][o.name]={'position':pos(world(o).translation),'dimensions':[abs(x) for x in pos(o.dimensions)]}

# Weapon parts retain the existing 100 cm gameplay scale and carry frame.
root=bpy.data.objects['Shotgun_ROOT']; allparts=list(root.children_recursive); origin=world(root).translation;rot=Matrix.Rotation(math.pi/2,4,'Z')@world(root).to_quaternion().inverted().to_matrix().to_4x4()
pump=[o for o in allparts if 'Pump' in o.name];bolt=[o for o in allparts if 'Bolt' in o.name]
body=[o for o in allparts if o not in pump+bolt and not o.name.startswith('Shotgun_Shell_') and o.name!='Shotgun_FX']
for suffix,objects in [('Body',body),('Pump',pump),('Bolt',bolt)]:export('SM_Shotgun_'+suffix,objects,origin,GUN_SCALE,rot,physics=suffix=='Body')
# Existing FX are compact geometry sequences, not skeletal animation clips.
for kind,name,count in [('Human','Shotgun_Impact_Human_FX',23),('Wall','Shotgun_Impact_Wall_FX',29),('Muzzle','Shotgun_FX',20)]:
    o=bpy.data.objects[name]; modifier=next(m for m in o.modifiers if m.type=='NODES')
    switch=next(n for n in modifier.node_group.nodes if n.bl_idname=='GeometryNodeIndexSwitch')
    frames=[]
    for i in range(count):
        frame=switch.inputs[i+1].links[0].from_node.inputs['Object'].default_value
        rotation=Matrix.Rotation(math.pi/2,4,'Z') if kind=='Muzzle' else Matrix.Identity(4)
        frames.append(export('SM_FX_'+kind+'_'+str(i).zfill(2),[frame],Vector(),SCALE,rotation,raw=True))
    manifest['fx'][kind]=frames
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
assert hashlib.sha256(source.read_bytes()).hexdigest()==digest,'Source changed during export; repeat with latest source.'
print('PERSONA_PHASE4_EXPORT_OK',len(manifest['meshes']),len(manifest['actors']))
