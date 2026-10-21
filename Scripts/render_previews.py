"""Run with -ExecutePythonScript (editor ticks), -RenderOffscreen, not a commandlet."""
import unreal as u
import time
u.EditorPythonScripting.set_keep_python_script_alive(True)
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Artifacts'/'Previews'
OUT.mkdir(parents=True,exist_ok=True)
level=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
views=[
    ('L_Hub',(-1700,-150,170),(0,0,0),''),
    ('L_TestChamber',(-1900,-150,170),(1,0,0),''),
    ('L_TestChamber',(900,1500,260),(10,-25,0),'_wide'),
]
state={'index':0,'phase':'load','deadline':time.monotonic()}

def tick(delta):
    if time.monotonic()<state['deadline']:return
    try:
        if state['index']==len(views):
            u.unregister_slate_post_tick_callback(state['handle'])
            u.SystemLibrary.quit_editor()
            return
        map_name,position,rotation,suffix=views[state['index']]
        if state['phase']=='load':
            level.load_level('/Game/Persona/Maps/'+map_name)
            world=u.EditorLevelLibrary.get_editor_world()
            capture=actors.spawn_actor_from_class(u.SceneCapture2D,u.Vector(*position),u.Rotator(*rotation))
            component=capture.get_component_by_class(u.SceneCaptureComponent2D)
            target=u.RenderingLibrary.create_render_target2d(world,1440,900,u.TextureRenderTargetFormat.RTF_RGBA8)
            component.set_editor_property('texture_target',target)
            component.set_editor_property('capture_source',u.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            component.set_editor_property('capture_every_frame',True)
            component.set_editor_property('always_persist_rendering_state',True)
            component.set_editor_property('fov_angle',90)
            state.update(world=world,capture=capture,target=target,phase='capture',deadline=time.monotonic()+30)
        else:
            name=map_name+suffix+'.png'
            u.RenderingLibrary.export_render_target(state['world'],state['target'],str(OUT),name)
            actors.destroy_actor(state['capture'])
            u.log('PERSONA_PREVIEW '+str(OUT/name))
            state.update(index=state['index']+1,phase='load',deadline=time.monotonic()+1)
    except Exception:
        import traceback
        u.log_error(traceback.format_exc())
        u.unregister_slate_post_tick_callback(state['handle'])
        u.SystemLibrary.quit_editor()

state['handle']=u.register_slate_post_tick_callback(tick)
