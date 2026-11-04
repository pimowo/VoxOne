# VOXONE — RC1-L0-MQTT-RETAINED-EVIDENCE-1

Data dowodu: 2026-11-04, 16:48–16:49 CET. Dowód pochodzi z ręcznego
odczytu operatora w panelu Home Assistant. Nie wykonywano nowego połączenia
MQTT z DEV, publikacji MQTT, zmiany stanu L0 ani konfiguracji HA lub brokera.

## Werdykt

- **MQTT retained: 3/3 PASS — obserwacja operatora w panelu HA.**
- **HA / REST / WebSocket: zdalny HIL PASS** w zachowanym zakresie testu
  `RC1-L0-HA-ENDURANCE-60M-1` oraz kontrolowanych prób MCP.
- **Bezpośredni dostęp MQTT z DEV: BLOCKED** — brak zatwierdzonych
  poświadczeń klienta read-only; nie wykonywano anonimowego CONNECT.
- **Fizyczny odsłuch: UNVERIFIED.**

Jest to zestaw dowodów z dwóch różnych okien czasowych. Godzinny test trwał
od `2026-11-04T15:10:53.882+01:00` do
`2026-11-04T16:10:55.311+01:00`. Odczyty retained wykonano później,
o 16:48–16:49 CET. Nie przypisuje się ich do godzinnego okna i nie zmienia
się retrospektywnie jego werdyktu `PARTIAL`, który wynikał wyłącznie z
nieobserwowanego wtedy MQTT.

## Dowody MQTT retained

| Czas CET | Temat | QoS | Retain | Payload | Wynik |
|---|---|---:|---|---|---|
| 16:48 | `voxone-8CBD1C/status` | 0 | `true` | `{"status":1,"station":2,"name":"Meloradio Wrocław","title":"Meloradio Wrocław","on":1}` | PASS |
| 16:48 | `voxone-8CBD1C/volume` | 0 | `true` | `25` | PASS |
| 16:49 | `voxone-8CBD1C/playlist` | 0 | `true` | `http://192.168.1.56/data/stations.tsv` | PASS |

Wynik 3/3 potwierdza dostępność wszystkich trzech wyjściowych tematów
retained zamrożonego kontraktu L0. Nie obejmuje wejściowego
`voxone-8CBD1C/command`, reconnectu brokera, ponownej subskrypcji po
reconnect ani aktywnego pomiaru chwili zapisu retained.

## Porównanie z RC1-L0-HA-ENDURANCE-60M-1

Godzinny runner zakończył `3600.66 s` z `duration_complete`:

- REST: `360 PASS / 0 FAIL`;
- WebSocket: `120 PASS / 0 FAIL`;
- HA MCP: `60 PASS / 0 FAIL`;
- stały `bootId=3cb6512e`, READY oraz OTA `app1 VALID`, `pending=false`;
- końcowo RADIO/PLAY/playing, stacja #2 `Meloradio Wrocław`,
  `volume100=10`, MUTE OFF, balance=0;
- HA miało dokładnie jedną encję `Salon` w domenie `media_player`, w stanie
  `playing`, z `volume_level=0.0984251968503937`.

Późniejszy retained `status` jest zgodny z tym końcowym stanem w polach
`status=1`, `station=2`, `name` i `title`. Pole `on=1` jest zgodne z legacy
kontraktem `config.store.dspon`, ale końcowe podsumowanie endurance nie
zapisało osobnego pola `dspon`, więc nie przypisuje mu się niezależnego
porównania z tego okna.

Retained `volume=25` dokładnie odpowiada `volume100=10` zgodnie z mapowaniem
firmware 0–100 na 0–254: `(10 * 254 + 50) / 100 = 25` w arytmetyce
całkowitej. URL retained `playlist` jest zgodny z kontraktem L0 i adresem
`/data/stations.tsv`; ten dowód nie jest ponowną walidacją zawartości pliku.

## Porównanie z MCP PLAY/PAUSE/VOLUME

Kontrolowany zdalny HIL MCP wykonał po osobnych zgodach dokładnie po jednej
operacji PAUSE i UNPAUSE oraz zmianę volume 10% → 9% → 10%. HA, REST i
WebSocket potwierdziły odpowiednio RADIO/STOP → RADIO/PLAY oraz końcowe
`volume100=10`; stacja #2, MUTE OFF, balance=0, `bootId=3cb6512e` i OTA
`app1 VALID` pozostały niezmienione. Późniejsze wartości retained
`status=1` i `volume=25` są zgodne z przywróconym stanem końcowym.

Ponieważ odczyt retained nie był zsynchronizowany z operacjami MCP, nie jest
dowodem, że broker zaktualizował retained w chwili każdego przejścia PAUSE,
UNPAUSE, 9% i 10%. Zachowuje się wcześniejszy PASS logicznego sterowania,
bez przyznawania fizycznego AUDIO PASS.

## Stan bramki Stable

Wpis MQTT/Home Assistant dla L0 pozostaje **OPEN / PARTIAL**. Nowy dowód
zamyka bieżący ręczny odczyt trzech tematów retained w panelu HA, lecz nadal
otwarte są bezpośredni autoryzowany odczyt z DEV, egzekwowane broker-side
ACL dla klienta read-only, fizyczny reconnect i ponowna subskrypcja,
aktywny dowód aktualizacji `status`/`playlist` w kontrolowanym oknie oraz
regresja po restarcie. Fizyczny odsłuch pozostaje UNVERIFIED.
