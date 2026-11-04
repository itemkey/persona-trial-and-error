"""Incremental combat/compartment asset update; does not rebuild authored maps."""
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir())
BASE = '/Game/Persona'
LIB = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()


def save(asset):
    if not asset:
        raise RuntimeError('Missing required combat asset')
    LIB.save_loaded_asset(asset, only_if_is_dirty=False)


records = json.loads((ROOT / 'SourceArt/Audio/combat_provenance.json').read_text())['assets']
for name in records:
    task = u.AssetImportTask()
    task.filename = str(ROOT / 'SourceArt/Audio' / (name + '.wav'))
    task.destination_path = BASE + '/Audio'
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    sound = u.load_asset(BASE + '/Audio/' + name)
    sound.set_editor_property('looping', False)
    path = BASE + '/Audio/ATT_' + name
    attenuation = u.load_asset(path) or TOOLS.create_asset('ATT_' + name, BASE + '/Audio', u.SoundAttenuation, u.SoundAttenuationFactory())
    settings = attenuation.get_editor_property('attenuation')
    settings.set_editor_property('attenuate', True)
    settings.set_editor_property('spatialize', True)
    settings.set_editor_property('attenuation_shape_extents', u.Vector(120 if name.startswith('Shotgun') else 60, 0, 0))
    settings.set_editor_property('falloff_distance', 9000 if name.startswith('Shotgun') else 1200)
    attenuation.set_editor_property('attenuation', settings)
    sound.set_editor_property('attenuation_settings', attenuation)
    save(attenuation)
    save(sound)

gun = u.load_asset(BASE + '/Items/DA_Shotgun')
for key, value in dict(camera_recoil_degrees=8, residual_recoil_degrees=4, recoil_aim_scale=.75,
                       recoil_yaw_degrees=.8, recoil_duration=.55,
                       pump_duration=records['Pump']['seconds'], load_duration=records['ShellLoad']['seconds']).items():
    gun.set_editor_property(key, value)
gun.set_editor_property('recoil_translation', u.Vector(-12, .4, 2.5))
gun.set_editor_property('recoil_rotation', u.Rotator(12, .7, -1.2))
for index, name in enumerate(('Translation', 'Rotation', 'Camera')):
    curve = u.load_asset(BASE + '/Items/C_Shotgun' + name)
    u.PersonaAssetTools.configure_recoil_curve(curve, index)
    save(curve)
save(gun)

deaths = ['MM_Death_Back_01', 'MM_Death_Front_01', 'MM_Death_Right_01', 'MM_Death_Left_01']
inputs = u.IKRetargetBatchOperationInputs()
inputs.assets_to_retarget = [LIB.find_asset_data('/Game/Characters/Mannequins/Anims/Death/' + name) for name in deaths]
inputs.source_mesh = u.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
inputs.target_mesh = u.load_asset(BASE + '/Characters/SK_Persona')
inputs.ik_retarget_asset = u.load_asset(BASE + '/Animation/RTG_Manny_Persona')
inputs.prefix = 'P_'
inputs.target_path = BASE + '/Animation/Retargeted'
inputs.include_referenced_assets = True
inputs.overwrite_existing_files = True
if any(not LIB.does_asset_exist(inputs.target_path + '/P_' + name) for name in deaths):
    if not u.IKRetargetBatchOperation.run_batch_retarget(inputs):
        raise RuntimeError('Death retarget failed')

stance_path = BASE + '/Animation/Stances'
source = u.load_asset(BASE + '/Animation/Retargeted/P_MM_Idle')
blend = u.load_asset(stance_path + '/BS_Crouch') or LIB.duplicate_asset(BASE + '/Animation/Retargeted/P_BS_Idle_Walk_Run', stance_path + '/BS_Crouch')
u.PersonaAssetTools.bake_crouch_blend(blend)
clips = {}
for name, kind in [('CrouchEnter', 1), ('CrouchExit', 2), ('Climb', 3)]:
    clip = u.load_asset(stance_path + '/' + name)
    if not clip:
        factory = u.AnimSequenceFactory()
        factory.set_editor_property('target_skeleton', source.get_editor_property('skeleton'))
        clip = TOOLS.create_asset(name, stance_path, u.AnimSequence, factory)
    u.PersonaAssetTools.bake_stance_clip(source, clip, kind)
    clips[name] = clip
    save(clip)
save(blend)
anim = u.load_asset(BASE + '/Animation/ABP_Persona')
defaults = u.get_default_object(anim.generated_class())
defaults.set_editor_property('crouch_locomotion', blend)
defaults.set_editor_property('crouch_enter_clip', clips['CrouchEnter'])
defaults.set_editor_property('crouch_exit_clip', clips['CrouchExit'])
defaults.set_editor_property('climb_clip', clips['Climb'])
defaults.set_editor_property('death_clips', [u.load_asset(inputs.target_path + '/P_' + name) for name in deaths])
save(anim)
physics = u.load_asset(BASE + '/Characters/SK_Persona_PhysicsAsset')
u.PersonaAssetTools.configure_body_physics(physics)
save(physics)

manifest = json.loads((ROOT / 'SourceArt/Phase4/manifest.json').read_text())
for record in manifest['actors']:
    if not record.get('parts'):
        continue
    definition = u.load_asset(BASE + '/Hub/DA_' + record['id'])
    if not definition:
        continue
    parts = definition.get_editor_property('parts')
    for index, part in enumerate(parts):
        # Mesh names are stable across component reconstruction and save reloads.
        part.set_editor_property('id', record['parts'][index]['mesh'])
        label = str(definition.get_editor_property('label'))
        part.set_editor_property('label', f'{label} — отделение {index + 1}')
    definition.set_editor_property('parts', parts)
    save(definition)

LIB.save_directory(BASE, only_if_is_dirty=True, recursive=True)
u.log('PERSONA_COMBAT_UPDATE_OK')
