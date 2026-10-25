"""Blender background: export derived meshes without saving the source .blend.
Blender --background --disable-autoexec blender/persona_01.blend --python Scripts/export_items.py
"""
import bpy, json, hashlib, re, math
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'SourceArt'/'Items'
OUT.mkdir(parents=True, exist_ok=True)
source = Path(bpy.data.filepath)
before = hashlib.sha256(source.read_bytes()).hexdigest()
deps = bpy.context.evaluated_depsgraph_get()
gun = bpy.data.objects['Shotgun_ROOT']
points = [gun.matrix_world.inverted() @ o.matrix_world @ Vector(p)
          for o in gun.children_recursive if o.type == 'MESH' for p in o.bound_box]
scale = 100.0/(max(p.y for p in points)-min(p.y for p in points))
manifest = {'source':str(source.relative_to(ROOT)), 'source_sha256':before,
            'centimetres_per_source_unit':scale, 'items':{}, 'materials':{}}
collection = bpy.data.collections.new('PERSONA_ITEM_EXPORT')
bpy.context.scene.collection.children.link(collection)
bpy.context.scene.unit_settings.system='METRIC'
bpy.context.scene.unit_settings.scale_length=.01
exports=[]
for root_name, name in [('Shotgun_ROOT','SM_Shotgun'),('Ammo12GA_ROOT','SM_Ammo12GA')]:
    root=bpy.data.objects[root_name]
    verts=[]; faces=[]; face_materials=[]; face_smooth=[]; materials=[]
    for obj in [root]+list(root.children_recursive):
        if obj.type!='MESH': continue
        evaluated=obj.evaluated_get(deps); mesh=evaluated.to_mesh()
        transform=Matrix.Rotation(math.pi/2,4,'Z') @ Matrix.Scale(scale,4) @ root.matrix_world.inverted() @ obj.matrix_world
        offset=len(verts)
        verts.extend(transform @ v.co for v in mesh.vertices)
        slots=[]
        for evaluated_material in mesh.materials:
            mat = bpy.data.materials.get(evaluated_material.name) if evaluated_material else None
            if mat not in materials: materials.append(mat)
            slots.append(materials.index(mat))
            if mat and mat.name not in manifest['materials']:
                color=list(mat.diffuse_color)
                if mat.use_nodes:
                    principled=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
                    if principled: color=list(principled.inputs['Base Color'].default_value)
                manifest['materials'][mat.name]={'color':color,'asset':'M_Item_'+re.sub(r'[^a-zA-Z0-9]+','_',mat.name)}
        for poly in mesh.polygons:
            faces.append([offset+i for i in poly.vertices])
            face_materials.append(slots[poly.material_index] if slots else 0)
            face_smooth.append(poly.use_smooth)
        evaluated.to_mesh_clear()
    # Ammo origin at box centre; shotgun origin retains authored grip reference.
    if name=='SM_Ammo12GA':
        center=Vector([(max(p[i] for p in verts)+min(p[i] for p in verts))/2 for i in range(3)])
        verts=[p-center for p in verts]
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(verts,[],faces); mesh.update()
    for mat in materials: mesh.materials.append(mat)
    for poly,index,smooth in zip(mesh.polygons,face_materials,face_smooth): poly.material_index=index; poly.use_smooth=smooth
    uv=mesh.uv_layers.new(name='UVMap')
    for poly in mesh.polygons:
        axis=max(range(3),key=lambda i:abs(poly.normal[i]))
        axes=[i for i in range(3) if i!=axis]
        for loop in poly.loop_indices:
            co=mesh.vertices[mesh.loops[loop].vertex_index].co
            uv.data[loop].uv=(co[axes[0]]*.01,co[axes[1]]*.01)
    obj=bpy.data.objects.new(name,mesh); collection.objects.link(obj); exports.append(obj)
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,object_types={'MESH'},
        apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',bake_anim=False,
        axis_forward='-Y',axis_up='Z',use_mesh_modifiers=False,mesh_smooth_type='FACE')
    manifest['items'][name]={'root':root_name,'vertices':len(verts),'faces':len(faces),
        'materials':[m.name for m in materials], 'bounds_blender_cm':[[min(p[i] for p in verts),max(p[i] for p in verts)] for i in range(3)]}
(OUT/'export_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
# Derivative contains only the exports; the original file is never written.
for obj in list(bpy.data.objects):
    if obj not in exports: bpy.data.objects.remove(obj,do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'PERSONA_ItemsExport.blend'),copy=True)
assert before==hashlib.sha256(source.read_bytes()).hexdigest()
print('PERSONA_ITEMS_EXPORT_OK',before)
