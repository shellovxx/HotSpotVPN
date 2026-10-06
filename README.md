# HotspotVPN DNS

Выбор DNS для клиентов точки доступа: Cloudflare, Google или свой IPv4. Версия 1.0.0 · rootless iOS 15+.

## Установка

1. Установите VPN-приложение и [оригинальный HotspotVPN](original/com.evgeniy.hotspotvpn_1.0_iphoneos-arm64.deb) ≥ 1.0. Для оригинала нужны CCSupport и библиотека хуков.
2. Установите собранный пакет `local.hotspotvpndns` через менеджер пакетов. Дополнению нужны оригинал, PreferenceLoader и библиотека хуков.
3. Включите VPN и HotspotVPN в Пункте управления. Выберите DNS в **Настройки → HotspotVPN** и переподключите клиентов.

Дополнение не заменяет оригинал. Менеджер пакетов скачивает зависимости только из подключённых источников; `dpkg -i` их не скачивает.

При первой установке выбран Cloudflare `1.1.1.1`; обновления сохраняют настройки. Hook `bootpd` заменяет DHCP option 6 в OFFER/ACK, сохраняет размер пакета и пересчитывает UDP checksum.

Удаление: `sudo dpkg -r local.hotspotvpndns`. Оригинал и настройки сохраняются.

## Сборка

Нужны Theos, совместимый Apple clang и iOS 16.5 SDK. Пакет должен содержать подписанную arm64e PAC00 slice.

```sh
make clean package FINALPACKAGE=1 DEBUG=0
python3 tests/run.py
```

Нативная сборка: `HPD_SDK=/path/iPhoneOS16.5.sdk sh scripts/build-native.sh`; упаковка — `scripts/package.py --help`. SDK и инструменты в репозиторий не входят.

## Проверка и ограничения

Проверено по USB на iPhone 11 Pro Max / iOS 16.6.1 / Dopamine / ElleKit / Happ 6.0.0: DHCP, HTTPS, 8/8 UDP-проб DNS/STUN/NTP и 28 тестов парсера. UDP-ответы приходили после повтора через 1,3–1,5 с; причина задержки неизвестна.

Wi-Fi и другие устройства не проверены. DoH/DoT, IPv6 и собственный DNS клиента не контролируются; kill switch отсутствует.
