# Сборка и выпуск

Текущая версия — 1.1.1. VERSION, PackageVersion в installer/QIP2005.iss и ресурсы EXE должны совпадать. Windows file version — 1.1.1.0, релизный тег — v1.1.1.

## Локальная сборка

Нужны Windows 11 x64, Python с scripts/requirements-ci.txt, w64devkit 2.9.1 и официальный Inno Setup 6.7.3. scripts/acquire-toolchain.ps1 получает закреплённый архив компилятора, проверяет SHA-256 и извлекает его как данные.

```powershell
python -m pip install -r scripts/requirements-ci.txt
./scripts/build-agent.ps1 -ToolchainBin C:\tools\w64devkit\bin
./scripts/test.ps1 -ToolchainBin C:\tools\w64devkit\bin
./scripts/build-installer.ps1 -ISCC C:\tools\inno\ISCC.exe -SigningKeyDpapi C:\private\release-signing-key.dpapi
```

Чистый пакет хранится в private/payload и проверяется по installer/payload-manifest.json: 151 файл и точный набор каталогов. Пользовательские Users, история и сохранённые аккаунты в пакет не добавляются. Контрольная сумма исходного QIP2005_clean.7z: 0bcc0f2b0914f89af68200fd811e8c96b9f28fcbdac8ff034bdf02a81fd13a0c.

Исходный пакет уже опубликован в историческом v1.1.1. Независимое основание его распространения пока не представлено: текущий статус — в redistribution.md, новые бинарные релизы закрыты. Обычный CI выполняет сквозной тест с синтетическими пакетами и временным тестовым ключом; исходный QIP не скачивает. scripts/prepare-ci-payload.ps1 оставлен для отдельно разрешённой проверки пакета с закреплённым SHA-256. Ключ -CompileOnly у build-installer.ps1 создаёт только локальную проверочную сборку без подписи обновления. Параметр -AgentDirectory позволяет изолировать кандидат от артефактов опубликованного выпуска.

## Проверки установщика

```powershell
./scripts/test-installer.ps1 -ISCC C:\tools\inno\ISCC.exe -BaselineSourceDirectory ./tests/fixtures/v1.0.0 -WorkDirectory C:\private\qip-upgrade-test
```

Тест создаёт отдельные AppId, каталог, группу и имена ярлыков. Содержимое профилей проверяется по SHA-256 с двумя разрешёнными изменениями INI: полный HistPath и Startup=0. Точная исходная копия профиля дополнительно проверяется в резервном снимке до установки. Для локальной проверки копий реальных данных есть -RealBaseline; такие данные остаются вне Git и отчётов. Для перехода с 1.1.0 задаются -BaselineVersion 1.1.0 и каталог старого агента через -BaselineAgentDirectory.

## Подпись и локальная публикация

Публичный ключ закреплён в updater/keys/release-public.pem и EXE. Приватный RSA-3072 ключ защищён DPAPI исходной учётной записи Windows и хранится вне репозитория. Его защищённая резервная копия нужна до переноса сборки на другой компьютер. DPAPI-файл сам по себе не переносим между пользователями.

Публикация локально подписанного выпуска выполняется после явного решения владельца:

```powershell
python scripts/publish-release.py --check-auth
python scripts/publish-release.py --directory dist --publish
```

Скрипт использует GH_TOKEN/GITHUB_TOKEN либо штатный Git credential manager. Учётные данные остаются в памяти и не выводятся. До создания draft он требует проверенное независимое основание от правообладателя, SHA-256 документа разрешения и сведения о проверке, а также отдельный флаг допуска в docs/release-audit.json. Заявление владельца проекта само по себе допуск не открывает. Затем проверяются RSA-манифест, контрольные суммы, чистота дерева, совпадение с main и успешный CI того же коммита. Существующий стабильный релиз не перезаписывается.

## GitHub Actions

ci.yml собирает нативный агент и выполняет тесты при PR, push и ручном запуске. tests/test_e2e_update.py собирает реальные Inno-установщики двух версий с собственным управляемым процессом и подписанным манифестом. Проверяются отдельный worker, ожидание клиента, сохранение данных, отказ при повреждении, откат, восстановление журнала и совместимость старого worker с новым помощником. Тестовый ключ создаётся в памяти и не совпадает с релизным. Полный этап с оригинальным QIP закрыт проверкой прав. Пользовательские профили, ключи и бинарные artifacts не загружаются.

private-build.yml оставлен для локального self-hosted runner. release.yml требует ручного запуска из main с approval=PUBLISH_STABLE, проверку прав и допуска аудита, а также environment qip-release с required reviewers. Подпись выполняется только выделенным self-hosted Windows runner с меткой qip-release-signing. QIP_SIGNING_KEY_DPAPI — путь к локальному защищённому файлу, repository/environment variable не содержит ключ. Runner должен работать под исходной учётной записью DPAPI. Этот runner в рамках аудита не создавался и не проверялся.

Экспортёр секрета в GitHub удалён. Ключ не принимается через MANIFEST_SIGNING_KEY_B64. Защищённая локальная копия и ограничения восстановления описаны в signing-key-recovery.md. QIP_SOURCE_PACKAGE_URL задаёт адрес пакета только после снятия правового ограничения; его SHA-256 всё равно проверяется.

Обычные jobs имеют contents: read. Записывающие права выдаются только job публикации. Само приложение обращается к публичному GitHub API анонимно и не содержит токенов.
