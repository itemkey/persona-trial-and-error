"""Update hinge metadata from Blender without re-exporting unchanged geometry."""
import bpy,json,math,hashlib
from pathlib import Path
from mathutils import Vector,Quaternion

ROOT=Path(bpy.data.filepath).resolve().parents[1]
path=ROOT/'SourceArt/Phase4/manifest.json'
data=json.loads(path.read_text(encoding='utf-8'))
source=Path(bpy.data.filepath)
digest=hashlib.sha256(source.read_bytes()).hexdigest()
if digest!=data['sha256']:raise RuntimeError('Source geometry changed: run export_phase4.py instead')
hinges=json.loads((ROOT/'Scripts/hinges_phase5.json').read_text())
checked=[]
for actor in data['actors']:
    for part in actor.get('parts',[]):
        name=part['mesh'].removeprefix('SM_')
        if name not in hinges:continue
        obj=bpy.data.objects[name];spec=hinges[name]
        axis=obj.matrix_world.to_quaternion()@Vector(spec['axis'])
        q=Quaternion(axis,math.radians(spec['degrees']))
        part.update(closed_rotation=[0,0,0,1],rotation=[-q.x,q.y,-q.z,q.w],hinge=name,signed_degrees=spec['degrees'])
        points=[child.matrix_world@Vector(corner) for child in obj.children_recursive if child.type=='MESH' for corner in child.bound_box]
        if points and spec['degrees']:
            center=sum(points,Vector())/len(points);pivot=obj.matrix_world.translation
            moved=pivot+q@(center-pivot)
            if 'Chest' in name:assert moved.z>center.z,name
            elif name=='HUB_Oven_Door':assert moved.z<center.z and moved.y<center.y,name
            elif 'Refrigerator' in name:assert moved.x<center.x,name
            elif 'Cabinet' in name or 'SinkUnit' in name:assert moved.y<center.y,name
            elif 'Container' in name:
                outward=obj.matrix_world.to_quaternion()@Vector((1,0,0))
                assert (moved-center).dot(outward)>0,name
        checked.append(name)
assert set(checked)==set(hinges),(set(hinges)-set(checked))
path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
print('PERSONA_HINGES_OK',json.dumps(checked))
