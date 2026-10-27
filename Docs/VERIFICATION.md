Актуальные проверки версии 0.5: [VERIFICATION_05.md](VERIFICATION_05.md). Проверки 0.4: [VERIFICATION_04.md](VERIFICATION_04.md). Ниже сохранены результаты предыдущих этапов.

# PERSONA 0.3 — руки и мини-инвентарь

Проверено 22.10.2026: UE 5.8.3, Windows Development, локальный Null/IP.
Editor собран через UBT; Game прошёл BuildCookRun/cook/stage/archive с ExitCode 0.
Актуальная готовая версия находится в `Artifacts/Windows`, запуск — `Start_Game.cmd`.
Логи сборки: `.work/build-phase3-final.log`, `.work/package-phase3-final.log`.

| Проверка | Результат | Отчёт в Artifacts/Validation |
|---|---|---|
| Три слота, импортированные модели, 1 дробовик / 20 патронов, частичный подбор, переполнение, выбор, бросок, одновременный захват, переходы | 18/18, два exe, 100 мс / 2% потерь | `phase3-packaged-inventory-lag100-loss2.json` |
| Отключение / повторный вход, полный выход из exe, загрузка сектора, сохранение предметов без дубликатов | 7/7 | `phase3-packaged-persistence.json` |
| Опущенные руки, ходьба, бег, прыжок, оружие при наклоне камеры и прыжке | 7/7 | `phase3-packaged-arms.json` |

Диагностика использует те же серверные операции подбора, выбора и броска, что управление.
У хоста и клиента при вращении камеры 240–270°/с измерено отклонение локального предмета
менее 0,001 см и 0,01°. Это проверка привязки к камере; визуальные кадры также просмотрены.
Руки решаются в локальном пространстве камеры, поза плеч независима от прыжка тела.
Амплитуда шага уменьшена, переходы сглажены; геометрия рук остаётся стилизованной PERSONA.

Кадры в `Artifacts/Previews`: `Phase3_Idle`, `Phase3_Walk`, `Phase3_Run`, `Phase3_Jump`,
`Phase3_Shotgun`, `Phase3_Ammo`, `Phase3_Shotgun_LookUp`, `Phase3_Shotgun_LookDown`,
`Phase3_Shotgun_Jump` (PNG, 1920×1080).

При проверке быстрого перехода испытание → хаб выявлен старый отложенный вызов NPC
с обращением к выгруженному GameMode. Таймер теперь отменяется в EndPlay и использует
слабую привязку к объекту. Повторный быстрый переход и ожидание после срока таймера прошли.

Экспорт `Shotgun_ROOT` и `Ammo12GA_ROOT` создаёт отдельные FBX и производный `.blend`
в `SourceArt/Items`. Исходный файл не записывается; экспорт проверяет его SHA-256 до/после.
Дополнительно геометрия экспортов сверена с актуальным исходником: 13 333 / 3 945 вершины,
максимальное расхождение 0 см. Материалы и графические детали перенесены из моделей.

Сохранение секторов — формат 2, чтение старого формата 1 поддержано. Инвентарь хранится
в записи участника; набор свободных предметов восстанавливается отдельно, поэтому
подобранные и объединённые предметы не появляются заново при возврате на карту.

Повторение проверки (PowerShell, после упаковки):

```powershell
$diag="$PWD/Artifacts/Windows/PersonaTrial/.work/Diagnostics"
$shots="$PWD/Artifacts/Windows/PersonaTrial/Artifacts/Previews"
Scripts/RunPackagedLocal.ps1 -Players 2 -Diagnostics -RenderHost -NetworkEmulation
Scripts/TestInventory.ps1 -DiagnosticsRoot $diag -PreviewRoot $shots -ReportName phase3-packaged-inventory-lag100-loss2
Scripts/TestInventoryPersistence.ps1
Scripts/TestFirstPerson.ps1
Scripts/StopLocal.ps1
```

Стрельба, перезарядка и ручное деление стаков не реализованы. G/Q опускают/бросают весь
выбранный стак. Steam на двух физических ПК и субъективное ощущение анимации игроком
этими автоматизированными проверками не подтверждены. Сетевой фильтр версии — 0.3.0;
участники должны использовать одинаковую сборку.

## Архив проверок 0.2 (предыдущая версия)

Результаты на 22.10.2026. UE 5.8.3, Windows Development, MSVC 14.44.35229 / Windows SDK 22621.
Пользовательский `blender/persona_01.blend` в этом этапе не редактировался.

## Сборка и проверки

| Проверка | Результат | Отчёт в Artifacts/Validation |
|---|---|---|
| C++ Editor | Сборка успешна | `.work/build-phase2.log` |
| C++ Game, cook, stage, archive | BuildCookRun успешен, ExitCode 0 | `.work/package-phase2.log` |
| Передвижение, позы, дверь, AI, слух, зрение, урон | 17/17 | `phase2-packaged-gameplay.json` |
| Четыре участника, GAS, 2×2, переходы, конкуренция за предмет, бросок, отключения | 18/18 при 100 мс / 2% потерь | `phase2-packaged-network-lag100-loss2.json` |
| Создание, поиск, подключение, выход и повторное создание LAN-сессии | 6/6 | `phase2-packaged-sessions.json` |
| Меню, отдельные секторы, сохранение мира, отключённый друг и повторное подключение | 23/23 | `phase2-packaged-sectors.json` |
| Полное завершение exe и загрузка с диска в новом процессе | 6/6 | `phase2-packaged-cold-save.json` |

