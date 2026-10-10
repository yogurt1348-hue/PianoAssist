# Архитектура Adaptive Follow

## Поток данных

```
 FL Studio: Piano Roll > File > Export as MIDI file        ...\Settings\Hardware\AdaptiveFollow\data
            │ part.mid                                      (MIDI-скрипт может читать/писать только здесь)
            ▼                                              ┌──────────────────────────┐
 tools\midi_to_reference.py (обычный Python, вне FL) ────► │ references\<имя>.json    │
   midifile.py → build_reference_from_notes   JSON          │ last_reference.txt       │
                                                           │ config.json              │
 (Piano Roll script: тот же JSON, если FL разрешает ему     │ logs\, reports\          │
  запись; в FL с API 40 — нет, отдельный интерпретатор)    └────────────┬─────────────┘
 MIDI-клавиатура                                └────────────┬─────────────┘
      │ Note On/Off (не помечаются handled →                 │ загрузка при
      │ FL играет инструмент как обычно)                     │ OnInit / af_reload
      ▼                                                      ▼
 ┌──────────────────────────── device_AdaptiveFollow.py (MIDI device script) ───────┐
 │ OnMidiMsg ─► session.on_note(pitch, vel, channel, время прихода perf_counter)    │
 │ OnIdle (~20 мс) ─► session.tick(): follower.update → state → controller.step     │
 │                                                                                  │
 │  shared/adaptive_follow (чистый Python, без FL):                                 │
 │   ScoreFollower ──snapshot──► TransportController ──Decision──► FLTransportPort  │
 │   (позиция, уверенность,      (PLL по измеренному      (speed / tempo /           │
 │    темп исполнителя)           playhead FL)              play-pause / seek)       │
 └──────────────────────────────────────────────────────────────────────┬──────────┘
                                                                        ▼
                                 transport.setPlaybackSpeed / start / setSongPos,
                                 general.processRECEvent(REC_Tempo) [tempo_control]
```

Песочница FL Studio (проверено в FL с MIDI scripting API 40): Piano Roll script и MIDI
device script работают в **разных интерпретаторах Python**, а `open()` вне разрешённых папок
даёт `SystemError("<class '_io.FileIO'> returned NULL without setting an exception")`.
MIDI-скрипту разрешена `...\Hardware\AdaptiveFollow\data`; поэтому референс создаётся
внешним конвертером. `Storage.select` выбирает первую папку, где запись работает, и ищет
референсы во всех; если запись невозможна нигде, MIDI-скрипт работает в памяти.

Обратная связь: каждые ~20 мс читается реальная позиция FL
(`transport.getSongPos(2)`, абсолютные тики), и контроллер исправляет расхождение
между позицией FL и позицией исполнителя (а не просто «выставляет темп»).

## Модули `shared/adaptive_follow`

| Модуль | Назначение |
|---|---|
| `model.py` | настройки `Config` (валидация, значения по умолчанию, `from_dict` никогда не падает), константы стратегий |
| `reference.py` | формат референса, группировка нот в события (аккорды), валидация JSON, `build_reference_from_notes` для Piano Roll |
| `score_follower.py` | онлайн-выравнивание живых нот с референсом, уверенность, потеря и повторный захват |
| `tempo_estimator.py` | темп исполнителя как *rate* относительно темпа проекта |
| `adaptive_transport.py` | контроллер транспорта: speed / tempo / пауза / перемотка, ограничения, восстановление |
| `state.py` | публичные состояния и журнал переходов |
| `session.py` | связывает всё: фильтры MIDI, Arm/Disarm, цикл `tick`, dry-run, команды, отчёт |
| `diagnostics.py` | лог с уровнями и ротацией, кольцевой буфер, отчёт |
| `storage.py` | папки данных (первая, где FL разрешает запись; чтение — из всех), безопасные имена файлов Windows, атомарная запись, config/command |
| `midifile.py` | чтение Standard MIDI File (формат 0/1) для конвертера `tools/midi_to_reference.py` |
| `compat.py` | запасные реализации на случай урезанной стандартной библиотеки во встроенном Python FL |
| `simulation.py` | виртуальный транспорт FL и синтетический исполнитель (тесты и симулятор) |

Ядро не импортирует модули FL Studio; FL-специфика собрана в `FLTransportPort`
в device script. Синтаксис — Python 3.6+ (проверяется тестом).

## Формат референса (JSON, schema_version 1)

