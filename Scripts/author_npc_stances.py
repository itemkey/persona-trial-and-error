"""Author independent PERSONA crouch actions in a new Blender source file."""
import bpy,math,json
from pathlib import Path
from mathutils import Vector,Matrix,Quaternion
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'SourceArt/NPCMotion';OUT.mkdir(parents=True,exist_ok=True)
rig=bpy.data.objects['SK_Persona_Rig'];rig.data.pose_position='POSE'
bpy.context.view_layer.objects.active=rig
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
rig.animation_data_clear();rig.animation_data_create()
rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
heads={b.name:b.head_local.copy() for b in rig.data.bones}
scene=bpy.context.scene;scene.render.fps=60

def smooth(t):return t*t*(3-2*t)
def place_segment(name,start,end):
    bone=rig.data.bones[name];direction=(bone.tail_local-bone.head_local).normalized()
    q=direction.rotation_difference((end-start).normalized())
    rig.pose.bones[name].matrix=Matrix.Translation(start)@q.to_matrix().to_4x4()@rest[name].to_quaternion().to_matrix().to_4x4()
    bpy.context.view_layer.update()
def solve(upper,lower,tip,target,pole):
    a=rig.pose.bones[upper].head.copy()
    l1=(heads[lower]-heads[upper]).length;l2=(heads[tip]-heads[lower]).length
    delta=target-a;d=min(max(delta.length,.1),l1+l2-.02);axis=delta.normalized()
    bend=(pole-axis*pole.dot(axis)).normalized()
    along=(l1*l1-l2*l2+d*d)/(2*d);height=math.sqrt(max(0,l1*l1-along*along))
    knee=a+axis*along+bend*height
    place_segment(upper,a,knee);place_segment(lower,knee,target)
    rig.pose.bones[tip].matrix=Matrix.Translation(target)@rest[tip].to_quaternion().to_matrix().to_4x4()
    bpy.context.view_layer.update()

clips=[]
for name,angle,kind,frames in [('CrouchIdle',0,'idle',60),('CrouchEnter',0,'enter',17),('CrouchExit',0,'exit',19)]+[(f'CrouchMove_{a}',a,'move',48) for a in (-180,-135,-90,-45,0,45,90,135,180)]:
    action=bpy.data.actions.new(name);rig.animation_data.action=action
    for f in range(frames+1):
        scene.frame_set(f);t=f/frames
        alpha=smooth(t) if kind=='enter' else 1-smooth(t) if kind=='exit' else 1
        for pb in rig.pose.bones:pb.matrix_basis=Matrix.Identity(4)
        bpy.context.view_layer.update()
        phase=2*math.pi*t;moving=kind=='move'
        pelvis=rest['pelvis'].copy();pelvis.translation+=Vector((math.sin(phase)*1.2 if moving else 0,13*alpha,-35*alpha+(math.cos(phase*2)-1)*.6 if moving else -35*alpha))
        pelvis=Matrix.Translation(pelvis.translation)@Matrix.Rotation(math.radians(8*alpha),4,'X')@pelvis.to_quaternion().to_matrix().to_4x4()
        rig.pose.bones['pelvis'].matrix=pelvis;bpy.context.view_layer.update()
        spine=rig.pose.bones['spine_01'];m=spine.matrix.copy();m=Matrix.Translation(m.translation)@Matrix.Rotation(math.radians(20*alpha),4,'X')@m.to_quaternion().to_matrix().to_4x4();spine.matrix=m
        bpy.context.view_layer.update()
        direction=Vector((math.sin(math.radians(angle)),-math.cos(math.radians(angle)),0))
        lateral=abs(direction.x) if moving else 0
        # Side steps need a wider base and shorter travel so the feet never cross.
        cycle_distance=144+(100-144)*lateral
        for side,sign in [('l',1),('r',-1)]:
            target=heads['foot_'+side]+Vector((sign*(4+17*lateral)*alpha,0,0))
            if moving:
                q=(t+(0 if side=='l' else .5))%1;contact=.62;stride=cycle_distance*contact
                if q<contact:travel=.5-q/contact;lift=0
                else:s=(q-contact)/(1-contact);travel=-.5+smooth(s);lift=5*math.sin(math.pi*s)
                target+=direction*stride*travel+Vector((0,0,lift))
            solve('thigh_'+side,'calf_'+side,'foot_'+side,target,Vector((sign*.08,-1,0)))
            shoulder=rig.pose.bones['upperarm_'+side].head.copy()
            swing=math.sin(phase+(0 if side=='l' else math.pi))*5 if moving else 0
            solve('upperarm_'+side,'lowerarm_'+side,'hand_'+side,shoulder+Vector((sign*12,-22+swing,-32)),Vector((sign*.3,-1,0)))
        for pb in rig.pose.bones:
            pb.scale=Vector((1,1,1));pb.rotation_mode='QUATERNION';pb.keyframe_insert('location',frame=f);pb.keyframe_insert('rotation_quaternion',frame=f);pb.keyframe_insert('scale',frame=f)
    scene.frame_start=0;scene.frame_end=frames
    action.use_fake_user=True
    bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,object_types={'ARMATURE'},add_leaf_bones=False,
        bake_anim=True,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,
        apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
        primary_bone_axis='Y',secondary_bone_axis='X',armature_nodetype='NULL',use_armature_deform_only=True)
    clips.append(dict(name=name,angle=angle,kind=kind,seconds=frames/60))
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'PERSONA_NPC_Motion.blend'),copy=True)
(OUT/'stances.json').write_text(json.dumps(clips,indent=2))
print('PERSONA_NPC_STANCES_AUTHORED',len(clips))
