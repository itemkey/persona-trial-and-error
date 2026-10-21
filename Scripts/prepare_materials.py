"""Persist all runtime shader usages; editor auto-detection alone is not sufficient."""
import unreal as u
for name in ['M_Charcoal','M_Concrete','M_Paper','M_Void','M_WhiteLight','M_Persona','M_Observer']:
    material=u.load_asset('/Game/Persona/Materials/'+name)
    u.MaterialEditingLibrary.set_material_usage(material,u.MaterialUsage.MATUSAGE_STATIC_LIGHTING)
    if name in ['M_Persona','M_Observer']:
        u.MaterialEditingLibrary.set_material_usage(material,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        u.MaterialEditingLibrary.set_material_usage(material,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    if name=='M_Observer':
        tone=u.MaterialEditingLibrary.get_material_property_input_node(material,u.MaterialProperty.MP_BASE_COLOR)
        tone.set_editor_property('default_value',u.LinearColor(.018,.020,.023,1))
    u.MaterialEditingLibrary.recompile_material(material)
    u.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False)
for name in ['SK_Persona','SK_Persona_Arms']:
    mesh=u.load_asset('/Game/Persona/Characters/'+name)
    materials=mesh.get_editor_property('materials')
    for i in range(len(materials)):
        slot=materials[i]
        slot.set_editor_property('material_interface',u.load_asset('/Game/Persona/Materials/M_Persona'))
        materials[i]=slot
    mesh.set_editor_property('materials',materials)
    u.EditorAssetLibrary.save_loaded_asset(mesh,only_if_is_dirty=False)
u.log('PERSONA_MATERIAL_USAGES_SAVED')