```json
{
  "schema": "adaptive-follow.reference",
  "schema_version": 1,
  "name": "Verse",
  "ppq": 96,
  "chord_window_ticks": 6,
  "meta": {
    "exporter": "Adaptive Follow Piano Roll script 0.1.0",
    "core": "0.1.0", "source": "FL Studio Piano Roll", "notes_read": 3,
    "time_signature": [4, 4],
    "filter": {"mode": "all", "color": null, "min_velocity": 0.0, "include_muted": false}
  },
  "stats": {"note_count": 3, "event_count": 2, "first_tick": 0, "end_tick": 144},
  "notes": [
    {"tick": 0,  "length": 96, "number": 60, "velocity": 0.8, "color": 0, "group": 0},
    {"tick": 3,  "length": 96, "number": 64, "velocity": 0.8, "color": 0, "group": 0},
    {"tick": 96, "length": 48, "number": 67, "velocity": 0.8, "color": 0, "group": 0}
  ]
}
```

* `tick`/`length` — тики Piano Roll при `ppq` (`score.PPQ`); `number` — MIDI-нота 0–127;
  `velocity` 0–1, `color` 0–15, `group` — как в `flpianoroll.Note`.
* События строятся при загрузке: ноты, начавшиеся в пределах `chord_window_ticks` от
  **первой** ноты события, — один аккорд (окно не «ползёт» по арпеджио). Одинаковые
  ноты на одном тике схлопываются (счётчик `duplicates` в отчёте экспорта).
* Валидация при загрузке: схема и версия, `ppq` 1..`MAX_PPQ`, диапазоны полей; плохие
  ноты пропускаются с предупреждением, нечитаемый JSON → `ReferenceFormatError
  ("corrupted reference file …")`, отсутствующий файл → `ReferenceMissingError`
  с подсказкой. В обоих случаях скрипт не падает, FL работает как обычно.
* Экспортёр никогда не меняет ноты; пустой результат (нет нот / всё отфильтровано)
  не создаёт файл и объясняет причину.

## Машина состояний

```
            af_arm()                первая уверенная нота
   IDLE ───────────────► ARMED ───────────────────────────► TRACKING ◄──────────┐
    ▲                      │                                  │  ▲              │
    │                      │                 нет ноты дольше  │  │ исполнитель  │ повторный захват
    │                      │                 допуска          ▼  │ продолжил    │ подтверждён
    │                      │                               WAITING              │ (N нот)
    │                      │                                  │                 │
    │                      │          N промахов подряд /     ▼                 │
    │                      │          низкая уверенность    LOST ──────► RECOVERING
    │                      ▼                                     глобальный поиск
    └────────────────── STOPPED  ◄── af_disarm() / Stop в FL / OnDeInit / смена проекта
                        (скорость 1.0, исходный темп)       (из любого активного состояния)
```

| Состояние | Транспорт |
|---|---|
| IDLE | ничего не трогается |
| ARMED | ждём первую ноту; если FL уже играет (вступление) — нормальная скорость, пауза у места вступления (`wait_behavior`) |
| TRACKING | скорость = темп исполнителя + ограниченная поправка фазы; низкая уверенность → слабее поправка («uncertain» внутри follower) |
| WAITING | торможение к ожидаемому событию; `pause` — пауза, если опоздание > `pause_grace_s`; `slow` — минимальная скорость; `continue` — без изменений |
| LOST | `continue` — плавный возврат к скорости 1.0 (и снятие своей паузы); `pause` — пауза |
| RECOVERING | слабая поправка, ждём подтверждения новой позиции |
| STOPPED | `restore()`: скорость 1.0, исходный темп |

## Контроллер транспорта (PLL)

```
цель     P* = anchor_tick + (now − anchor_live) · rate / spt     (не дальше hold + tolerance)
ошибка   e  = (P* − playhead) · spt                               (секунды)
команда  r  = rate + clamp(gain · e / T, ± max_corr · rate)
```

* `rate` — темп исполнителя относительно проекта (feed-forward), поправка фазы
  ограничена `max_phase_correction` (15 %), при отставании > 0.25 с —
  `catchup_max_correction` (35 %), всё в пределах `speed_min..speed_max` и 0.25..4.0 (FL).
* Скорость меняется не быстрее `speed_slew_per_s`; торможение у точки ожидания и его
  снятие — быстрее (4/с), чтобы не проезжать ожидаемую ноту. `setPlaybackSpeed`
  вызывается не чаще ~10 раз/с (кроме крупных изменений).
* `speed_only` — только скорость. `tempo_only` — темп проекта = base × r (с шагом не
  меньше `tempo_update_min_bpm`, не чаще 4 раз/с). `hybrid` — темп медленно следует
  за исполнителем (`tempo_smoothing_s`), быстрые поправки — скоростью.
  Темп меняется **только** при `tempo_control: true`.
