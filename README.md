# HotspotVPN и HotspotVPN DNS — RootHide

Два пакета для RootHide / Relaxin:

| Пакет | Версия | Назначение |
|---|---|---|
| `com.evgeniy.hotspotvpn` | `1.0+roothide.1` | Передача раздаваемого трафика через VPN, модуль Пункта управления |
| `local.hotspotvpndns` | `1.2.0` | Cloudflare, Google или свой IPv4 DNS в DHCP для клиентов |

Оба `.deb` имеют архитектуру `iphoneos-arm64e`. DNS содержит подписанные arm64 и современные arm64e PAC00 slices. DNS-хуки переписаны на Logos; генератор MobileSubstrate использует установленный ElleKit. Диагностические логи удалены.

Основной HotspotVPN имеет закрытый исходный код. В репозитории доступен только исходный `.deb`: он перенесён официальным RootHide Converter и повторно подписан после удаления прямых диагностических вызовов NSLog из твика и CC-модуля. Этот бинарный перенос не является переписыванием основного пакета на Logos. Точечный патчер находится в `scripts/quiet_macho.py`; после него обязательна повторная подпись.

Установка:

1. Установите VPN-приложение и RootHide-пакет HotspotVPN из Releases. Нужны CCSupport, ElleKit, RootHide ≥ 0.1.0, iOS ≥ 15.
2. Установите HotspotVPN DNS; дополнительно нужен PreferenceLoader.
3. Добавьте HotspotVPN в **Настройки → Пункт управления**. Включите VPN, точку доступа и кнопку HotspotVPN.
4. Выберите DNS в **Настройки → HotspotVPN**, затем переподключите клиентов.

Первоначально выбран Cloudflare `1.1.1.1`; обновления сохраняют настройки. Настройки читаются через API `jbroot`, иконка загружается относительно ресурса PreferenceLoader. Хуки работают только в `bootpd`, меняют DHCP option 6 в OFFER/ACK и пересчитывают UDP checksum, сохраняя длину пакета.

Удаление DNS: `sudo dpkg -r local.hotspotvpndns`. Основной пакет и сохранённые настройки остаются. `dpkg -i` самостоятельно не скачивает зависимости.

Сборка DNS с [RootHide Theos](https://github.com/roothide/theos), SDK iOS 16.5 и совместимым Apple/Procursus clang:

```sh
make clean package FINALPACKAGE=1 DEBUG=0
python3 tests/run.py
# Альтернатива: clang, ld, lipo и ldid в PATH
THEOS=/path/to/roothide-theos HPD_SDK=/path/to/iPhoneOS16.5.sdk sh scripts/build-native.sh
python3 scripts/package.py --binary work/ios-build/HotspotVPNDNS.dylib --output packages/local.hotspotvpndns_1.2.0_iphoneos-arm64e.deb
```

Для воспроизведения основной сборки сначала конвертируйте копию `original/com.evgeniy.hotspotvpn_1.0_iphoneos-arm64.deb` через RootHide Converter. Converter может удалить входной файл, поэтому используйте копию. Затем на устройстве с Python 3, ldid и cctools выполните:

```sh
sh scripts/package-vendor.sh /path/to/converted.deb /path/to/output.deb
```

Проверено на iPhone 11 Pro Max / iOS 16.6.1 / Relaxin / RootHide / ElleKit / Happ: модуль CC, страница настроек, загрузка хуков и реальная раздача по Wi-Fi на второй телефон. Подробности в [проверке RootHide](verification-roothide.md). 28 тестов парсера проходят. DoH/DoT, IPv6 и собственный DNS клиента не контролируются; kill switch отсутствует.
