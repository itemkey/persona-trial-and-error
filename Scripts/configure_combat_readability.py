"""Keep hostile silhouettes readable under the arena's fixed -7.5 EV exposure.
Only dynamic character assets change; existing map lightmaps stay valid.
"""
import unreal as u
material=u.load_asset('/Game/Persona/Materials/M_Observer')
editing=u.MaterialEditingLibrary
floor=editing.get_material_property_input_node(material,u.MaterialProperty.MP_EMISSIVE_COLOR)
if not floor:
    floor=editing.create_material_expression(material,u.MaterialExpressionVectorParameter)
    floor.set_editor_property('parameter_name','ShadowTone')
    editing.connect_material_property(floor,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
# Scene-linear intensity compensates for exposure; the displayed result remains
# dark grey, without an outline or visibility through occluding geometry.
floor.set_editor_property('default_value',u.LinearColor(8,8.4,8.8,1))
editing.set_material_usage(material,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
editing.set_material_usage(material,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
editing.recompile_material(material)
u.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False)
bp=u.load_asset('/Game/Persona/AI/BP_PersonaNPC')
mesh=u.get_default_object(bp.generated_class()).get_editor_property('mesh')
mesh.set_editor_property('override_materials',[material]*mesh.get_num_materials())
u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
u.log('PERSONA_COMBAT_READABILITY_OK')
