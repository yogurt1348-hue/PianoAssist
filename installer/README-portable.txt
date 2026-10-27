PianoAssist — портативная версия / portable version
====================================================

Запуск
  Распакуйте архив в папку, где можно записывать файлы (например, «Документы»
  или флешка), и запустите PianoAssist.exe. Устанавливать ничего не нужно.
  Не распаковывайте в C:\Program Files — туда обычной программе писать нельзя.

Где данные
  Файл PianoAssist.portable рядом с PianoAssist.exe включает портативный режим:
  настройки, журналы и библиотека композиций хранятся в папке Data рядом с exe.
  Установленная версия (setup.exe) хранит данные отдельно, в %APPDATA%\PianoAssist,
  поэтому две версии не смешивают данные.
  Если в папку Data записать нельзя, PianoAssist предложит выбрать другую папку
  на этот запуск; молча в профиль пользователя он не переключается.
  Папку PianoAssist можно перемещать целиком: пути к песням внутри Data
  хранятся относительными.

Обновление
  1. Закройте PianoAssist.
  2. Распакуйте новый архив во временную папку.
  3. Скопируйте из неё PianoAssist.exe и папку licenses поверх старых,
     заменив файлы. Папку Data НЕ трогайте и не удаляйте.
  4. Запустите PianoAssist.exe. Настройки и песни на месте.
  Проверить, есть ли новая версия: Помощь > Проверить обновления.

Удаление
  Удалите папку целиком. Если хотите сохранить песни и настройки,
  сначала скопируйте папку Data.

Звуки
  В архиве нет VST3-плагинов и SoundFont-файлов. Встроенный звук работает
  сразу; свои инструменты подключаются в Настройки > Звуки.

Предупреждение Windows
  Сборка пока не подписана, поэтому SmartScreen может показать «Windows
  защитила ваш компьютер». Сверьте SHA-256 архива со строкой из SHA256SUMS.txt
  на странице выпуска: Get-FileHash -Algorithm SHA256 <файл>.
  Не отключайте защиту Windows и не добавляйте исключения.

----------------------------------------------------------------------

Run: unzip into a writable folder (not Program Files) and start PianoAssist.exe.
Data: PianoAssist.portable next to the exe keeps settings, logs and songs in
  Data\ beside it; the installed version keeps its own data in %APPDATA%.
Update: close PianoAssist, copy the new PianoAssist.exe and licenses over the
  old ones, keep the Data folder.
Remove: delete the folder (copy Data first to keep your songs).
