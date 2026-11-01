# VOXONE-L0-MQTT-RETAINED-LIVE-HIL-1

Data: 2026-11-01. Preflight około 20:43 CET na DEV-VM, repo `main` na
`febfa5d`; fizyczny L0 `192.168.1.56` zgłaszał build `bf1d147`.
Uzupełnienie: obserwacje użytkownika w Home Assistant z 20:59–21:46 CET.
Próba była wyłącznie odczytowa: nie publikowano MQTT, nie zmieniano stacji,
volume, MUTE, playlisty, konfiguracji HA ani firmware. Nie wykonywano
reconnectu, restartu, OTA ani flashowania. Nie używano odsłuchu.

## Werdykt

**PHYSICAL HIL PASS w ograniczonym zakresie obserwacji HA:** użytkownik
potwierdził 18 kolejnych aktualizacji `status` z naturalnymi zmianami
metadanych oraz odczyt retained trzech tematów na fizycznym L0 `bf1d147`.
To dowodzi live publikacji tytułów i bieżącej dostępności retained przez HA;
nie jest pełną bramką MQTT. Reconnect, ponowna subskrypcja `command` po
reconnect i availability/LWT pozostają **UNVERIFIED**. Nie dostarczono
surowego eksportu wiadomości HA, zsynchronizowanego WS/UART ani ciągłego
monitoringu READY/bootId z okna 20:59–21:46; nie przypisuje się im PASS.

Wcześniejszy preflight DEV-VM był **BLOCKED**: broker `192.168.1.14:1883`
odrzucił pojedynczy anonimowy MQTT CONNECT odpowiedzią `CONNACK 20 02 00 05`
(`Not authorized`). Nie było zatwierdzonego magazynu poświadczeń HIL,
więc nie odczytywano haseł ani nie obchodzono ACL. Z DEV-VM nie subskrybowano
tematów i nie wykonano planowanej 180-sekundowej obserwacji. Późniejsze
odczyty użytkownika w HA rozwiązują brak dowodu retained/live w podanym
zakresie, lecz nie zmieniają wyniku tej wcześniejszej próby DEV-VM.

## Nowe dowody z Home Assistant

- Broker: `192.168.1.14`; temat `voxone-8CBD1C/status`; stacja 10,
  `RMF Dla dzieci`. Między 20:59 a 21:46 użytkownik odebrał 18 kolejnych
  aktualizacji metadanych utworów. Dostawy podczas aktywnej subskrypcji
  miały QoS 0 i `Retain=false`. Przykłady: 21:43
  `Przedszkojak - Radę sobie dam`; 21:46
  `Majka Jeżowska - Kołysanka dla Michałka`. **Live metadata:
  PHYSICAL HIL PASS** w zakresie obserwacji użytkownika.
- Bieżące odczyty retained w HA: `voxone-8CBD1C/status` — PASS;
  `voxone-8CBD1C/volume` — PASS, surowa wartość `30`;
  `voxone-8CBD1C/playlist` — PASS, URL
  `http://192.168.1.56/data/stations.tsv`. Wszystkie trzy odczyty miały
  QoS 0 i `Retain=true`. **Retained readback: PHYSICAL HIL PASS** dla tych
  trzech tematów. Nie podano pełnego bieżącego JSON `status` ani równoległego
  readbacku WS, więc nie zaliczono parytetu wszystkich pól.
- `Retain=false` przy dostawie live i `Retain=true` przy odczycie retained
  rozróżniają rodzaj dostawy do subskrybenta; nie są sprzecznymi wynikami.

## Kontrakt i zachowane dowody

`docs/STABLE_CONTRACT.md` oraz `src/core/mqtt.cpp` definiują cztery sufiksy;
bieżący root `voxone-8CBD1C` potwierdza `/api/mqtt`. Wejściowy `command`
(subskrypcja QoS 2) i trzy tematy
wyjściowe, publikowane QoS 0 z `retain=true`:

