# VOXONE — RC1-L0-MQTT-STATUS-TRANSITION-EVIDENCE-1

Data testu: 2026-11-04, 17:43–17:51 CET. Test wykonano zdalnie na L0
`192.168.1.56`, profil `L0`, build `4ef1e53`, wersja `0.8.0`.

## Werdykt

- **MQTT ACTIVE STATUS TRANSITION: PASS.**
- **MQTT retained po PAUSE: PASS.**
- **MQTT retained po PLAY: PASS.**
- **HA / REST / WebSocket logical HIL: PASS** w zakresie tego scenariusza.
- **Bezpośredni odczyt MQTT z DEV: NOT USED / nadal BLOCKED.**
- **Fizyczny odsłuch: UNVERIFIED.**

Wynik potwierdza aktywne publikacje i aktualizację retained tematu
`voxone-8CBD1C/status` dla kontrolowanego przejścia
RADIO PLAY → STOP → PLAY. Nie zamyka całej bramki MQTT/Home Assistant.

## Źródła dowodów i ograniczenia

Wiadomości MQTT obserwował ręcznie operator w istniejącym panelu MQTT Home
Assistant. Panel podawał czas z dokładnością do minuty. Nie używano klienta
MQTT na DEV, nie odczytywano poświadczeń i nie wykonano publikacji MQTT.
Dowód nie jest surowym eksportem brokera ani przechwyceniem pakietów.

Sterowanie wykonano przez Home Assistant MCP. Stan encji HA, REST i
WebSocket sprawdzono z DEV przy użyciu istniejących autoryzowanych
interfejsów. REST potwierdzał zdrowie, bootId, OTA i stację; stan odtwarzania
potwierdzały HA i WebSocket.

## Baseline

Bezpośrednio przed PAUSE potwierdzono:

- dokładnie jedną encję `Salon` w domenie `media_player`;
- HA `media_player.salon=playing`;
- L0 RADIO/PLAY, stację #2 `Meloradio Wrocław`;
- `volume100=10`, MUTE OFF, balance 0;
- READY=true, `bootId=3cb6512e`;
- OTA `app1 VALID`, `pending=false`.

O 17:43 CET świeża subskrypcja panelu HA zwróciła początkową wiadomość
retained `status=1`, `station=2`, `title="Meloradio Wrocław"`, `on=1`,
QoS 0, `Retain=true`.

## Przebieg i dowody MQTT

### PAUSE

O `17:45:45.575 CET` wykonano dokładnie jedno
`media_player__HassMediaPause` dla `Salon` / `media_player`. MCP zwróciło
`action_done`, sukces dla `media_player.salon`, bez błędów i retry.

O 17:45 CET panel HA odebrał dwie identyczne wiadomości live:

```json
{"status":0,"station":2,"name":"Meloradio Wrocław","title":"[zatrzymano]","on":1}
```

Obie miały QoS 0 i `Retain=false`. HA przeszedł do `idle`, a WebSocket do
RADIO/STOP, `transport=stopped`, `playerwrap=stopped`.

O 17:47 CET nowa subskrypcja tego samego dokładnego tematu zwróciła:

```json
{"status":0,"station":2,"name":"Meloradio Wrocław","title":"[zatrzymano]","on":1}
```

Wiadomość miała QoS 0 i `Retain=true`. Potwierdza to zapis końcowego stanu
STOP jako bieżącej wiadomości retained, niezależnie od dostarczenia live.

### PLAY

Po osobnej zgodzie, o `17:49:55.028 CET`, wykonano dokładnie jedno
`media_player__HassMediaUnpause` dla `Salon` / `media_player`. MCP zwróciło
`action_done`, sukces dla `media_player.salon`, bez błędów i retry.

O 17:49 CET panel HA odebrał pięć wiadomości live w następującej kolejności:

| Nr panelu | `status` | `title` | QoS | Retain |
|---:|---:|---|---:|---|
| 5 | 0 | `[łączenie]` | 0 | `false` |
| 6 | 0 | `[łączenie]` | 0 | `false` |
| 7 | 0 | `[łączenie]` | 0 | `false` |
| 8 | 1 | `[łączenie]` | 0 | `false` |
| 9 | 1 | `Meloradio Wrocław` | 0 | `false` |

Każda wiadomość zawierała `station=2`, `name="Meloradio Wrocław"` i
`on=1`. Przejściowe `status=0` odpowiada fazie łączenia, w której odtwarzacz
nie ma jeszcze stanu PLAYING. Końcowe `status=1` potwierdza rozpoczęcie
odtwarzania, a ostatnia publikacja także końcową aktualizację tytułu.

O 17:51 CET nowa subskrypcja zwróciła końcową wiadomość retained:

```json
{"status":1,"station":2,"name":"Meloradio Wrocław","title":"Meloradio Wrocław","on":1}
```

Wiadomość miała QoS 0 i `Retain=true`. Potwierdza to zastąpienie retained
STOP końcowym stanem PLAY.

## Korelacja HA / REST / WebSocket

Po PAUSE:

- HA: `idle`, `volume_level=0.0984251968503937`;
- WebSocket: RADIO/STOP, `transport=stopped`, `playerwrap=stopped`,
  metadata `[zatrzymano]`;
- REST: READY=true, `bootId=3cb6512e`, OTA `app1 VALID`, `pending=false`;
- stacja #2, `volume100=10`, MUTE OFF i balance 0 bez zmian.

Po PLAY:

- HA: `playing`, `volume_level=0.0984251968503937`;
- WebSocket: RADIO/PLAY, `transport=playing`, `playerwrap=playing`, MP3,
  metadata `Meloradio Wrocław`;
- REST: READY=true, `bootId=3cb6512e`, OTA `app1 VALID`, `pending=false`;
- stacja #2, `volume100=10`, MUTE OFF i balance 0 bez zmian.

Nie wystąpił restart ani zmiana bootId. Pole MQTT `on=1` pozostało stałe i
nie było interpretowane jako PLAY/STOP; ma ono odrębną semantykę DSP.

## Zakres PASS i stan bramki

Scenariusz zamyka brakujący dowód aktywnej aktualizacji MQTT `status` oraz
retained podczas kontrolowanego STOP → PLAY. Duplikaty publikacji oraz
przejściowe `status=0` podczas łączenia są zgodne z kontraktem firmware i
nie stanowią błędu.

Cała bramka MQTT/Home Assistant pozostaje **OPEN / PARTIAL**. Nadal nie ma
rzeczywistego dowodu reconnectu brokera i ponownej subskrypcji, aktywnej
ponownej publikacji `playlist`, availability/LWT ani pełnej regresji po
restarcie. Brak klienta MQTT read-only na DEV jest ograniczeniem
infrastruktury testowej, nie jest w tym raporcie klasyfikowany jako defekt
firmware.

Test jest zdalnym logicznym HIL sterowania i synchronizacji stanów. Bez
niezależnego pomiaru analogowego lub obecności operatora przy urządzeniu nie
przyznaje fizycznego AUDIO PASS.
