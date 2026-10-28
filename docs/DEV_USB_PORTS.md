# ASUS DEV — kanoniczny rejestr portów USB VoxOne

Ten dokument opisuje pięć logicznych połączeń stanowiska ASUS DEV. Jedynym
źródłem stałych identyfikatorów odczytywanym przez automatykę jest
[`DEV_USB_PORTS.json`](DEV_USB_PORTS.json). Nazwy `/dev/tty*` i bieżąca
topologia USB są tylko migawką diagnostyczną: po ponownej enumeracji mogą się
zmienić i nie wolno na ich podstawie wybierać urządzenia.

Rozpoznanie portu nie jest zgodą na jego otwarcie, reset, programowanie ani
wysłanie danych. `scripts/dev_usb_inventory.py` czyta wyłącznie linki `by-id`,
sysfs oraz właściwości udev; nie otwiera żadnego portu szeregowego.

## Kanoniczna mapa tożsamości

Migawka została zweryfikowana 2026-10-26 równocześnie w
`/dev/serial/by-id`, sysfs i udev.

| Nazwa logiczna | Urządzenie docelowe | Rola USB | VID:PID | Numer seryjny USB | Pełna ścieżka `/dev/serial/by-id` | Bieżący tty (informacyjnie) | Bieżąca topologia udev | Pewność mapowania |
|---|---|---|---|---|---|---|---|---|
| `L0_NATIVE` | L0 ESP32-S3 DevKit | native USB Serial/JTAG ESP32-S3 | `303a:1001` | `E8:F6:0A:8C:BD:1C` | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_E8:F6:0A:8C:BD:1C-if00` | `/dev/ttyACM1` | `pci-0000:00:14.0-usb-0:1.4:1.0` (`3-1.4`) | **CONFIRMED** |
| `L0_UART` | L0 ESP32-S3 DevKit | USB–UART L0, FT232R | `0403:6001` | `A5069RR4` | `/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A5069RR4-if00-port0` | `/dev/ttyUSB0` | `pci-0000:00:14.0-usb-0:1.3:1.0` (`3-1.3`) | **CONFIRMED** |
| `VOXONEBT_USB` | moduł VoxOneBT | jedyny port USB modułu, `USB Single Serial` | `1a86:55d4` | `5B09012537` | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B09012537-if00` | `/dev/ttyACM0` | `pci-0000:00:14.0-usb-0:1.2:1.0` (`3-1.2`) | **CONFIRMED** |
| `M0_NATIVE` | M0 ESP32-S3 Zero | native USB Serial/JTAG ESP32-S3 | `303a:1001` | `D0:CF:13:2F:EE:00` | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_D0:CF:13:2F:EE:00-if00` | `/dev/ttyACM2` | `pci-0000:00:14.0-usb-0:2:1.0` (`3-2`) | **CONFIRMED** |
| `M0_UART` | M0 ESP32-S3 Zero | zewnętrzny FT232R, dokumentowany jako RX-only | `0403:6001` | `A7039N0Z` | `/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0` | `/dev/ttyUSB1` | `pci-0000:00:14.0-usb-0:1.1:1.0` (`3-1.1`) | **CONFIRMED** |

Gałęzie `3-1.1`–`3-1.4` są aktualnie podłączone przez hub Terminus
`1a40:0101`; `M0_NATIVE` jest aktualnie bezpośrednio na `3-2`. Jest to
potwierdzona migawka położenia, a nie element tożsamości. Model `L0_UART` i
`M0_UART` jest potwierdzony deskryptorem jako `FTDI FT232R USB UART`, sterownik
`ftdi_sio`. Native ESP32-S3 i VoxOneBT używają obecnie `cdc_acm`.

## Połączenia, zasilanie i linie sterujące

| Nazwa | Sposób podłączenia i zasilania | TX / RX / DTR / RTS / EN / GPIO0 | Ryzyko resetu przy otwarciu | Dozwolone użycie |
|---|---|---|---|---|
| `L0_NATIVE` | Natywne USB ESP32-S3; aktualnie przez hub. USB jest obecne jako źródło zasilania magistrali, ale udział tego kabla w całym zasilaniu L0 nie został elektrycznie rozpoznany. | To USB Serial/JTAG, nie osobny UART TTL. Wpływ sterowania CDC na EN/GPIO0 w rzeczywistym L0 nie został fizycznie zakwalifikowany. **UNVERIFIED**. | Możliwe; narzędzia dla ESP32-S3 potrafią użyć sygnałów sterujących do resetu/bootloadera. | Automatycznie tylko inwentaryzacja metadanych. Otwarcie, logger, reset i flash wymagają osobnej procedury oraz zgody. |
| `L0_UART` | FT232R przypisany do toru USB–UART L0; kontrolowana wcześniejsza identyfikacja wskazuje prawe gniazdo USB-C L0. Dokładna rewizja PCB i droga zasilania pozostają nieznane. | FT232R udostępnia TX/RX/DTR/RTS, lecz fizyczne połączenie DTR/RTS z EN/GPIO0 L0 nie jest potwierdzone. **UNVERIFIED**. | **Możliwe / niezakwalifikowane**. Samo `open()` może zmienić DTR/RTS; tryb deskryptora RX-only tego nie wyklucza. | Automatycznie tylko inwentaryzacja. Logger RX-only dopiero po osobnym potwierdzeniu bezpieczeństwa linii; bez automatycznego reopen. Flash/reset niedozwolone bez odrębnej procedury. |
| `VOXONEBT_USB` | Jedyny port USB modułu VoxOneBT; bieżąca zależność zasilania modułu od tego kabla nie została elektrycznie sprawdzona. | Deskryptory nie dowodzą połączeń TX/RX/DTR/RTS ani ich wpływu na EN/BOOT modułu. **UNVERIFIED**. | Możliwe / niezakwalifikowane. | Automatycznie tylko inwentaryzacja. Otwieranie, komendy, restart i programowanie wymagają osobnej procedury. |
| `M0_NATIVE` | Natywne USB ESP32-S3 Zero, aktualnie bezpośrednio do kontrolera hosta. Udział kabla w zasilaniu M0 nie został elektrycznie sprawdzony. | To USB Serial/JTAG. Wpływ kontroli CDC na EN/GPIO0 stanowiska M0 nie został tu fizycznie zakwalifikowany. **UNVERIFIED**. | Możliwe; nie traktować pasywnego `open()` jako obojętnego. | Automatycznie tylko inwentaryzacja. Logger, reset i flash wyłącznie w osobno zatwierdzonej procedurze M0. |
| `M0_UART` | Zewnętrzny FT232R jest dokumentowany jako odbiornik RX-only M0. Sysfs/udev nie potrafią potwierdzić obecnego okablowania ani zasilania po stronie TTL. | Zamierzona topologia to M0 TX → FT232R RX i wspólna masa, bez TX adaptera, DTR i RTS. Aktualnego stanu przewodów DTR/RTS/EN/GPIO0 nie da się potwierdzić programowo. **UNVERIFIED**. | Niskie tylko wtedy, gdy topologia RX-only jest fizycznie zachowana; obecnie nie wolno uznać tego za dowód. | Automatycznie tylko inwentaryzacja. Logger dopiero w procedurze potwierdzającej aktualne RX-only; bez automatycznego resetu lub programowania. |

Poziom **CONFIRMED** w pierwszej tabeli dotyczy mapowania logicznej nazwy na
tożsamość USB. Nie podnosi pewności połączeń elektrycznych. Wszystkie linie
sterujące pozostają **UNVERIFIED**, dopóki nie potwierdzi ich schemat właściwej
rewizji albo kontrola fizyczna.

## Automatyczna inwentaryzacja

Uruchomienie czytelnej tabeli:

```sh
python3 scripts/dev_usb_inventory.py
```

Jawna kontrola kompletności i jednoznaczności:

```sh
python3 scripts/dev_usb_inventory.py --check
```

Wyjście dla przyszłych skryptów HIL:

```sh
python3 scripts/dev_usb_inventory.py --json
```

Kod wyjścia `0` i `status=PASS` wymagają pięciu zgodnych, unikalnych ról.
Brak linku, zerwany link, duplikat serialu/tożsamości, wspólny tty dla dwóch
ról albo rozbieżność między rejestrem, sysfs i udev daje kod `2` oraz
`BLOCKED`. Skrypt nie tworzy linków, nie zmienia udev i nie dotyka termios.

Wszystkie przyszłe skrypty HIL/OTA powinny żądać nazwy logicznej
`L0_NATIVE`, `L0_UART`, `VOXONEBT_USB`, `M0_NATIVE` lub `M0_UART`, następnie
rozwiązać ją przez ten rejestr i ponownie wykonać kontrolę tożsamości. Nie
wolno utrwalać `/dev/ttyACM*`, `/dev/ttyUSB*` ani bieżącego portu huba.

## Nadal wymagane potwierdzenie fizyczne

- dokładna rewizja i schemat płytki L0 oraz połączenia DTR/RTS mostka
  `A5069RR4` z EN/GPIO0;
- faktyczny stan przewodów TX adaptera, DTR i RTS w deklarowanym torze
  RX-only `M0_UART`;
- wpływ otwarcia obu interfejsów native i portu VoxOneBT na reset/bootloader;
- rzeczywista droga zasilania każdego targetu przy podłączonych kilku kablach;
- elektryczne przypisanie TX/RX i poziomów logicznych tam, gdzie nie wynika ze
  schematu właściwej rewizji.

Do czasu tych potwierdzeń rejestr jest gotowy do bezpiecznej identyfikacji
metadanych, ale nie jest dowodem bezpieczeństwa otwierania portów.
