"""Update field kit meshes, item icons and the shared trial/item catalog."""
from pathlib import Path
import unreal as u
import os

REIMPORT_NORTH=True
prefix=(Path(__file__).parent/'configure_northern_forest.py').read_text(encoding='utf-8').split('# Mist on the six planes')[0]
if not os.environ.get('PERSONA_CATALOG_ONLY'):
    exec(compile(prefix,'configure_northern_forest.py','exec'))
else:
    root=Path(__file__).resolve().parents[1]
    lib=u.EditorAssetLibrary;tools=u.AssetToolsHelpers.get_asset_tools()
    def save(obj):
        if not lib.save_loaded_asset(obj,only_if_is_dirty=False):raise RuntimeError(obj.get_path_name())

def data(name,cls):
    path='/Game/Persona/Items/'+name
    if lib.does_asset_exist(path):return lib.load_asset(path)
    factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',cls)
    return tools.create_asset(name,'/Game/Persona/Items',cls,factory)

syringe=data('DA_Syringe',u.PersonaItemDefinition)
syringe.set_editor_property('display_name','Шприц')
syringe.set_editor_property('max_stack',8)
syringe.set_editor_property('mesh',lib.load_asset('/Game/Persona/North/SM_Syringe'))
syringe.set_editor_property('carry_offset',u.Vector(51,14,-17))
syringe.set_editor_property('left_grip',u.Vector(-7,-11,-7))
syringe.set_editor_property('right_grip',u.Vector(-2,4,-7))
syringe.set_editor_property('use_kind',u.PersonaUseKind.HEALTH)
syringe.set_editor_property('use_amount',35.)
syringe.set_editor_property('npc_drop_chance',.05)

apple=lib.load_asset('/Game/Persona/Items/DA_Ration')
apple.set_editor_property('use_kind',u.PersonaUseKind.FOOD);apple.set_editor_property('use_amount',40.)
water=lib.load_asset('/Game/Persona/Items/DA_Flask')
water.set_editor_property('use_kind',u.PersonaUseKind.WATER);water.set_editor_property('use_amount',45.)
ammo=lib.load_asset('/Game/Persona/Items/DA_Ammo12GA')
ammo.set_editor_property('npc_drop_chance',.25);ammo.set_editor_property('npc_drop_max',2)
gun=lib.load_asset('/Game/Persona/Items/DA_Shotgun');gun.set_editor_property('ammunition',ammo)
names={'DA_Shotgun':'Дробовик','DA_Ammo12GA':'Патроны 12 калибра','DA_Flask':'Питьевая вода',
       'DA_HUB_ChoppingBoard':'Разделочная доска','DA_HUB_CookingPot':'Кастрюля','DA_HUB_Hammer':'Молоток',
       'DA_HUB_Toolbox':'Ящик с инструментами','DA_HUB_Wrench':'Гаечный ключ'}
for name,label in names.items():lib.load_asset('/Game/Persona/Items/'+name).set_editor_property('display_name',label)
exec(compile((Path(__file__).parent/'configure_medical_readability.py').read_text(encoding='utf-8'),'configure_medical_readability.py','exec'))

items=[]
for path in lib.list_assets('/Game/Persona/Items',recursive=True,include_folder=False):
    if not path.rsplit('/',1)[-1].startswith('DA_'):continue
    obj=lib.load_asset(path)
    if isinstance(obj,u.PersonaItemDefinition):items.append(obj)
items.sort(key=lambda obj:str(obj.get_editor_property('display_name')).lower())
catalog=data('DA_ItemCatalog',u.PersonaItemCatalog);catalog.set_editor_property('items',items);save(catalog)

preview=root/'Artifacts/Previews/Icons0101';preview.mkdir(exist_ok=True,parents=True)
for item in items:
    icon=u.PersonaAssetTools.bake_item_icon(item,'/Game/Persona/Items/Icons/T_'+item.get_name())
    if not icon:raise RuntimeError('Cannot render item icon: '+item.get_name())
    save(icon);save(item)
    task=u.AssetExportTask();task.object=icon;task.filename=str(preview/(item.get_name()+'.png'));task.automated=True;task.replace_identical=True;task.prompt=False;task.exporter=u.TextureExporterPNG()
    if not u.Exporter.run_asset_export_task(task):raise RuntimeError('Cannot export icon preview')

def find(item,source,npc=False):
    row=u.PersonaTrialFind();row.set_editor_property('item',item);row.set_editor_property('source',source);row.set_editor_property('from_npc',npc);return row

forest=lib.load_asset('/Game/Persona/Trials/DA_SurvivalAgility')
forest.set_editor_property('finds',[
    find(apple,'Дикие яблони. Плоды восстанавливаются через 7 игровых минут.'),
    find(water,'Родники. Вода восстанавливается через 7 игровых минут.'),
    find(lib.load_asset('/Game/Persona/Items/DA_Scrap'),'Тайники. Запас исчерпывается до следующей вылазки.'),
    find(lib.load_asset('/Game/Persona/Items/DA_Cloth'),'Тайники. Запас исчерпывается до следующей вылазки.'),
    find(ammo,'Редкие тайники и тела противников.',True),find(syringe,'Тела противников.',True)])
forest.set_editor_property('requirements',[]);save(forest)
shooting=lib.load_asset('/Game/Persona/Trials/DA_Shooting')
shooting.set_editor_property('finds',[find(gun,'Стартовые контейнеры испытания.'),find(ammo,'Стартовые контейнеры и тела противников.',True),find(syringe,'Тела противников.',True)])
shooting.set_editor_property('requirements',[])
shooting.set_editor_property('loot_description','В контейнерах — дробовики и патроны. На телах противников иногда встречаются дополнительные патроны и шприцы. Лечение между волнами не предоставляется.')
save(shooting)
u.log('PERSONA 0.10.1 authored: '+str(len(items))+' items with icons')
