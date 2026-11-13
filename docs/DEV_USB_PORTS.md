# Kanoniczny rejestr portów USB VoxOne

Ten dokument opisuje pięć bieżących logicznych połączeń stanowisk
deweloperskich VoxOne. Cztery pozostają na ASUS DEV, a dedykowany odbiornik
logów `L0_LOG_RX` jest podłączony do Raspberry Pi `voxone-bridge`
(`192.168.1.85`). Jedynym
źródłem stałych identyfikatorów odczytywanym przez automatykę jest
[`DEV_USB_PORTS.json`](DEV_USB_PORTS.json). Nazwy `/dev/tty*` i bieżąca
topologia USB są tylko migawką diagnostyczną: po ponownej enumeracji mogą się
zmienić i nie wolno na ich podstawie wybierać urządzenia.

Rozpoznanie portu nie jest zgodą na jego otwarcie, reset, programowanie ani
wysłanie danych. `scripts/dev_usb_inventory.py` czyta wyłącznie linki `by-id`,
sysfs oraz właściwości udev; nie otwiera żadnego portu szeregowego.

## Kanoniczna mapa tożsamości

Tożsamości czterech połączeń ASUS DEV i ich topologia pochodzą z migawki
zweryfikowanej 2026-10-26 równocześnie w `/dev/serial/by-id`, sysfs i udev.
Przypisanie `L0_LOG_RX` oraz jego stan na `voxone-bridge` zaktualizowano
2026-10-31 zgodnie z bieżącym stanem fizycznym; topologii udev na tym hoście
nie zapisano.

