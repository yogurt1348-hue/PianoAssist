# Установка Adaptive Follow на Windows

## Требования

* Windows 10/11, FL Studio с поддержкой Python-скриптов:
  * MIDI-скрипты контроллеров (MIDI Controller Scripting) — FL Studio 20.7+;
  * скрипты Piano Roll (`flpianoroll`, `ScriptDialog`) — FL Studio 21+ (официальная документация
    описывает их для FL Studio 21; в старых версиях экспортёр недоступен).
  Скрипты выполняет **встроенный Python FL Studio**; системный Python для них не нужен.
* Python **3.9+** (python.org, галочка «Add python.exe to PATH») — только для
  вспомогательных инструментов: проверки установки, симулятора, `afctl.py`, тестов.
  Без Python установщик всё равно ставит скрипты (с предупреждением).
* MIDI-клавиатура (любая; модуль не привязан к модели или производителю).
* Права администратора **не нужны**: всё ставится в папки пользователя.

## Что и куда ставится

| Что | Куда |
|---|---|
| MIDI device script | `<FL user data>\FL Studio\Settings\Hardware\AdaptiveFollow\device_AdaptiveFollow.py` |
| Piano Roll script | `<FL user data>\FL Studio\Settings\Piano roll scripts\Adaptive Follow - Export Reference.pyscript` |
| пути установки (генерируется) | `adaptive_follow_paths.py` рядом с каждым из скриптов |
| общий модуль `adaptive_follow` | `%APPDATA%\AdaptiveFollow\lib\adaptive_follow\` |
| референсы, настройки, логи, отчёты | `%APPDATA%\AdaptiveFollow\{references, config.json, logs, reports}` |
| резервные копии перезаписанных файлов | `%APPDATA%\AdaptiveFollow\backup\<дата-время>\` |
| манифест установки | `%APPDATA%\AdaptiveFollow\install-manifest.json` |
| venv для инструментов | `%LOCALAPPDATA%\AdaptiveFollow\venv` |

`<FL user data>` по умолчанию — `Документы\Image-Line` (в FL: **OPTIONS > File settings >
User data folder**). Установщик ищет `...\Image-Line\FL Studio\Settings` в «Документах»
(в том числе перенаправленных в OneDrive); если не нашёл — спрашивает путь.

## Запуск установщика

Откройте PowerShell в папке `flstudio-adaptive-follow` и выполните:

```powershell
# 1) посмотреть, что будет сделано (ничего не меняется)
powershell -ExecutionPolicy Bypass -File tools\install_windows.ps1 -DryRun

# 2) установить
powershell -ExecutionPolicy Bypass -File tools\install_windows.ps1
```

`-ExecutionPolicy Bypass` действует только на этот запуск и не меняет политику системы.

Полезные параметры:

| Параметр | Зачем |
|---|---|
| `-DryRun` | показать все действия, ничего не меняя |
| `-FLSettingsPath "D:\Мои\Image-Line\FL Studio\Settings"` | если папка FL нестандартная |
| `-DataPath "D:\AdaptiveFollow"` | другое место для данных и общего модуля |
| `-SkipVenv` | не создавать venv (Python не нужен) |
| `-WithDevTools` | поставить в venv ещё pytest, mypy и официальные стабы API FL |
| `-NonInteractive` | ничего не спрашивать (ошибка вместо вопроса) |

Установщик: проверяет Python и версию → создаёт venv → `pip install -r requirements.txt`
(сейчас внешних зависимостей нет) → находит папку FL → копирует общий модуль и создаёт
папки данных → копирует скрипты → **перед перезаписью любого файла делает резервную
копию** → `config.json` создаёт, только если его ещё нет (ваши настройки не
перезаписываются) → пишет манифест → запускает `tools\validate_install.py` → выводит
список установленного и команды отмены. Повторный запуск безопасен (обновление).

Проверить установку отдельно: `%LOCALAPPDATA%\AdaptiveFollow\venv\Scripts\python tools\validate_install.py`.

## Настройка FL Studio

### 1. Включить MIDI-клавиатуру и назначить device script

1. Перезапустите FL Studio (скрипты читаются при запуске).
2. **OPTIONS > MIDI settings** (F10 → MIDI).
3. В списке **Input** выберите вашу клавиатуру, нажмите **Enable**.
4. В **Controller type** выберите **AdaptiveFollow** (имя берётся из первой строки
   скрипта `# name=AdaptiveFollow`; в некоторых версиях рядом пишется `(user)`).
