"""Run with UnrealEditor-Cmd -run=pythonscript after export_persona.py."""
import unreal as u
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
DEST = '/Game/Persona/Characters'
TOOLS = u.AssetToolsHelpers.get_asset_tools()
LIB = u.EditorAssetLibrary

def asset(name, path, cls, factory):
    existing = u.load_asset(path + '/' + name)
    return existing or TOOLS.create_asset(name, path, cls, factory)

def import_mesh(name, skeleton=None):
    existing = u.load_asset(DEST + '/' + name)
    if existing and '-PersonaReimport' not in u.SystemLibrary.get_command_line():
        return existing
    options = u.FbxImportUI()
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_as_skeletal', True)
    options.set_editor_property('mesh_type_to_import', u.FBXImportType.FBXIT_SKELETAL_MESH)
    options.set_editor_property('automated_import_should_detect_type', False)
    options.set_editor_property('import_animations', False)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_textures', False)
    options.set_editor_property('create_physics_asset', skeleton is None)
    if skeleton:
        options.set_editor_property('skeleton', skeleton)
    data = options.get_editor_property('skeletal_mesh_import_data')
    data.set_editor_property('import_morph_targets', True)
    data.set_editor_property('update_skeleton_reference_pose', False)
    data.set_editor_property('use_t0_as_ref_pose', False)
    data.set_editor_property('convert_scene', True)
    data.set_editor_property('force_front_x_axis', False)
    data.set_editor_property('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    task = u.AssetImportTask()
    task.filename = str(ROOT / 'SourceArt' / 'Persona' / (name + '.fbx'))
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = existing is not None
    task.save = True
    task.options = options
    task.factory = u.FbxFactory()
    TOOLS.import_asset_tasks([task])
    mesh = u.load_asset(DEST + '/' + name)
    if not mesh:
        raise RuntimeError('Skeletal mesh import failed: ' + name + ' ' + str(task.imported_object_paths))
    return mesh

body = import_mesh('SK_Persona')
skeleton = body.get_editor_property('skeleton')
arms = import_mesh('SK_Persona_Arms', skeleton)

source = u.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
if not source:
    raise RuntimeError('Copy bundled TemplateResources/High/Characters/Content to Content/Characters first.')

CHAINS = {
    'Root': ('root','root'), 'Spine': ('spine_01','spine_02'), 'Head': ('head','head'),
    'LeftArm': ('upperarm_l','hand_l'), 'RightArm': ('upperarm_r','hand_r'),
    'LeftLeg': ('thigh_l','foot_l'), 'RightLeg': ('thigh_r','foot_r'),
    'LeftClavicle': ('clavicle_l','clavicle_l'), 'RightClavicle': ('clavicle_r','clavicle_r'),
}
def rig(name, mesh, source_rig=False):
    result = asset(name, '/Game/Persona/Animation', u.IKRigDefinition, u.IKRigDefinitionFactory())
    controller = u.IKRigController.get_controller(result)
    controller.set_skeletal_mesh(mesh)
    controller.set_retarget_root('pelvis')
    for chain, (start,end) in CHAINS.items():
        if source_rig and chain == 'Spine': end = 'spine_05'
        if chain not in [str(x.chain_name) for x in controller.get_retarget_chains()]:
            controller.add_retarget_chain(chain, start, end, '')
    LIB.save_loaded_asset(result)
    return result

source_rig = rig('IK_Manny_Source', source, True)
target_rig = rig('IK_Persona', body)
retargeter = asset('RTG_Manny_Persona', '/Game/Persona/Animation', u.IKRetargeter, u.IKRetargetFactory())
controller = u.IKRetargeterController.get_controller(retargeter)
controller.set_ik_rig(u.RetargetSourceOrTarget.SOURCE, source_rig)
controller.set_ik_rig(u.RetargetSourceOrTarget.TARGET, target_rig)
controller.set_preview_mesh(u.RetargetSourceOrTarget.SOURCE, source)
controller.set_preview_mesh(u.RetargetSourceOrTarget.TARGET, body)
controller.add_default_ops()
controller.auto_map_chains(u.AutoMapChainType.EXACT, True)
controller.auto_align_all_bones(u.RetargetSourceOrTarget.TARGET, u.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
LIB.save_loaded_asset(retargeter)

source_paths = [
    '/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run',
    '/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump',
    '/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS',
]
inputs = u.IKRetargetBatchOperationInputs()
inputs.assets_to_retarget = [LIB.find_asset_data(p) for p in source_paths]
inputs.source_mesh = source
inputs.target_mesh = body
inputs.ik_retarget_asset = retargeter
inputs.prefix = 'P_'
inputs.target_path = '/Game/Persona/Animation/Retargeted'
inputs.include_referenced_assets = True
inputs.overwrite_existing_files = True
outputs = u.IKRetargetBatchOperation.run_batch_retarget(inputs)
if not outputs:
    raise RuntimeError('Retarget produced no animation assets.')
LIB.save_directory('/Game/Persona', only_if_is_dirty=False, recursive=True)
report = {'mesh':body.get_path_name(),'arms':arms.get_path_name(),'skeleton':skeleton.get_path_name(),
          'bounds':str(body.get_bounds()),'animations':[str(a.package_name) for a in outputs]}
(ROOT / 'SourceArt' / 'Persona' / 'import_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('PERSONA_IMPORT_OK',json.dumps(report))
