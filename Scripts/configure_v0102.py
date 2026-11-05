"""Rebuild only player crouch clips, keeping all maps and seeded forests intact."""
import unreal as u
root='/Game/Persona/Animation/Stances/'
blend=u.load_asset(root+'BS_Crouch')
if not blend:
    raise RuntimeError('Missing player crouch blend space')
u.PersonaAssetTools.bake_crouch_blend(blend)
for path in u.EditorAssetLibrary.list_assets(root,recursive=False):
    asset=u.load_asset(path)
    if isinstance(asset,u.AnimSequence) and asset.get_name().startswith('Crouch_'):
        u.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False)
u.EditorAssetLibrary.save_loaded_asset(blend,only_if_is_dirty=False)
u.log('PERSONA 0.10.2: player crouch clips rebuilt in place')