5. Убедитесь, что клавиатура по-прежнему играет выбранный инструмент: скрипт
   **не помечает ноты как обработанные**, FL обрабатывает их как обычно.

Если клавиатура уже была назначена другому скрипту, Adaptive Follow временно
заменяет его для этого входа. Вернуть: Controller type → `(generic controller)` или прежний.

### 2. Script output, консоль и режим отладки

* **VIEW > Script output** — окно вывода скриптов. Выберите вкладку **AdaptiveFollow**:
  при запуске должна быть строка `Adaptive Follow 0.1.0 ready ...`. Сюда же выводятся
  ошибки Python.
* В этом окне есть консоль: команды выполняются в контексте выбранного скрипта.

  | Команда | Действие |
  |---|---|
  | `af_help()` | справка |
  | `af_status()` | состояние, уверенность, темп исполнителя, скорость, темп |
  | `af_arm()` / `af_disarm()` / `af_toggle()` | включить / выключить слежение |
  | `af_dry_run(True)` / `af_dry_run(False)` | безопасный режим вкл/выкл |
  | `af_reload()` | перечитать настройки и референс (при выключенном слежении) |
  | `af_report()` | записать диагностический отчёт |
  | `af_set("midi_channel", 2)` | изменить настройку (сохраняется в config.json) |

* Если консоль недоступна в вашей версии — те же команды из обычной командной строки:
  `python tools\afctl.py status`, `... arm`, `... dry-run off`, `... report`, `... set follow_strategy hybrid`
  (скрипт читает `%APPDATA%\AdaptiveFollow\command.txt` каждые 0.5 с).
* **Подробный лог**: `af_set("log_level", "debug")` (или `"log_level": "debug"` в
  `config.json`). В режиме debug сообщения дублируются и в Script output.
* **Перезагрузка скрипта** после обновления: перезапуск FL Studio либо повторный выбор
  Controller type (в новых версиях в Script output есть кнопка перезагрузки скрипта).

### 3. Найти и запустить скрипт в Piano Roll

1. Откройте нужную партию в Piano Roll.
2. **Снимите выделение (Ctrl+D)**: скрипты Piano Roll получают выделенные ноты, а если
   ничего не выделено — все ноты. Выделение используйте, если нужно экспортировать часть.
3. Откройте меню скриптов Piano Roll (FL 21+: меню Piano Roll **Tools > Scripting**;
   в разных версиях пункт может называться немного иначе) и выберите
   **Adaptive Follow - Export Reference**.
4. В диалоге: **Reference name**, **Notes** (All notes / Only note color 1–16),
   **Min velocity %**, **Chord window (ticks)** (ноты, начавшиеся в пределах окна,
   считаются одним аккордом), **Include muted notes** → **Accept**.
5. Сообщение покажет число нот/событий, путь к файлу и предупреждения (дубликаты,
   отфильтрованные ноты). Ноты в Piano Roll **не изменяются**.

