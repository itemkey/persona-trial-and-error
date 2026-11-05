"""Create the native battle arena's persistent map without touching existing sectors."""
import unreal as u
path='/Game/Persona/Maps/L_SectorBattle'
if not u.EditorAssetLibrary.does_asset_exist(path):
    levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
    if not levels.new_level(path):
        raise RuntimeError('Could not create battle map')
    world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',u.PersonaGameMode.static_class())
    if not levels.save_current_level():
        raise RuntimeError('Could not save battle map')
u.log('PERSONA 0.11.0: native industrial battle map ready')
