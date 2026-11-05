"""Keep the small medical prop legible under both hub and forest exposure."""
import unreal as u

lib=u.EditorAssetLibrary;tools=u.AssetToolsHelpers.get_asset_tools();m=u.MaterialEditingLibrary
folder='/Game/Persona/Items/Materials'
colors={'paper':(.54,.55,.52),'iron':(.13,.15,.15),'silver':(.52,.54,.52),'blush':(.40,.065,.045)}
materials={}
for name,color in colors.items():
    path=folder+'/M_Syringe_'+name
    mat=lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset('M_Syringe_'+name,folder,u.Material,u.MaterialFactoryNew())
    m.delete_all_material_expressions(mat);mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    c=m.create_material_expression(mat,u.MaterialExpressionConstant3Vector);c.set_editor_property('constant',u.LinearColor(*color,1))
    normal=m.create_material_expression(mat,u.MaterialExpressionPixelNormalWS)
    shade=m.create_material_expression(mat,u.MaterialExpressionCustom)
    inputs=[]
    for arg in ('C','N'):
        i=u.CustomInput();i.set_editor_property('input_name',arg);inputs.append(i)
    shade.set_editor_property('inputs',inputs);shade.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    shade.set_editor_property('code','return C*(.45+.55*saturate(dot(normalize(N),normalize(float3(-.4,-.3,.85)))));')
    m.connect_material_expressions(c,'',shade,'C');m.connect_material_expressions(normal,'',shade,'N')
    inverse=m.create_material_expression(mat,u.MaterialExpressionEyeAdaptationInverse)
    names=m.get_material_expression_input_names(inverse)
    light=next(str(n) for n in names if 'light' in str(n).lower())
    alpha=next(str(n) for n in names if 'alpha' in str(n).lower())
    one=m.create_material_expression(mat,u.MaterialExpressionConstant);one.set_editor_property('r',1.)
    assert m.connect_material_expressions(shade,'',inverse,light)
    assert m.connect_material_expressions(one,'',inverse,alpha)
    assert m.connect_material_property(inverse,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    m.recompile_material(mat);assert lib.save_loaded_asset(mat,only_if_is_dirty=False);materials[name]=mat

mesh=lib.load_asset('/Game/Persona/North/SM_Syringe')
for index,slot in enumerate(mesh.get_editor_property('static_materials')):
    key=str(slot.get_editor_property('imported_material_slot_name')).removeprefix('N_')
    mesh.set_material(index,materials[key])
assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
item=lib.load_asset('/Game/Persona/Items/DA_Syringe')
item.set_editor_property('carry_rotation',u.Rotator(pitch=0,yaw=90,roll=0))
item.set_editor_property('left_grip',u.Vector(-14,-3,-8));item.set_editor_property('right_grip',u.Vector(8,2,-8))
assert lib.save_loaded_asset(item,only_if_is_dirty=False)