* Перемотка (`setSongPos`) — только при `allow_seek: true`, после подтверждённого
  повторного захвата, с уверенностью ≥ `seek_min_confidence` и расхождением ≥
  `seek_threshold_beats`, и не чаще одного раза на захват.
* Dry-run: все решения вычисляются и пишутся в лог (`[dry-run] would …`), FL не трогается.

## API FL Studio и запасные варианты

Источник: официальные стабы Image-Line [IL-Group/FL-Studio-API-Stubs](https://github.com/IL-Group/FL-Studio-API-Stubs)
(сайт документации был недоступен из среды разработки; стабы — тот же источник).
`tools/check_fl_api.py` проверяет mypy-ом, что скрипты вызывают только описанное в стабах.

| Вызов | Документация | Использование / запасной вариант |
|---|---|---|
| `OnInit`, `OnDeInit`, `OnMidiMsg`, `OnIdle` | callbacks MIDI-скрипта | старт/выгрузка, ноты, цикл (~20 мс) |
| `OnProjectLoad(status)` | API 16 | при загрузке другого проекта — Disarm; в старых версиях просто не вызывается |
| `FlMidiMsg.status/data1/data2/handled` | | ноты не помечаются `handled`; только назначенные CC |
| `FlMidiMsg.timestamp` | «по-видимому всегда 0» | не используется → время прихода `time.perf_counter()` |
| `transport.isPlaying()`, `start()` (play/pause) | API 1 | старт / пауза / продолжение |
| `transport.getSongPos(2)` | mode с API 3 (2 = абсолютные тики) | позиция FL |
| `transport.setSongPos(t, 2)` | mode с API 4 | только при `allow_seek`; при API < 4 перемотка отключается |
| `transport.setPlaybackSpeed(0.25..4)` | API 1, геттера нет | скорость; текущая хранится у себя |
| `mixer.getCurrentTempo()` | | базовый темп при Arm (нормализация, если вернётся BPM×1000) |
| `general.processRECEvent(REC_Tempo, bpm·1000, REC_Control\|REC_UpdateControl)` | функции `setCurrentTempo` в API нет | смена темпа, только при `tempo_control` |
| `general.getRecPPQ()` | API 8 | PPQ проекта; при API < 8 — PPQ референса |
| `general.getVersion()` | | версия API для отключения недоступных функций |
| `ui.setHintMsg()` | | подсказки (можно выключить `hint_messages`) |
| `flpianoroll.score.PPQ/tsnum/tsden/noteCount/getNote(i)`, `Note.*` | Piano Roll API | только чтение |
| `flpianoroll.ScriptDialog` (`AddInputText/Combo/KnobInt/Checkbox`, `GetInputValue`), `Utils.ShowMessage/ProgressMsg` | в стабах IL-Group с 09.2024 (в pip 37.0.1 — ещё нет) | диалог экспорта |
| структура `createDialog()` / `apply(form)` | **в стабах не описана** | как в фабричных скриптах FL — проверить в FL |

Все вызовы FL обёрнуты: исключение (в т.ч. `TypeError("Operation unsafe at current time")`)
пишется в лог, скрипт продолжает работать; при ошибке смены темпа или перемотки эта
функция отключается до конца сессии (видно в отчёте: `disabled features`).

## Происхождение из Cadenza

Алгоритмы перенесены из Adaptive Song Mode Cadenza (`Source/Adaptive/*`, C++) и
переписаны на Python; файлы Cadenza не изменялись и не импортируются.

| Adaptive Follow | Cadenza | Отличия |
|---|---|---|
| `score_follower.py` | `ScoreFollower.cpp` | тот же оконный DP (аккорды, пропуски, лишние ноты, шаг назад, тайминг); глобальный поиск при потере — по кандидатам с той же высотой + короткое выравнивание назад вместо полного Smith-Waterman (скорость в однопоточном цикле FL) |
| `tempo_estimator.py` | `TempoEstimator.cpp` | без изменений по смыслу (взвешенный МНК, паузы, выбросы, EMA, ограничения) |
| `adaptive_transport.py` | `AdaptiveTransport.cpp` | в Cadenza контроллер сам владеет позицией; здесь позиция у FL → замкнутый контур по измеренному playhead, ограниченная скорость FL (≥ 0.25×) → пауза вместо полной остановки, стратегии speed/tempo/hybrid |
| `reference.py` | `ReferencePerformance.cpp`, `PitchMask.h` | источник — Piano Roll, а не MIDI-файл; JSON вместо внутреннего формата |
| `session.py`, `state.py` | `AdaptiveSongSession.cpp` | однопоточный (FL), без triple buffer; публичные состояния по ТЗ |
