# Adaptive Follow для FL Studio (Windows, MVP)

**Adaptive Follow** — отдельный модуль, который заставляет транспорт FL Studio
(скорость воспроизведения, при желании — темп) **следовать за живым
исполнителем**. Вы играете свою партию на MIDI-клавиатуре, а аранжировка в FL
Studio ускоряется, замедляется и ждёт вас.

Модуль полностью независим от Cadenza: он лежит в своей папке
`flstudio-adaptive-follow/`, ничего не импортирует из Cadenza и не меняет её
файлы. Из Cadenza перенесены (переписаны на Python) только алгоритмы
следования — см. [ARCHITECTURE.md](ARCHITECTURE.md#происхождение-из-cadenza).

Он состоит из двух скриптов FL Studio. Это **разные API и разные контексты**:

| Часть | Где работает | Что делает |
|---|---|---|
| `piano_roll_script/Adaptive Follow - Export Reference.pyscript` | Piano Roll, API `flpianoroll` | Один раз **читает** ноты вашей партии (не меняя их) и сохраняет «референс» в JSON |
| `midi_device_script/device_AdaptiveFollow/device_AdaptiveFollow.py` | MIDI-скрипт контроллера, API `transport`/`mixer`/`general`/`midi`/`ui` | Слушает клавиатуру, сравнивает ноты с референсом, управляет транспортом |
| `shared/adaptive_follow/` | общий чистый Python (без FL) | модель, референс, score follower, оценка темпа, контроллер транспорта, состояния, диагностика, хранилище |

## Первый запуск (10 шагов)

1. **Установите** модуль (подробно: [INSTALL_WINDOWS.md](INSTALL_WINDOWS.md)):
   `powershell -ExecutionPolicy Bypass -File tools\install_windows.ps1 -DryRun`, затем то же без `-DryRun`.
2. **Сделайте копию** вашего проекта `.flp` и работайте с копией.
3. Перезапустите FL Studio. **OPTIONS > MIDI settings**: выберите вашу клавиатуру во
   входах (Input), включите **Enable**, в **Controller type** выберите **AdaptiveFollow**.
4. Откройте **VIEW > Script output** — на вкладке AdaptiveFollow должно быть
   `Adaptive Follow 0.1.1 ready. Reference: NONE (export one first). Dry-run: True.`
5. Откройте партию, которую будете играть, в **Piano Roll**. Снимите выделение
   (Ctrl+D) — иначе экспортируются только выделенные ноты.
6. В меню скриптов Piano Roll запустите **Adaptive Follow - Export Reference**,
   задайте имя, фильтр (все ноты или один цвет), окно аккорда → **Accept**. Появится
   сообщение «exported N notes (M events)» и путь к файлу.
7. Поставьте курсор песни на начало партии (по умолчанию тик 0 референса = тик 0 песни;
   см. `offset_mode`/`offset_ticks` в настройках), заглушите (mute) экспортированную
   партию, если не хотите слышать её дважды.
8. В консоли Script output наберите `af_reload()`, затем `af_arm()`.
   Состояние станет **ARMED**. Сыграйте первые ноты: в режиме **dry-run** (по умолчанию)
   транспорт не трогается, а в консоли/логе видно, что модуль *сделал бы*
   (`[dry-run] would start …`, `af_status()`).
9. Когда поведение устраивает: `af_dry_run(False)`, затем `af_arm()` и играйте —
   воспроизведение стартует по первой уверенно распознанной ноте, скорость плавно
   подстраивается, при вашей паузе FL притормаживает и ждёт.
10. `af_disarm()` (или Stop в FL) — слежение выключается, скорость возвращается к 1.0,
    темп — к исходному. Если что-то пошло не так: `af_report()` и приложите отчёт
    (см. [INSTALL_WINDOWS.md](INSTALL_WINDOWS.md#диагностика-и-отчёт)).

Управлять можно и из обычной командной строки (без консоли FL):
`python tools\afctl.py status | arm | disarm | dry-run off | report | list | set KEY VALUE`.

## Что умеет MVP

* Состояния: **IDLE → ARMED → TRACKING ⇄ WAITING → LOST → RECOVERING → TRACKING**, **STOPPED**.
* Распознаёт одиночные ноты, аккорды (в любом порядке и с разбросом), арпеджио,
  повторяющиеся ноты; переносит небольшие ошибки ритма, пропущенные, лишние и
  неверные ноты; после потери находит вас снова (глобальный поиск + подтверждение).
* Стратегии: `speed_only` (по умолчанию, только `setPlaybackSpeed`), `tempo_only`
  и `hybrid` (меняют темп проекта **только** если включён `tempo_control`).
* Безопасность: dry-run по умолчанию; без перемотки (`allow_seek=false`); скорость
  и темп ограничены и сглажены; LOST — плавный возврат к скорости 1.0; Stop/Disarm/
  выгрузка скрипта — восстановление скорости 1.0 и исходного темпа; ноты клавиатуры
  **никогда не «съедаются»** — инструмент играет как обычно.
* Диагностика: лог с ротацией, уровни off/error/info/debug, отчёт `af_report()`.
* Всё настраивается в `%APPDATA%\AdaptiveFollow\config.json`
  (пример с комментариями: [config/config.example.json](config/config.example.json)).

## Ограничения MVP

* **Piano Roll script и MIDI device script — разные API и разные контексты.** Они
  не видят друг друга напрямую и обмениваются только файлом референса.
* **Референс не обновляется автоматически.** Изменили партию — экспортируйте снова,
  затем `af_reload()` (при выключенном слежении).
* **Тестируйте на копии .flp.** Модуль меняет скорость воспроизведения и (если
  разрешено) темп проекта.
* **Поведение транспорта и темпа может зависеть от версии FL Studio** (см. список
  проверок в [INSTALL_WINDOWS.md](INSTALL_WINDOWS.md#что-проверить-в-вашей-версии-fl-studio)).
  Минимальная скорость FL — 0.25×, поэтому «полная» остановка — это пауза.
* **Один референс на сессию** (последний экспортированный или `reference_name`).
* Задержка реакции определяется частотой `OnIdle` FL Studio (≈20 мс) и тем, что FL
  не передаёт скриптам временные метки MIDI (используется время прихода события).
* **Это не замена нативному VST3/плагину** с собственным аудиодвижком: модуль лишь
  управляет транспортом FL через официальный скриптовый API.
* **Cadenza не изменена** и продолжает работать как раньше.

## Проверка без FL Studio

```powershell
python -m venv .venv; .venv\Scripts\pip install -r requirements-dev.txt
.venv\Scripts\python -m pytest -q                       # тесты (без FL)
.venv\Scripts\python tools\simulate_session.py -v       # симулятор сессии
.venv\Scripts\python tools\check_fl_api.py              # только документированный API FL
```

## Файлы

* [INSTALL_WINDOWS.md](INSTALL_WINDOWS.md) — установка, настройка FL, отладка, удаление.
* [ARCHITECTURE.md](ARCHITECTURE.md) — поток данных, формат JSON, машина состояний, API FL.
* [CHANGELOG.md](CHANGELOG.md), [docs/EXAMPLE_REPORT.txt](docs/EXAMPLE_REPORT.txt) — пример отчёта.
* `tools/` — `install_windows.ps1`, `uninstall_windows.ps1`, `validate_install.py`,
  `simulate_session.py`, `afctl.py`, `check_fl_api.py`.