Проверки выполнялись через диагностический драйвер в отдельных готовых exe без редактора.
Он вызывает игровые операции и обработчики меню; это не автоматизация физических кликов мышью.
Сохранение проверено для позиций, приседания, здоровья, сытости, предмета у друга, двери,
идентичности участников и трёх восстановленных NPC с работающими Behavior Tree.
NPC наносил урон отключённому игроку; при возвращении здоровье не восстанавливалось,
новое тело не создавалось. Создание второго сектора не изменяло первый.

Исправлены сброс приседания при possession/restart и отсечение рук из-за узких bounds.
Тест перезапуска ждёт полного завершения exe. Контрольная позиция внутри пола хаба
проверяется как grounded, затем сравнивается до и после перезапуска.

## Визуальная проверка

Кадры в `Artifacts/Previews`: `Phase2_main_menu.png`, `Phase2_new_sector.png`,
`Phase2_sector_archive.png`, `Phase2_idle.png`, `Phase2_walk.png`, `Phase2_run.png`,
`Phase2_carry.png`, `Phase2_character.png`.

- В покое руки опущены за нижний край кадра; ходьба и бег плавно поднимают их и добавляют махи.
- Кисти и предмет используют общие точки контакта. Предмет находится ниже прицела.
  Исходная условная геометрия PERSONA сохранена; новые пальцы не моделировались.
- Игроки, NPC и руки используют один матовый серо-чёрный материал со слабой постоянной
  составляющей для читаемости в тенях.
- Архитектура исключена из графической обводки. Постпроцесс перенесён до временного
  сглаживания TSR, шаг соседних выборок соответствует размеру буфера.
- Скрытое тело владельца не отбрасывает тень на его отдельную модель рук. Полный персонаж
  продолжает отбрасывать тени в представлении других участников. Собственной тени полного
  тела в первом лице при текущем профиле обычных shadow maps нет.

Это остаётся художественным стендом. Сохранённые кадры не заменяют длительную субъективную
проверку пластики и мерцания на разных мониторах. Два Geometry Nodes исходника не исполняются
в Unreal; шесть перенесённых morph targets сохранены.

## Нагрузка

`phase2-packaged-network-lag100-loss2-performance.json`: 60 отсчётов за 30 секунд,
30 NPC, 1920×1080, Screen Percentage 100%, четыре процесса, 100 мс / 2% потерь.
Хост рендерит вне экрана, три клиента без рендера. i7-13700 / RTX 3090 / 32 ГБ RAM.
Замер выполнен до заключительного уточнения оттенка материала; игровой код тот же.

| Метрика | Среднее | 95-й процентиль | Максимум |
|---|---:|---:|---:|
| Game Thread, мс | 2,20 | 2,66 | 2,86 |
| Render Thread, мс | 2,26 | 2,59 | 2,79 |
| GPU, мс | 4,76 | 6,30 | 10,37 |
| Входящий трафик хоста, байт/с | 11 135 | 11 339 | 11 376 |
| Исходящий трафик хоста, байт/с | 29 125 | 42 611 | 48 196 |

Это короткий замер на RTX 3090, не подтверждение 60 FPS на среднем ПК.
Ограничитель 60 FPS включён; `frameMs` не используется вместо времени CPU/GPU.

## Повторение

Из корня репозитория в PowerShell, после `Scripts/Package.ps1`:

```powershell
$diag="$PWD/Artifacts/Windows/PersonaTrial/.work/Diagnostics"
$shots="$PWD/Artifacts/Windows/PersonaTrial/Artifacts/Previews"
Scripts/RunPackagedLocal.ps1 -Players 4 -Diagnostics -RenderHost -NetworkEmulation
Scripts/TestGameplay.ps1 -ReportName phase2-packaged-gameplay -DiagnosticsRoot $diag -PreviewRoot $shots
Scripts/TestLocal.ps1 -ReportName phase2-packaged-network-lag100-loss2 -PerfSeconds 30 -ExpectedPacketLag 100 -ExpectedPacketLoss 2 -DiagnosticsRoot $diag -PreviewRoot $shots
Scripts/TestSessions.ps1 -ReportName phase2-packaged-sessions -DiagnosticsRoot $diag
Scripts/StopLocal.ps1
Scripts/RunPackagedLocal.ps1 -Players 2 -Menu -Diagnostics -RenderHost
Scripts/TestSectors.ps1 -ReportName phase2-packaged-sectors -DiagnosticsRoot $diag -PreviewRoot $shots
Scripts/TestColdSector.ps1
Scripts/StopLocal.ps1
```

Диагностика включается только явно через `-PersonaDiagnostics=Player0` в Development/Editor.
В Shipping она отключена. Тестовые сохранения отделены от обычных игровых секторов.

## Пока не проверено

Steam-подключение, приглашения и пространственный VOIP на двух ПК с разными аккаунтами.
Нужны две сборки 0.2.0, Steam и App ID 480. Проверить V, направление звука, затухание до тишины
на 25 м, mute, громкость и восстановление голоса после смены карты/переподключения.
Локальные Null/IP-процессы не являются проверкой Steam или микрофона.

Настройки, профиль, обучение и статистика в главном меню пока являются недоступными пунктами.
