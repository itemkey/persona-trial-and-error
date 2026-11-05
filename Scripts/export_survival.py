"""Build the forest's modular field kit and foliage in centimetres for Unreal."""
import json
import math
import random
from pathlib import Path

import bpy

out = Path(__file__).resolve().parents[1] / '.work' / 'SurvivalArt'
out.mkdir(parents=True, exist_ok=True)
rng = random.Random(8771)
bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = .01
palette = dict(peat=(.18,.115,.065), moss=(.21,.29,.09), pine=(.045,.16,.105),
               needle=(.09,.23,.14), bark=(.22,.13,.07), stone=(.32,.35,.32),
               slate=(.19,.22,.23), canvas=(.40,.32,.15), rust=(.65,.19,.065),
               cream=(.86,.76,.49), metal=(.24,.31,.31), seam=(.065,.07,.06),
               water=(.07,.40,.53), red=(.82,.08,.045))
materials = {}
for key, color in palette.items():
    m = bpy.data.materials.new(key)
    m.diffuse_color = (*color,1)
    materials[key] = m

vertices, faces, shades = [], [], []
def face(points, material):
    base = len(vertices)
    vertices.extend(points)
    faces.append(tuple(range(base, base+len(points))))
    shades.append(material)

def box(x,y,z,w,d,h,mat):
    p = [(x+i*w/2,y+j*d/2,z+k*h) for k in (0,1) for i,j in ((-1,-1),(1,-1),(1,1),(-1,1))]
    for ids in ((3,2,1,0),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)):
        face([p[i] for i in ids],mat)

def rings(x,y,z,rows,n,mat,rough=0):
    layers=[]
    phase=rng.uniform(0,6.28)
    for height,radius in rows:
        layer=[]
        for i in range(n):
            a=phase+math.tau*i/n
            r=radius*(1+rng.uniform(-rough,rough))
            layer.append((x+r*math.cos(a),y+r*math.sin(a),z+height))
        layers.append(layer)
    face(layers[0][::-1],mat)
    for lower,upper in zip(layers,layers[1:]):
        for i in range(n):
            j=(i+1)%n
            face([lower[i],lower[j],upper[j],upper[i]],mat)
    face(layers[-1],mat)

def export(name):
    mesh=bpy.data.meshes.new(name)
    mesh.from_pydata(vertices,[],faces); mesh.update()
    uv=mesh.uv_layers.new(name='UVMap')
    for poly in mesh.polygons:
        axis=max(range(3),key=lambda i:abs(poly.normal[i]))
        a,b=[i for i in range(3) if i!=axis]
        for loop in poly.loop_indices:
            p=mesh.vertices[mesh.loops[loop].vertex_index].co
            uv.data[loop].uv=(p[a]*.01,p[b]*.01)
    keys=list(dict.fromkeys(shades))
    for key in keys: mesh.materials.append(materials[key])
    for poly,key in zip(mesh.polygons,shades): poly.material_index=keys.index(key)
    obj=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(obj)
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True); bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.fbx(filepath=str(out/(name+'.fbx')),use_selection=True,axis_forward='-Y',axis_up='Z',
                             apply_unit_scale=True,add_leaf_bones=False,bake_anim=False,mesh_smooth_type='FACE')
    bpy.data.objects.remove(obj,do_unlink=True)
    vertices.clear(); faces.clear(); shades.clear()
    manifest[name]=keys

manifest={}
for variant in range(3):
    height=rng.uniform(650,1050)
    rings(0,0,0,[(0,29),(height*.58,14),(height*.87,4)],8,'bark',.1)
    for layer in range(7):
        width=height*(.245-layer*.026)
        z=height*(.18+layer*.103)
        rings(0,0,z,[(0,width*.64),(height*.034,width),(height*.31,width*.04)],11,
              'pine' if layer%2==0 else 'needle',.20)
    export('Pine'+str(variant))
rings(0,0,-24,[(0,85),(55,108),(112,69),(135,18)],8,'stone',.32)
export('Boulder')
# Reusable 20 m square of gently uneven forest floor. All edges meet at grade.
grid=8
zs=[[0 if i in (0,grid) or j in (0,grid) else rng.uniform(-13,13) for j in range(grid+1)] for i in range(grid+1)]
for i in range(grid):
    for j in range(grid):
        pts=[((a/grid-.5)*2000,(b/grid-.5)*2000,zs[a][b]) for a,b in [(i,j),(i+1,j),(i+1,j+1),(i,j+1)]]
        face(pts[:3],'moss' if rng.random()>.24 else 'peat'); face([pts[0],pts[2],pts[3]],shades[-1])
export('ForestFloor')
# Cliff walls along the river and plank bridges have walkable collision.
box(0,0,-3050,2000,110,3050,'slate')
for x in range(-900,901,180):
    rings(x,0,-2950,[(0,120),(1780,110),(2750,58),(2880,32)],7,'stone',.2)
export('Cliff')
for i in range(45): box((i-22)*46,0,0,44,280,14,'bark' if i%4 else 'peat')
for y in (-90,90): box(0,y,-24,2100,18,24,'bark')
export('Bridge')
# A-frame bivouac, with an open front and a bedroll visible inside.
for side in (-1,1):
    face([(-155,0,165),(155,0,165),(155,side*130,4),(-155,side*130,4)],'canvas')
box(0,0,0,300,220,6,'peat'); box(0,0,6,136,58,10,'moss')
export('Shelter')
box(0,0,0,60,38,25,'metal'); box(0,0,24,63,40,6,'rust')
for x in (-19,19): box(x,0,0,4,41,32,'seam')
box(0,0,31,20,7,2,'cream'); box(0,0,32,7,18,2,'cream')
export('MedCase')
rings(0,0,-90,[(0,2.6),(180,2.6)],8,'cream')
for i in range(5):
    x=i*10
    face([(x,4*math.sin(i),84-i*2),(x+10,4*math.sin(i+1),82-i*2),
          (x+10,4*math.sin(i+1),48-i*2),(x,4*math.sin(i),50-i*2)],'red' if i<4 else 'cream')
export('Flag')
rings(0,0,-6,[(0,5.5),(1,6),(11,6),(12,5.5)],16,'rust')
rings(0,0,-4,[(0,6.1),(6,6.1)],16,'cream'); export('Ration')
rings(0,0,-12,[(0,4),(2,4.8),(16,4.8),(20,2.5)],12,'water')
rings(0,0,8,[(0,2.6),(3,2.6)],12,'cream'); export('Flask')
for i in range(3): box(0,i*3-3,-2+i,17,3,2,'metal')
export('Scrap')
for i in range(3): box(i-1,0,-3+i*2,17,11,2,'canvas' if i!=1 else 'cream')
export('Cloth')
(out/'kit.json').write_text(json.dumps(dict(meshes=manifest,colors=palette)),encoding='utf-8')
