# HotspotVPN — выбор DNS для клиентов

Production-версия **1.0.0** дополнения к оригинальному HotspotVPN 1.0. Установлена и проверена 6 октября 2026 года на **iPhone 11 Pro Max / iOS 16.6.1 / rootless Dopamine / ElleKit**, с активным **Happ 6.0.0**. Репозиторий содержит исходники дополнения; оригинальный твик и его иконки в него не включены.

## Использование

В Настройках пункт **HotspotVPN** содержит переключатель, Cloudflare **1.1.1.1**, Google **8.8.8.8** и свой IPv4. При первой установке дополнение включено и выбран Cloudflare. Обновление сохраняет ранее выбранные настройки, включая выключенный переключатель. На проверенном устройстве сейчас включён Cloudflare.

После изменения DNS переподключите клиентов или обновите DHCP-аренду. Для маршрутизации через VPN включите Happ и оригинальный HotspotVPN в Пункте управления. Иконка панели использует оригинальный `Icon.png` / `@2x` / `@3x`, размер **29 pt**.

## Результат проверки UDP

После установки 1.0.0 все **8 проверок** с USB-клиента получили корректные ответы: DNS, STUN и NTP. `youtube.com` разрешился через Cloudflare и Google по **UDP**, без TCP-прокси. Это опровергает предположение о блокировке всего UDP.

Ответы пришли через 1.36–1.49 секунды от начала, после повторной отправки через секунду. Причина задержки или потери первого запроса не установлена. Ранние одиночные пробы с таймаутами не доказывали отсутствие UDP. Это не гарантия работы любых UDP-приложений или отсутствия потерь.

Все 16 отправок восьми тестовых запросов видны на `bridge100` и `utun7`; совпадений с ними на `pdp_ip0` нет. На сотовом интерфейсе при этом есть отдельные DNS-запросы самого iPhone для Apple Push. Дополнение выбирает DNS **клиентов точки доступа**; весь DNS-трафик телефона оно не исправляет.

Подробности: [отчёт проверки](docs/production-validation.md), [машинные результаты](validation.json).

## Реализация и ограничения

Hook загружается только в `bootpd` и заменяет адреса существующей DHCP option 6 в OFFER/ACK на выбранный IPv4. Размер пакета сохраняется, ненулевой UDP checksum пересчитывается. Отсутствующая option 6 не добавляется; неверный IPv4 оставляет штатный ответ без изменений.

Перехватываются `sendto`, `sendmsg`, `write`, `writev`, `ioctl`. Для BPF отслеживается успешный `BIOCSETIF` с интерфейсом `bridge…` и проверкой идентичности устройства через `fstat`; повторное связывание сбрасывает запись. Для UDP проверяются порты 67/68 и server identifier точки доступа.

Собственный DNS клиента, DoH/DoT, IPv6 RDNSS/DHCPv6 и старые аренды не контролируются. Принудительного перехвата DNS и DNS-прокси нет. При выключенном VPN дополнение не блокирует трафик. Wi-Fi отдельно не проверялся: устройство проверено с USB-клиентом.

## Сборка

Release-флаги: `-O2 -DNDEBUG -Wall -Wextra -Werror`. Пакет требует подписанную **arm64e PAC00** slice, subtype `0x80000002`, совместимую с проверенным A13. SDK и сторонние toolchain не входят в репозиторий.

Theos с совместимым Apple clang и SDK iOS 16.5:

```sh
make clean package FINALPACKAGE=1 DEBUG=0
```

Нативная сборка с Procursus Clang 16, ld64 951.9 и ldid:

```sh
HPD_SDK=/path/iPhoneOS16.5.sdk sh scripts/build-native.sh
mkdir -p outputs
python3 scripts/package.py --binary work/ios-build/HotspotVPNDNS.dylib --output outputs/local.hotspotvpndns_1.0.0_iphoneos-arm64.deb
```

Раздельный SDK для link stubs задаётся через `HPD_LINK_SDK`; пути инструментов через `HPD_CLANG`, `HPD_LD`, `HPD_LDID`, ресурсный каталог через `HPD_RESOURCE_DIR`. Подпись и архитектура проверяются до упаковки. Debian architecture `iphoneos-arm64` обозначает rootless пакет; установленная dylib имеет arm64e PAC00.

## Проверки и Git

```sh
python3 tests/run.py
```

На Windows можно передать `--cc /path/zig.exe`. Runner компилирует настоящий C-парсер и запускает 28 тестов, включая 20 000 случайных входов, усечения, overload, VLAN и checksums. GitHub Actions настроен для этих тестов; удалённый CI пока не запускался.

Для повторения UDP-проверки на Windows с Npcap: `python scripts/udp_matrix.py --help`. Параметры адаптера и адреса нужно взять со своего подключения. Адрес Google STUN нужно разрешить заново перед проверкой.

Ветка локального репозитория — `main`, release tag — `v1.0.0`. Remote не настроен. `work/`, `outputs/`, бинарные файлы, pcap и локальные конфигурации исключены из Git. Пароли и настройки Happ в исходники и отчёт не включены.

## Удаление

```sh
sudo dpkg -r local.hotspotvpndns
```

`postrm` перезапускает `bootpd`, выгружая hook. Оригинальный HotspotVPN и выбранные пользователем настройки дополнения сохраняются.

Источники: [RFC 2132, option 6](https://www.rfc-editor.org/rfc/rfc2132#section-3.8), [Apple UDP transport](https://github.com/apple-oss-distributions/bootp/blob/main/bootplib/udp_transmit.c), [Apple BPF transport](https://github.com/apple-oss-distributions/bootp/blob/main/bootplib/bpflib.c).
