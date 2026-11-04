"""Idempotent, targeted asset update; does not rebuild maps or lighting."""
import json
from pathlib import Path
import unreal as u

ROOT = Path(__file__).resolve().parents[1]
BASE = '/Game/Persona'
lib = u.EditorAssetLibrary
editing = u.MaterialEditingLibrary

def save(asset):
    if not lib.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Cannot save '+asset.get_path_name()+'; close running game/editor instances.')

for name in ('A', 'B'):
    asset = u.load_asset(f'{BASE}/Hub/DA_TRIAL01_Container_{name}_ROOT')
    asset.set_editor_property('shared_inventory', True)
    save(asset)

manifest = json.loads((ROOT/'SourceArt/Phase4/manifest.json').read_text(encoding='utf-8'))
stove_record = next(a for a in manifest['actors'] if a['id'] == 'HUB_Stove_Oven')
stove = u.load_asset(BASE+'/Hub/DA_HUB_Stove_Oven')
# Measured from the source's four solid burners: 96.5 cm centre + 1.25 cm top.
burners = stove_record.get('burners', [[x, y, 97.75] for x in (-22, 22) for y in (20, -20)])
stove.set_editor_property('burners', [u.Vector(*p) for p in burners])
stove.set_editor_property('burner_radius', 14.5)
save(stove)

path = BASE+'/Materials/M_CookwarePreview'
material = u.load_asset(path) if lib.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_CookwarePreview', BASE+'/Materials', u.Material, u.MaterialFactoryNew())
material.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('two_sided', True)
editing.delete_all_material_expressions(material)
color = editing.create_material_expression(material, u.MaterialExpressionConstant3Vector)
color.set_editor_property('constant', u.LinearColor(30, 35, 35, 1))
opacity = editing.create_material_expression(material, u.MaterialExpressionConstant)
opacity.set_editor_property('r', .32)
editing.connect_material_property(color, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
editing.connect_material_property(opacity, '', u.MaterialProperty.MP_OPACITY)
editing.recompile_material(material)
save(material)
pot = u.load_asset(BASE+'/Items/DA_HUB_CookingPot')
pot.set_editor_property('stove_cookware', True)
pot.set_editor_property('placement_material', material)
save(pot)

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
levels.load_level(BASE+'/Maps/L_Hub')
decorations = {'DA_HUB_Hammer', 'DA_HUB_Wrench', 'DA_HUB_Toolbox'}
changed = False
for actor in actors.get_all_level_actors():
    if not isinstance(actor, u.PersonaProp):
        continue
    definition = actor.get_editor_property('definition')
    if not definition or definition.get_name() not in decorations:
        continue
    transform = actor.get_actor_transform()
    decoration = actors.spawn_actor_from_class(u.StaticMeshActor, transform.translation, transform.rotation.rotator())
    decoration.set_actor_scale3d(transform.scale3d)
    decoration.set_actor_label(actor.get_actor_label())
    decoration.set_editor_property('tags', actor.get_editor_property('tags'))
    decoration.static_mesh_component.set_static_mesh(definition.get_editor_property('mesh'))
    decoration.static_mesh_component.set_collision_profile_name('BlockAll')
    decoration.static_mesh_component.set_mobility(u.ComponentMobility.STATIC)
    actors.destroy_actor(actor)
    changed = True
if changed:
    if not levels.save_current_level():
        raise RuntimeError('Cannot save L_Hub; close running game/editor instances.')
u.log('PERSONA_INTERACTION_FIXES_OK')
