# Changelog

## 0.1.1

* Исправлено: в FL Studio скрипты не находили общий модуль
  (`ModuleNotFoundError: No module named 'adaptive_follow'`). По официальной документации
  FL добавляет в путь поиска модулей папку самого скрипта, поэтому установщик теперь кладёт
  копию `adaptive_follow` рядом с device script и Piano Roll script; `%APPDATA%` остаётся
  запасным вариантом. При ошибке импорта скрипты перечисляют, где искали модуль.

## 0.1.0 — MVP (не выпущено)

Первая версия отдельного модуля Adaptive Follow для FL Studio (Windows).

* Piano Roll script **Adaptive Follow - Export Reference**: экспорт нот (только чтение)
  в JSON-референс; фильтр «все ноты / один цвет», минимальная velocity, окно аккорда,
  приглушённые ноты; понятные сообщения, обработка пустого выбора, дубликатов и аккордов.
* MIDI device script **AdaptiveFollow**: следование за исполнителем; ноты не «съедаются»;
  Arm → старт по первой уверенной ноте → плавная скорость (и темп при `tempo_control`);
  WAITING (торможение/пауза), LOST (возврат к 1.0), RECOVERING; восстановление при
  Stop/Disarm/выгрузке; dry-run по умолчанию; консольные команды `af_*` и `command.txt`.
* Общее ядро `shared/adaptive_follow` (без FL): score follower, оценка темпа и контроллер
  транспорта, перенесённые из Adaptive Song Mode Cadenza; состояния IDLE/ARMED/TRACKING/
  WAITING/LOST/RECOVERING/STOPPED; диагностика с ротацией лога и отчётом; хранилище в
  `%APPDATA%\AdaptiveFollow`.
* Инструменты: `install_windows.ps1` / `uninstall_windows.ps1` (dry-run, резервные копии,
  манифест), `validate_install.py`, `simulate_session.py`, `afctl.py`, `check_fl_api.py`.
* Тесты (pytest, без FL): референс и JSON, фильтры, аккорды, арпеджио, ошибки тайминга,
  пропущенные/лишние/повторяющиеся ноты, потеря и повторный захват, оценка темпа,
  ограничения скорости и темпа, восстановление транспорта, отсутствующий и повреждённый
  референс, контракт device/Piano Roll скриптов с поддельными модулями FL.
