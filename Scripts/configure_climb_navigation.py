"""Preserve narrow cover tops when rasterizing the authored arena navigation."""
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
for name in ('L_Shooting', 'L_Hub'):
    levels.load_level('/Game/Persona/Maps/' + name)
    for actor in actors.get_all_level_actors():
        if not isinstance(actor, u.RecastNavMesh):
            continue
        params = actor.get_editor_property('nav_mesh_resolution_params')
        for index, value in enumerate(params):
            value.set_editor_property('cell_size', 20.0 if index == 0 else 10.0)
            value.set_editor_property('cell_height', 5.0)
            value.set_editor_property('agent_max_step_height', 35.0)
        actor.set_editor_property('nav_mesh_resolution_params', params)
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    if not u.PersonaAssetTools.build_navigation(world):
        raise RuntimeError('Navigation build failed: ' + name)
    if name == 'L_Shooting':
        for height, x, y in ((140, -900, -3300), (220, -740, -3140)):
            point = u.NavigationSystemV1.project_point_to_navigation(
                world, u.Vector(x, y, -12600 + height), None, None, u.Vector(35, 35, 20))
            if point is None:
                raise RuntimeError('Cover top is absent from navigation: ' + str(height))
    levels.save_current_level()
u.log('PERSONA_CLIMB_NAVIGATION_OK')
