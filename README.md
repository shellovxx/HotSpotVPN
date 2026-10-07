# HotspotVPN DNS

Выбор DHCP DNS для клиентов точки доступа: Cloudflare, Google или свой IPv4. Rootless iOS 15+, дополнение к оригинальному HotspotVPN.

## Установка

Установите [оригинал](original/com.evgeniy.hotspotvpn_1.0_iphoneos-arm64.deb), затем DEB из [релиза](https://github.com/shellovxx/HotSpotVPN/releases/latest). В **Настройки → HotspotVPN** выберите DNS и включите его выдачу. Также включите оригинальный HotspotVPN в Пункте управления.

Отключение любого переключателя возвращает системный DHCP DNS. После изменения переподключите клиентов или обновите DHCP-аренду.

## Сборка

Theos, совместимый clang и SDK iOS 16.5:

```sh
make clean package FINALPACKAGE=1 DEBUG=0
python3 tests/run.py
```

Logos: `src/HotspotVPNDNS.x` и `prefs/HPDRootListController.x`. C: `src/preferences.c` и `src/dhcp_dns.c`. Для нативной сборки: `THEOS=… sh scripts/build-native.sh`.

**1.1.0 проверена по SSH на iPhone 11 Pro Max / iOS 16.6.1:** 36 DHCP-проверок, checksum, настоящие аренды Windows и 29 тестов парсера. Полная работа VPN не подтверждена: HTTPS и два STUN-запроса завершились таймаутами. Wi-Fi отдельно не проверялся; DoH/DoT и IPv6 не контролируются.