| Temat | Kontrakt | Stan dowodu |
|---|---|---|
| `status` | JSON z `status` (1 tylko dla PLAYING), numerem/nazwą stacji, `title` i niezależnym `on=dspon` | Historyczny pasywny retained PASS z HA; na `bf1d147` użytkownik potwierdził 18 dostaw live tytułów i bieżący odczyt retained, QoS 0. Pełny aktualny JSON i parytet wszystkich pól z WS nie zostały dostarczone. |
| `volume` | Liczba w kompatybilnej skali MQTT 0–254 | Wcześniejszy HA↔L0 HIL 20% → 15% → 20% oraz retained 51 → 38 → 51 mają ograniczony PASS na wcześniejszym obrazie. Na `bf1d147` użytkownik potwierdził bieżący retained `30`, QoS 0; bez pomiaru nowej aktywnej publikacji volume. |
| `playlist` | URL HTTP do `/data/stations.tsv` | Historyczny pasywny retained PASS z HA; na `bf1d147` użytkownik potwierdził bieżący retained URL, QoS 0. Zwykła zmiana metadanych nie wyzwala publikacji playlisty; aktywna ponowna publikacja pozostaje niezweryfikowana. |
| `command` | Odbiór komend QoS 2; temat nie jest wyjściowym retained | Na `bf1d147` scenariusze TTS A/B/C dowiodły odbioru trzech ręcznych publikacji HA i trzech odrębnych żądań fixture; nie powtarzano ich. |

Nie ma innych wymaganych tematów retained w zamrożonym kontrakcie. Kod
`mqtt.cpp` nie konfiguruje osobnego availability/LWT; jego brak nie jest
„nieodczytanym retained topic”. Kwestia availability/LWT i fizyczny reconnect
z ponowną subskrypcją `command` pozostają otwarte, bez nadawania im PASS.
Test native `mqtt_contract_native` potwierdza ścieżkę reconnectu software,
lecz nie zastępuje fizycznego dowodu.

## Read-only preflight L0

- `GET /api/mqtt`: HTTP 200; `enabled=true`, broker `192.168.1.14:1883`,
  `effectiveRoot=voxone-8CBD1C`, `passwordSet=true`. API nie ujawnia hasła;
  sama konfiguracja nie dowodzi aktywnej sesji MQTT.
- Dwukrotny `GET /api/ready`: HTTP 200, `ready=true`, stały
  `bootId=ca5d5657`, OTA `app0` VALID i WWW `READY`, verified/compatible.
  To krótki preflight, **nie** 180-sekundowy PASS stabilności.
- Jeden krótkotrwały klient WS: handshake 101, kompletny GETINDEX, następnie
  `getsystem` i `getwebstatus`; po odczycie klient został zamknięty. L0 zgłasza
  RADIO/PLAY/playing, stację 1 „Afterhours”, metadata
  `Ronny K. - Trance4nations 099 on AH.FM 26-10-2026`, MP3 192 kb/s,
  `volume100=12`, MUTE OFF, balance 0, RSSI −60 dBm, wolny heap 116764 B,
  wolny PSRAM 7796119 B. Bez odczytu brokera nie wolno uznać zgodności
  bieżącego `status` z tym stanem za potwierdzoną.
- `GET /data/stations.tsv` zwrócił HTTP 200; stacja 1 wskazuje
  `https://nl.ah.fm/live`. Nie dowodzi to zawartości retained `playlist`.

## Otwarte kryteria i minimalny dostęp

Nowe obserwacje HA zamykają odczyt retained trzech tematów i live zmianę
tytułu tylko w opisanym zakresie. Nadal brak zsynchronizowanego porównania
pełnego `status` z WS, dowodu aktywnej ponownej publikacji `playlist`,
fizycznego reconnectu i ponownej subskrypcji `command`, availability/LWT
oraz regresji po restarcie L0. Te scenariusze wymagają odrębnych dowodów;
nie są objęte tą próbą. Brak dostępu z DEV-VM nie uzasadnia obchodzenia ACL.

**Nie wykryto nowego defektu firmware w podanym zakresie.** Z obserwacji
użytkownika nie wynika pełna kwalifikacja MQTT ani brak innych defektów.
Wpis release-gating w `docs/TODO.md` pozostaje otwarty.
