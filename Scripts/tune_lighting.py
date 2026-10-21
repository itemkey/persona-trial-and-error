"""Apply verified exposure and bounded Lightmass sampling to the owned test maps."""
import unreal as u
level=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
for name,center,size in [('L_Hub',(0,0,700),(5000,3600,1800)),('L_TestChamber',(1500,0,1200),(8800,5600,2800))]:
    level.load_level('/Game/Persona/Maps/'+name)
    for actor in actors.get_all_level_actors():
        if isinstance(actor,u.PostProcessVolume):
            settings=actor.get_editor_property('settings')
            settings.set_editor_property('auto_exposure_bias',0.0)
            actor.set_editor_property('settings',settings)
    if not any(isinstance(a,u.LightmassImportanceVolume) for a in actors.get_all_level_actors()):
        volume=actors.spawn_actor_from_class(u.LightmassImportanceVolume,u.Vector(*center))
        volume.set_actor_scale3d(u.Vector(*(v/200 for v in size)))
        volume.set_actor_label('Lightmass - playable interior')
    level.save_current_level()
u.log('PERSONA_LIGHTING_TUNED')
