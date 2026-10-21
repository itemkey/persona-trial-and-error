"""Blender background exporter. Never saves or modifies the source .blend."""
import bpy
import bmesh
import json
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'SourceArt' / 'Persona'
OUT.mkdir(parents=True, exist_ok=True)
source_rig = bpy.data.objects['Armature.007']
source_rig.data.pose_position = 'REST'
bpy.context.view_layer.update()
sources = [bpy.data.objects[n] for n in ('Body.007', 'Head.007')]
positions = [o.matrix_world @ v.co for o in sources for v in o.data.vertices]
low = min(v.z for v in positions)
high = max(v.z for v in positions)
factor = 180.0 / (high - low)
transform = Matrix.Scale(factor, 4) @ Matrix.Translation(Vector((0, 0, -low)))

BONES = {
    'Root': 'root', 'Bone.005': 'pelvis', 'Bone.006': 'spine_01', 'Bone.007': 'spine_02',
    'Bone.011': 'head', 'Bone.008.L': 'clavicle_l', 'Bone.009.L': 'upperarm_l',
    'Bone.010.L': 'lowerarm_l', 'Bone.012.L': 'hand_l', 'Bone.008.R': 'clavicle_r',
    'Bone.009.R': 'upperarm_r', 'Bone.010.R': 'lowerarm_r', 'Bone.012.R': 'hand_r',
    'Bone.001.L': 'thigh_l', 'Bone.002.L': 'calf_l', 'Bone.003.L.001': 'foot_l',
    'Bone.001.R': 'thigh_r', 'Bone.002.R': 'calf_r', 'Bone.003.R.001': 'foot_r',
}
PARENTS = {
    'pelvis': 'root', 'spine_01': 'pelvis', 'spine_02': 'spine_01', 'head': 'spine_02',
    'clavicle_l': 'spine_02', 'upperarm_l': 'clavicle_l', 'lowerarm_l': 'upperarm_l', 'hand_l': 'lowerarm_l',
    'clavicle_r': 'spine_02', 'upperarm_r': 'clavicle_r', 'lowerarm_r': 'upperarm_r', 'hand_r': 'lowerarm_r',
    'thigh_l': 'pelvis', 'calf_l': 'thigh_l', 'foot_l': 'calf_l',
    'thigh_r': 'pelvis', 'calf_r': 'thigh_r', 'foot_r': 'calf_r',
}
collection = bpy.data.collections.new('PERSONA_GAME_EXPORT')
bpy.context.scene.collection.children.link(collection)
rig_data = bpy.data.armatures.new('SK_Persona_Skeleton')
rig = bpy.data.objects.new('SK_Persona_Rig', rig_data)
collection.objects.link(rig)
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='EDIT')
for source_name, target_name in BONES.items():
    src = source_rig.data.bones[source_name]
    dst = rig_data.edit_bones.new(target_name)
    dst.head = transform @ source_rig.matrix_world @ src.head_local
    dst.tail = transform @ source_rig.matrix_world @ src.tail_local
    if target_name == 'root':
        dst.head = (0, 0, 0)
        dst.tail = (0, 0, 10)
    dst.align_roll((transform @ source_rig.matrix_world).to_3x3() @ src.z_axis)
for name, parent in PARENTS.items():
    rig_data.edit_bones[name].parent = rig_data.edit_bones[parent]
bpy.ops.object.mode_set(mode='OBJECT')

mat = bpy.data.materials.new('PERSONA_Export_Matte')
mat.use_nodes = True
bsdf = mat.node_tree.nodes.get('Principled BSDF')
bsdf.inputs['Base Color'].default_value = (0.45, 0.46, 0.48, 1)
bsdf.inputs['Roughness'].default_value = 1
bsdf.inputs['Metallic'].default_value = 0
bsdf.inputs['Specular IOR Level'].default_value = 0

meshes = []
for src in sources:
    data = src.data.copy()
    data.transform(transform @ src.matrix_world, shape_keys=True)
    if data.shape_keys:
        data.shape_keys.animation_data_clear()
        for shape in data.shape_keys.key_blocks:
            shape.value = 0
    obj = bpy.data.objects.new('SK_Persona_' + ('Body' if src.name.startswith('Body') else 'Head'), data)
    collection.objects.link(obj)
    data.materials.clear()
    data.materials.append(mat)
    if src.name.startswith('Head'):
        obj.vertex_groups.new(name='head').add(list(range(len(data.vertices))), 1, 'REPLACE')
    else:
        source_names = {g.index: g.name for g in src.vertex_groups}
        for name in BONES.values():
            obj.vertex_groups.new(name=name)
        unknown = set()
        for vertex in src.data.vertices:
            assigned = 0
            for weight in vertex.groups:
                source_name = source_names[weight.group]
                if source_name in BONES and weight.weight > 0:
                    obj.vertex_groups[BONES[source_name]].add([vertex.index], weight.weight, 'ADD')
                    assigned += weight.weight
                elif weight.weight > 0:
                    unknown.add(source_name)
            if assigned <= 0:
                raise RuntimeError('Unweighted exported vertex: ' + str(vertex.index))
        print('Non-deforming source groups omitted:', sorted(unknown))
    obj.parent = rig
    modifier = obj.modifiers.new('Game skin', 'ARMATURE')
    modifier.object = rig
    meshes.append(obj)

# Keep the original body topology and corrective shapes; first-person arms are a derivative.
arms = meshes[0].copy()
arms.data = meshes[0].data.copy()
arms.name = 'SK_Persona_Arms'
collection.objects.link(arms)
arm_groups = {g.index for g in arms.vertex_groups if g.name.startswith(('clavicle_', 'upperarm_', 'lowerarm_', 'hand_'))}
keep = {v.index for v in arms.data.vertices if sum(g.weight for g in v.groups if g.group in arm_groups) >= 0.5}
bm = bmesh.new()
bm.from_mesh(arms.data)
bm.verts.ensure_lookup_table()
bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in keep], context='VERTS')
bm.to_mesh(arms.data)
bm.free()

bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = 0.01

def export(name, objects):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in [rig] + objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.fbx(
        filepath=str(OUT / name), use_selection=True, object_types={'ARMATURE', 'MESH'},
        apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
        use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
        axis_forward='-Y', axis_up='Z', primary_bone_axis='Y', secondary_bone_axis='X',
        armature_nodetype='NULL', use_armature_deform_only=True,
    )

export('SK_Persona.fbx', meshes)
export('SK_Persona_Arms.fbx', [arms])
manifest = {
    'source': 'blender/persona_01.blend', 'collection': 'PERSONA',
    'source_objects': [s.name for s in sources] + [source_rig.name],
    'height_cm': 180, 'source_height': high-low, 'scale_factor': factor,
    'bone_mapping': BONES, 'parents': PARENTS,
    'body_vertices': len(meshes[0].data.vertices), 'head_vertices': len(meshes[1].data.vertices),
    'arms_vertices': len(arms.data.vertices),
    'corrective_shapes': [k.name for k in meshes[0].data.shape_keys.key_blocks][1:],
    'source_geometry_node_corrections': ['P08_AbdomenBendSmooth', 'P07_LocalJointSmooth'],
    'notes': 'Geometry Nodes are Blender-only. Runtime joint correction must be validated in Unreal; source shape keys are preserved.',
}
(OUT / 'export_manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'PERSONA_GameExport.blend'), copy=True)
print('PERSONA_EXPORT_OK', json.dumps(manifest))
