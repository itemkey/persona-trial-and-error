"""Update existing Unreal assets only; never reads or exports the user's .blend."""
import unreal as u
from pathlib import Path
import runpy

ROOT=Path(__file__).resolve().parent
LIB=u.EditorAssetLibrary
M=u.MaterialEditingLibrary
BASE='/Game/Persona'
runpy.run_path(str(ROOT/'prepare_materials.py'))
runpy.run_path(str(ROOT/'configure_gameplay.py'))

mat=u.load_asset(BASE+'/Materials/M_GraphicAtmosphere')
mat.set_editor_property('blendable_location',u.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
node=M.get_material_property_input_node(mat,u.MaterialProperty.MP_EMISSIVE_COLOR)
code=node.get_editor_property('code')
code=code.replace('float mask=step(0.5,Stencil.r)*visible;','float mask=step(0.5,Stencil.r)*(1-step(2.5,Stencil.r))*visible;')
code=code.replace('View.ViewSizeAndInvSize.zw','View.BufferSizeAndInvSize.zw')
code=code.replace('result*0.15,edge*mask*fade*0.85','result*0.25,edge*mask*fade*0.65')
node.set_editor_property('code',code)
M.recompile_material(mat); LIB.save_loaded_asset(mat)

level=u.get_editor_subsystem(u.LevelEditorSubsystem)
path=BASE+'/Maps/L_MainMenu'
if LIB.does_asset_exist(path): level.load_level(path)
else: level.new_level(path)
world=u.EditorLevelLibrary.get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',u.load_class(None,'/Script/PersonaTrial.PersonaMenuMode'))
level.save_current_level()
LIB.save_directory(BASE,only_if_is_dirty=True,recursive=True)
u.log('PERSONA_PHASE2_ASSETS_OK')
