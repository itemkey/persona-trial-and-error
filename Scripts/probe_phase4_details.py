import bpy,json
from pathlib import Path
r={}
for n in ['TRIAL01_NPC_Gate_ROOT']:
 o=bpy.data.objects[n]
 r[n]={'type':o.type,'location':list(o.location),'parent':o.parent.name if o.parent else '', 'matrix':[list(row) for row in o.matrix_world], 'basis':[list(row) for row in o.matrix_basis], 'children':[{ 'name':c.name,'type':c.type,'location':list(c.location),'parent':c.parent.name if c.parent else ''} for c in o.children_recursive]}
r['walls']=[{'name':o.name,'position':list(o.matrix_world.translation),'dimensions':list(o.dimensions),'hidden':o.hide_render} for o in bpy.data.collections['HUB | Test: shooting'].all_objects if o.type=='MESH' and any(x in o.name.lower() for x in ['wall','floor','cube','enclosure'])]
Path(__file__).resolve().parents[1].joinpath('.work/phase4-details.json').write_text(json.dumps(r,indent=2))