Скрипт не нашёлся в меню → проверьте, что файл лежит в
`...\FL Studio\Settings\Piano roll scripts\`, и перезапустите FL.

## Что проверить в вашей версии FL Studio

Всё ниже используется строго по официальной документации/стабам Image-Line, но
поведение в конкретной версии стоит один раз проверить на копии проекта:

1. **Структура Piano Roll скрипта** `createDialog()` + `apply(form)`: диалог открывается,
   экспорт выполняется по Accept. (Эти имена функций соответствуют фабричным скриптам FL,
   но в официальных стабах не описаны.)
2. **Выделение vs все ноты** в Piano Roll (см. выше).
3. `transport.start()` при игре ставит **паузу** (а не стоп), повторный вызов продолжает.
4. `transport.setPlaybackSpeed()` меняет скорость; как при этом ведут себя **аудиоклипы**,
   зависит от их режима растяжения (stretch) — для MIDI/инструментов это не важно.
5. `mixer.getCurrentTempo()` возвращает BPM (если версия вернёт BPM×1000, модуль это
   нормализует — проверьте в `af_status()`, что base tempo совпадает с темпом проекта).
6. Изменение темпа через `general.processRECEvent(REC_Tempo, …)` (только `tempo_control: true`)
   и его восстановление после `af_disarm()`.
7. `transport.getSongPos(2)` (абсолютные тики) в режиме **Song**; в режиме **Pattern** позиция
   отсчитывается от начала паттерна — используйте `offset_mode: "playhead"` или Song.
8. Импорт `adaptive_follow_paths` из папки скрипта (иначе используется запасной путь
   `%APPDATA%\AdaptiveFollow\lib` — тоже работает, если данные лежат там).

## Диагностика и отчёт

* Лог: `%APPDATA%\AdaptiveFollow\logs\adaptive_follow.log` (ротация в `.log.1` при
  достижении `log_max_kb`, по умолчанию 512 КБ). В нём: время, MIDI-событие, найденное
  событие референса, уверенность, состояние, темп исполнителя, позиция песни, целевая
  позиция, скорость, темп, причины переходов, предупреждения.
* Отчёт: `af_report()` или `python tools\afctl.py report` → файл
  `%APPDATA%\AdaptiveFollow\reports\report-latest.txt` (настройки, состояние, последние
  переходы и последние строки лога). Пример: [docs/EXAMPLE_REPORT.txt](docs/EXAMPLE_REPORT.txt).
* **Как приложить**: воспроизведите проблему с `log_level: "debug"`, сразу выполните
  `af_report()`, приложите `report-latest.txt` (и при необходимости `adaptive_follow.log`)
  к issue/сообщению, укажите версию FL Studio (Help > About) и что вы ожидали.
  Отчёт содержит пути вашего профиля Windows — при желании замените имя пользователя.

## Частые проблемы

| Симптом | Что сделать |
|---|---|
| `cannot import the shared adaptive_follow module` | перезапустите установщик; проверьте `validate_install.py` |
| `Reference: NONE` | экспортируйте референс из Piano Roll, затем `af_reload()` |
| `reference file ... is corrupted` | экспортируйте заново; повреждённый файл не загружается, FL работает как обычно |
| ноты не распознаются | `af_status()`; проверьте `midi_channel`, `min_velocity`, что играете ту же партию и октаву |
| FL «не двигается» | включён dry-run (по умолчанию) — `af_dry_run(False)`; или `transport_control: false` |
| темп не меняется | так и задумано, пока `tempo_control: false` (стратегия `speed_only`) |

## Удаление

```powershell
# посмотреть, что будет удалено
powershell -ExecutionPolicy Bypass -File tools\uninstall_windows.ps1 -DryRun
# удалить скрипты, общий модуль и venv; референсы/настройки/логи СОХРАНИТЬ
powershell -ExecutionPolicy Bypass -File tools\uninstall_windows.ps1 -KeepData
# удалить всё, включая данные
powershell -ExecutionPolicy Bypass -File tools\uninstall_windows.ps1 -RemoveData
# вернуть файлы, которые установщик перезаписал (из backup\)
powershell -ExecutionPolicy Bypass -File tools\uninstall_windows.ps1 -RestoreBackups
```

Без `-KeepData`/`-RemoveData` деинсталлятор спросит, удалять ли данные (по умолчанию — нет).
Удаляются **только** файлы из манифеста установки; чужие скрипты в папках FL не
трогаются, папки удаляются, только если стали пустыми. После удаления в
**OPTIONS > MIDI settings** верните клавиатуре Controller type `(generic controller)`.