| Nazwa logiczna | Host | Urządzenie docelowe | Rola USB | VID:PID | Numer seryjny USB | Pełna ścieżka `/dev/serial/by-id` | Bieżący tty (informacyjnie) | Topologia udev z właściwej migawki | Pewność mapowania |
|---|---|---|---|---|---|---|---|---|---|
| `L0_NATIVE` | ASUS DEV | L0 ESP32-S3 DevKit | native USB Serial/JTAG ESP32-S3 | `303a:1001` | `E8:F6:0A:8C:BD:1C` | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_E8:F6:0A:8C:BD:1C-if00` | `/dev/ttyACM1` | `pci-0000:00:14.0-usb-0:1.4:1.0` (`3-1.4`) | **CONFIRMED** |
| `L0_UART` | ASUS DEV | L0 ESP32-S3 DevKit | pokładowy USB–UART L0, FT232R | `0403:6001` | `A5069RR4` | `/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A5069RR4-if00-port0` | `/dev/ttyUSB0` | `pci-0000:00:14.0-usb-0:1.3:1.0` (`3-1.3`) | **CONFIRMED** |
| `L0_LOG_RX` | `voxone-bridge` (`192.168.1.85`) | L0 ESP32-S3 DevKit | dedykowany pasywny odbiornik logów UART, FT232R; GPIO jeszcze niepodłączone | `0403:6001` | `A7039N0Z` | `/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0` | `/dev/ttyUSB0` | nie zapisano | **CONFIRMED** |
| `VOXONEBT_USB` | ASUS DEV | moduł VoxOneBT | jedyny port USB modułu, `USB Single Serial` | `1a86:55d4` | `5B09012537` | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B09012537-if00` | `/dev/ttyACM0` | `pci-0000:00:14.0-usb-0:1.2:1.0` (`3-1.2`) | **CONFIRMED** |
| `S0_NATIVE` | ASUS DEV | S0 ESP32-S3 Zero | native USB Serial/JTAG ESP32-S3 | `303a:1001` | `D0:CF:13:2F:EE:00` | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_D0:CF:13:2F:EE:00-if00` | `/dev/ttyACM2` | `pci-0000:00:14.0-usb-0:2:1.0` (`3-2`) | **CONFIRMED** |

W migawce ASUS DEV z 2026-10-26 gałęzie `3-1.1`–`3-1.4` były podłączone
przez hub Terminus `1a40:0101`, a `S0_NATIVE` bezpośrednio na `3-2`. Jest to
historyczna migawka położenia, a nie element tożsamości; bieżąca mapa nie
przypisuje już urządzenia z dawnej gałęzi `3-1.1` do ASUS DEV ani S0. Model
`L0_UART` i `L0_LOG_RX` jest potwierdzony deskryptorem jako
`FTDI FT232R USB UART`, sterownik `ftdi_sio`. Native ESP32-S3 i VoxOneBT
używają `cdc_acm`.

## Połączenia, zasilanie i linie sterujące

| Nazwa | Sposób podłączenia i zasilania | TX / RX / DTR / RTS / EN / GPIO0 | Ryzyko resetu przy otwarciu | Dozwolone użycie |
|---|---|---|---|---|
| `L0_NATIVE` | Natywne USB ESP32-S3; aktualnie przez hub. USB jest obecne jako źródło zasilania magistrali, ale udział tego kabla w całym zasilaniu L0 nie został elektrycznie rozpoznany. | To USB Serial/JTAG, nie osobny UART TTL. Wpływ sterowania CDC na EN/GPIO0 w rzeczywistym L0 nie został fizycznie zakwalifikowany. **UNVERIFIED**. | Możliwe; narzędzia dla ESP32-S3 potrafią użyć sygnałów sterujących do resetu/bootloadera. | Automatycznie tylko inwentaryzacja metadanych. Otwarcie, logger, reset i flash wymagają osobnej procedury oraz zgody. |
| `L0_UART` | FT232R przypisany do toru USB–UART L0; kontrolowana wcześniejsza identyfikacja wskazuje prawe gniazdo USB-C L0. Dokładna rewizja PCB i droga zasilania pozostają nieznane. | FT232R udostępnia TX/RX/DTR/RTS, lecz fizyczne połączenie DTR/RTS z EN/GPIO0 L0 nie jest potwierdzone. **UNVERIFIED**. | **Możliwe / niezakwalifikowane**. Samo `open()` może zmienić DTR/RTS; tryb deskryptora RX-only tego nie wyklucza. | Automatycznie tylko inwentaryzacja. Logger RX-only dopiero po osobnym potwierdzeniu bezpieczeństwa linii; bez automatycznego reopen. Flash/reset niedozwolone bez odrębnej procedury. |
| `L0_LOG_RX` | FT232R `A7039N0Z` na `voxone-bridge`, przeznaczony wyłącznie do pasywnego odczytu logów L0 przy 115200 8N1. Połączenie z GPIO L0 nie zostało jeszcze wykonane. | Planowane wyłącznie TX L0 → RX FT232R i wspólna GND. Bez TX adaptera, DTR, RTS oraz bez zasilania L0 z konwertera. Przed lutowaniem trzeba potwierdzić GPIO i poziomy napięć. **UNVERIFIED / NOT WIRED**. | Brak wpływu na L0 do czasu wykonania połączenia. Po lutowaniu ryzyko ma być niskie tylko po potwierdzeniu topologii RX-only i poziomów napięć. | Obecnie tylko inwentaryzacja USB. Po kwalifikacji wyłącznie pasywny logger 115200 8N1 przez ścieżkę `by-id`; bez resetu, flashowania, wysyłania danych i automatycznego reopen. |
| `VOXONEBT_USB` | Jedyny port USB modułu VoxOneBT; bieżąca zależność zasilania modułu od tego kabla nie została elektrycznie sprawdzona. | Deskryptory nie dowodzą połączeń TX/RX/DTR/RTS ani ich wpływu na EN/BOOT modułu. **UNVERIFIED**. | Możliwe / niezakwalifikowane. | Automatycznie tylko inwentaryzacja. Otwieranie, komendy, restart i programowanie wymagają osobnej procedury. |
| `S0_NATIVE` | Natywne USB ESP32-S3 Zero, aktualnie bezpośrednio do kontrolera hosta. Udział kabla w zasilaniu S0 nie został elektrycznie sprawdzony. | To USB Serial/JTAG. Wpływ kontroli CDC na EN/GPIO0 stanowiska S0 nie został tu fizycznie zakwalifikowany. **UNVERIFIED**. | Możliwe; nie traktować pasywnego `open()` jako obojętnego. | Automatycznie tylko inwentaryzacja. Logger, reset i flash wyłącznie w osobno zatwierdzonej procedurze S0. |

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

Kod wyjścia `0` i `status=PASS` wymagają pięciu zgodnych, unikalnych ról
z rejestru obecnych lokalnie. Bieżący skrypt nie rozróżnia hostów i nie
agreguje wyników, więc po rozdzieleniu rejestru żaden pojedynczy host nie
może uzyskać pełnego 5/5: brak `L0_LOG_RX` na ASUS DEV oraz brak ról ASUS DEV
na `voxone-bridge` są oczekiwane. Do czasu wykonania zadania wielohostowego
z `docs/TODO.md` lokalny `BLOCKED` nie jest oceną całego mapowania fizycznego.
Brak linku, zerwany link, duplikat serialu/tożsamości, wspólny tty dla dwóch
ról albo rozbieżność między rejestrem, sysfs i udev daje kod `2` oraz
`BLOCKED`. Skrypt nie tworzy linków, nie zmienia udev i nie dotyka termios.

Wszystkie przyszłe skrypty HIL/OTA powinny żądać nazwy logicznej
`L0_NATIVE`, `L0_UART`, `L0_LOG_RX`, `VOXONEBT_USB` lub `S0_NATIVE`, następnie
rozwiązać ją przez ten rejestr i ponownie wykonać kontrolę tożsamości. Nie
wolno utrwalać `/dev/ttyACM*`, `/dev/ttyUSB*` ani bieżącego portu huba.

## Historia przypisania `A7039N0Z` do M0

Do 2026-10-31 FT232R `A7039N0Z` był przypisany na stanowisku ASUS DEV do
logicznej roli `M0_UART` i opisywany jako pasywny RX-only M0. Migawka
z 2026-10-26 rejestrowała go jako `/dev/ttyUSB1` na gałęzi
`pci-0000:00:14.0-usb-0:1.1:1.0` (`3-1.1`). Historyczne raporty HIL M0,
które jawnie wskazują ten numer seryjny, zachowują ważność w zapisanym
zakresie i czasie.

To przypisanie nie opisuje już bieżącego stanowiska: konwerter zabrano z
urządzenia Zero, obecnie klasyfikowanego jako S0, i na stałe przypisano do L0
jako `L0_LOG_RX`. S0 nie ma obecnie zewnętrznego konwertera UART; w przyszłości
ma otrzymać inny. Nie wolno używać dawnej
nazwy `M0_UART`, ścieżki `A7039N0Z` ani historycznego `/dev/ttyUSB1` do
wyboru aktualnego portu S0.

## Nadal wymagane potwierdzenie fizyczne

- dokładna rewizja i schemat płytki L0 oraz połączenia DTR/RTS mostka
  `A5069RR4` z EN/GPIO0;
- GPIO TX L0, poziomy napięć oraz po wykonaniu lutowania faktyczna topologia
  TX L0 → RX FT232R i wspólna GND bez TX adaptera, DTR, RTS i zasilania dla
  `L0_LOG_RX`;
- tożsamość i topologia RX-only przyszłego, innego konwertera S0;
- wpływ otwarcia obu interfejsów native i portu VoxOneBT na reset/bootloader;
- rzeczywista droga zasilania każdego targetu przy podłączonych kilku kablach;
- elektryczne przypisanie TX/RX i poziomów logicznych tam, gdzie nie wynika ze
  schematu właściwej rewizji.

Do czasu tych potwierdzeń rejestr jest gotowy do bezpiecznej identyfikacji
metadanych, ale nie jest dowodem bezpieczeństwa otwierania portów.
