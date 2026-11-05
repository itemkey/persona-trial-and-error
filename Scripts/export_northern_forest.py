"""Author the northern forest field kit in Blender, in centimetres."""
import bpy, math, random, json
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[1]
out=root/'.work/NorthernArt'
out.mkdir(parents=True,exist_ok=True)
rng=random.Random(731899)
bpy.context.scene.unit_settings.system='METRIC'
bpy.context.scene.unit_settings.scale_length=.01
colors=dict(bark=(.084,.091,.088), deadwood=(.194,.176,.143), silver=(.52,.52,.475),
            shadow=(.026,.043,.040), needles=(.068,.104,.093), tips=(.12,.153,.131),
            leaf=(.21,.24,.171), shale=(.22,.236,.229), edge=(.32,.335,.32),
            iron=(.10,.112,.114), roots=(.131,.114,.094), skin=(.38,.053,.045),
            blush=(.49,.093,.062), ground=(.164,.169,.148), stream=(.12,.176,.175),
            fog=(.25,.281,.278), paper=(.54,.51,.421), rail=(.15,.165,.17))
mats={k:bpy.data.materials.new('N_'+k) for k in colors}
for k,v in mats.items():v.diffuse_color=(*colors[k],1)
verts=[]; faces=[]; slots=[]; manifest={}
def face(points,mat):
    start=len(verts);verts.extend([tuple(v) for v in points]);faces.append(tuple(range(start,len(verts))));slots.append(mat)
def tube(a,b,r1,r2,mat,sides=6):
    a,b=Vector(a),Vector(b); v=(b-a).normalized(); u=v.cross(Vector((0,0,1)) if abs(v.z)<.88 else Vector((0,1,0))).normalized(); w=v.cross(u)
    rim=[math.cos(i*math.tau/sides)*u+math.sin(i*math.tau/sides)*w for i in range(sides)]
    for i in range(sides):
        j=(i+1)%sides;face([a+rim[i]*r1,a+rim[j]*r1,b+rim[j]*r2,b+rim[i]*r2],mat)
    face([b+r*r2 for r in rim],mat)
def export(name):
    m=bpy.data.meshes.new(name);m.from_pydata(verts,[],faces);m.update()
    kinds=list(dict.fromkeys(slots))
    for k in kinds:m.materials.append(mats[k])
    uv=m.uv_layers.new(name='UV')
    for poly,k in zip(m.polygons,slots):
        poly.material_index=kinds.index(k)
        axis=max(range(3),key=lambda i:abs(poly.normal[i]));a,b=[i for i in range(3) if i!=axis]
        for l in poly.loop_indices:
            p=m.vertices[m.loops[l].vertex_index].co;uv.data[l].uv=(p[a]*.01,p[b]*.01)
    obj=bpy.data.objects.new(name,m);bpy.context.collection.objects.link(obj)
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.fbx(filepath=str(out/(name+'.fbx')),use_selection=True,axis_forward='-Y',axis_up='Z',apply_unit_scale=True,bake_anim=False,mesh_smooth_type='FACE')
    bpy.data.objects.remove(obj,do_unlink=True);verts.clear();faces.clear();slots.clear();manifest[name]=kinds
def crown(a,d,length,width,mat):
    side=Vector((-d.y,d.x,0));up=Vector((0,0,1))
    p=a+length*.47*d
    spine=p+up*width*.42
    lobes=[a,p-side*width*.7-up*width*.27,a+length*.73*d-side*width*.58-up*width*.36,a+length*d-up*width*.38,
           a+length*.82*d+side*width*.43-up*width*.46,p+side*width-up*width*.32]
    for i in range(len(lobes)):
        face([lobes[i],lobes[(i+1)%len(lobes)],spine],mat if i%4 else 'shadow')
def stem(h,rad,mat,bend):
    points=[Vector((bend[0]*t*t,bend[1]*t*t,h*t)) for t in (0,.22,.49,.72,1)]
    for i,(a,b) in enumerate(zip(points,points[1:])):tube(a,b,rad*(1-a.z/h)+1,rad*(1-b.z/h)+1,mat,9)
    tube((0,0,-14),(0,0,27),rad*1.55,rad*.94,mat,9)
    for j in range(6):
        # Retain the old RNG draw count: branches and crowns remain unchanged.
        ang=rng.random()*math.tau;reach=rng.uniform(63,119)
        if j>=2+int(rad)%2:continue
        d=Vector((math.cos(ang),math.sin(ang),0))
        tube(d*rad*.42+Vector((0,0,4)),d*(rad+reach*.22)+Vector((0,0,-12)),rad*.36,.7,mat)
    return lambda t:Vector((bend[0]*t*t,bend[1]*t*t,h*t))
