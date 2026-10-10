# device_AdaptiveFollow — MIDI device script

Скрипт контроллера FL Studio (MIDI Controller Scripting API). Устанавливается в
`<FL user data>\FL Studio\Settings\Hardware\AdaptiveFollow\` вместе с
`adaptive_follow_paths.py` (его пишет установщик). Отдельный `.json` для FL не нужен:
имя контроллера задаётся первой строкой `# name=AdaptiveFollow`.

* Назначение: OPTIONS > MIDI settings → вход клавиатуры → Enable → Controller type
  **AdaptiveFollow**.
* Ноты **не помечаются как обработанные** (`handled`), поэтому клавиатура продолжает
  играть выбранный инструмент. Потребляются только CC, явно назначенные в настройках
  (`control_cc_arm_toggle`, `control_cc_dry_run_toggle`, `control_cc_reset`).
* Callbacks: `OnInit` (загрузка настроек и референса), `OnMidiMsg` (ноты/CC),
  `OnIdle` (цикл управления, команды из `command.txt`, перечитывание `config.json`),
  `OnProjectLoad` (Disarm при смене проекта), `OnDeInit` (восстановление скорости и темпа).
* Консоль (VIEW > Script output): `af_help()`, `af_status()`, `af_arm()`, `af_disarm()`,
  `af_toggle()`, `af_dry_run(True/False)`, `af_reload()`, `af_report()`, `af_set(key, value)`.
* Если общий модуль не найден или референс отсутствует/повреждён, скрипт пишет понятное
  сообщение в Script output и ничего не трогает в FL.

Используемый API и запасные варианты — в [../../ARCHITECTURE.md](../../ARCHITECTURE.md#api-fl-studio-и-запасные-варианты).
Логика — в общем модуле `shared/adaptive_follow` (тестируется без FL).
