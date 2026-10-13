# Сторонние компоненты

| Компонент | Использование | Условия и уведомления |
|---|---|---|
| QIP 2005 и его модификация, docking, скины, звуки, локализация | Только локальный private payload | Разрешение публичного распространения не установлено; исключены из MIT и Git |
| nlohmann/json 3.12.0 | Vendored C++ header, встроен в наш EXE | MIT, Copyright 2013–2025 Niels Lohmann; оригинальный текст в `updater/vendor/LICENSE.nlohmann-json` |
| MinGW-w64 runtime | Статическая часть нативного EXE | Оригинальные уведомления в `licenses/MinGW-w64-runtime.txt` |
| GCC libstdc++/runtime | Статическая часть нативного EXE | GPLv3 и GCC Runtime Library Exception 3.1; оба текста в `licenses` |
| Inno Setup 6.7.3 | Сборка и bootstrap/deinstaller | Отдельная лицензия разработчиков в `licenses/Inno-Setup.txt` |
| WinHTTP, BCrypt, Crypt32 и другие Win32 DLL | Системные компоненты Windows 11 | Не распространяются с приложением |
| Python cryptography | Только сборка и тесты | Не включён в клиентский дистрибутив |

Уведомления для нашего native runtime также устанавливаются в `{app}\Licenses`. Лицензия корня не присваивает права на эти сторонние части.