for n in range(4):
    h=rng.uniform(900,1420); axis=stem(h,rng.uniform(14,23),'bark',(rng.uniform(-92,92),rng.uniform(-54,54)))
    for j in range(43):
        t=.14+.84*j/43;ang=j*2.39996+rng.uniform(-.40,.40);d=Vector((math.cos(ang),math.sin(ang),0))
        length=(.08+.18*(1-t))*h*rng.uniform(.66,1.13)*min(1,(1-t)*7);a=axis(t)
        b=a+d*length*.55+Vector((0,0,-length*.16));c=a+d*length+Vector((0,0,length*.055))
        tube(a,b,5.8*(1-t)+1,2.1,'bark');tube(b,c,2.1,.7,'deadwood')
        if t<.22 or rng.random()<.075:continue
        for s in (-1,1):
            perp=Vector((-d.y*s,d.x*s,0))
            for q in (.24,.48,.70,.87):
                v=(d*.78+perp*(1-q*.55)).normalized()
                p=a.lerp(b,min(1,q*1.65)) if q<.55 else b.lerp(c,(q-.55)/.45)
                crown(p,v,length*(.24+.47*(1-q)),length*.14, rng.choice(['needles','needles','tips']))
    export('Spruce'+str(n))
for kind in ['Birch','Orchard','Snag']:
    h={'Birch':820,'Orchard':340,'Snag':420}[kind]
    mat='silver' if kind=='Birch' else 'deadwood'
    axis=stem(h,12 if kind=='Birch' else 21,mat,(42,-16))
    for j in range(17 if kind!='Snag' else 6):
        t=rng.uniform(.25,.92);a=axis(t);ang=j*2.39996;d=Vector((math.cos(ang),math.sin(ang),0));l=h*(1-t*.65)*rng.uniform(.19,.48)
        b=a+d*l*.68+Vector((0,0,l*.27));c=b+d*l*.36+Vector((0,0,l*.48))
        tube(a,b,6,3,mat);tube(b,c,3,1.2,mat)
        if kind!='Snag':
            for s in (-1,1):
                v=(d+Vector((-d.y*s,d.x*s,0))*.78).normalized()
                e=c+v*l*.32+Vector((0,0,l*.18));tube(b,e,2,.7,mat)
                crown(e-v*27,v,rng.uniform(35,68),rng.uniform(16,38),'leaf')
        if kind=='Birch':
            p=axis(t);face([p+Vector((10,-5,0)),p+Vector((11,5,1)),p+Vector((10,6,4))],'bark')
    export(kind)
for n in range(2):
    ang=rng.random()*6.28; rings=[]
    for z,r in [(-28,.8),(13,1),(69,.91),(104,.61),(118,.29)]:
        rings.append([Vector((math.cos(ang+i*.785)*125*r*rng.uniform(.85,1.10)+z*.22,math.sin(ang+i*.785)*72*r,z+rng.uniform(-10,10))) for i in range(8)])
    for i in range(4):
        for j in range(8):face([rings[i][j],rings[i][(j+1)%8],rings[i+1][(j+1)%8],rings[i+1][j]],'edge' if i==3 else 'shale')
    face(rings[-1],'edge');export('Outcrop'+str(n))
a=Vector((-120,0,22));b=Vector((158,17,14));tube(a,b,21,12,'deadwood',10)
for j in range(8):
    p=a.lerp(b,rng.random());tube(p,p+Vector((22,rng.uniform(-63,67),rng.uniform(20,64))),5,1,'bark')
export('Windfall')
# Lobes at both ends, a stem and a leaf make the held fruit recognisable from above.
rings=[]
for z,r in [(-4,.8),(-3.9,2.8),(-2.4,4.2),(.3,4.9),(2.6,4.6),(3.75,3.45),(3.95,2.25),(2.85,.45)]:
    rings.append([Vector((r*math.cos(a)*(1+.038*math.cos(a*5)),r*math.sin(a)*(1+.038*math.cos(a*5)),z+.37*math.cos(a*5)*abs(z)/4)) for a in [i*math.tau/24 for i in range(24)]])
for a,b in zip(rings,rings[1:]):
    for i in range(24):face([a[i],a[(i+1)%24],b[(i+1)%24],b[i]],'blush' if i in range(3,8) else 'skin')
face(rings[0][::-1],'skin');face(rings[-1],'skin');tube((0,0,2.65),(.65,.2,5.8),.28,.2,'bark')
face([(.4,.2,4.8),(1.8,-.55,5.65),(3.3,.25,5.42),(1.7,.8,5.06)],'leaf');export('Apple')
# Field injector: a matte barrel, plunger, finger rests, safety collar and needle.
tube((-7,0,0),(5,0,0),1.45,1.45,'paper',12)
tube((-10,0,0),(-7,0,0),.55,.55,'iron',10)
tube((-11,0,0),(-10,0,0),2.1,2.1,'paper',12)
tube((5,0,0),(6.2,0,0),1.45,.7,'iron',12)
tube((6.2,0,0),(10.6,0,0),.15,.05,'silver',8)
tube((2.5,0,0),(3.2,0,0),1.5,1.5,'blush',12)
for side in (-1,1):tube((-7,0,0),(-7,side*3,0),.6,.6,'paper',8)
export('Syringe')
(out/'kit.json').write_text(json.dumps(dict(meshes=manifest,colors=colors)))

