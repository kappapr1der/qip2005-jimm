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

Публичный исходный пакет прикладывается к v1.1.1. scripts/prepare-ci-payload.ps1 получает этот файл, проверяет SHA-256, извлекает его, проверяет содержимое и готовит официальный Inno Setup с проверенной Authenticode-подписью. Ключ -CompileOnly у build-installer.ps1 предназначен для CI без секрета: результат не является подписанным обновлением и не публикуется.

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

Скрипт использует GH_TOKEN/GITHUB_TOKEN либо штатный Git credential manager. Учётные данные остаются в памяти и не выводятся. Перед созданием draft он проверяет владельческое подтверждение распространения, RSA-манифест, контрольные суммы, чистоту исходного дерева, совпадение с main и успешный CI того же коммита. После загрузки проверяет контрольные суммы файлов на GitHub и только затем публикует стабильный выпуск. Существующий стабильный релиз он не перезаписывает. Главная страница и описание репозитория рассчитаны на установку приложения.

## GitHub Actions

ci.yml собирает нативный агент и выполняет тесты при PR, push и ручном запуске. После появления утверждённого исходного пакета в v1.1.1 он также собирает полный установщик без ключа подписи и проверяет настоящий переход с 1.0.0. В первой проверке до публикации этого asset полный этап может быть явно пропущен; после публикации выполняется повторный запуск CI. Пользовательские профили, ключи и бинарные artifacts не загружаются.

private-build.yml оставлен для локального self-hosted runner. release.yml требует ручного запуска из main с approval=PUBLISH_STABLE и environment qip-release с required reviewers. Секрет подписи MANIFEST_SIGNING_KEY_B64 задаётся отдельно; публикация через этот workflow без настроенного секрета и правила подтверждения остановится. В этом выпуске приватный ключ в GitHub не переносился.

Для настройки секрета предусмотрен scripts/configure-signing-secret.py. Он требует отдельного решения владельца и авторизованного gh; передаёт значение в stdin без открытого временного файла. QIP_SOURCE_PACKAGE_URL — необязательная repository variable; по умолчанию используется проверяемый публичный пакет v1.1.1.

Обычные jobs имеют contents: read. Записывающие права выдаются только job публикации. Само приложение обращается к публичному GitHub API анонимно и не содержит токенов.
