# VOXONE-BT-VOLUME-VU-PHYSICAL-PROBE-1

Data: 2026-11-02, Europe/Warsaw. Wynik: **BLOCKED / SAFE STOP PRZED
ZAPISEM FIRMWARE**. Diagnostyczny HIL nie został rozpoczęty, a przyczyna
historycznego `peak=0` pozostaje **UNVERIFIED**.

## Wynik wykonawczy

Pierwsze i jedyne żądanie `POST /update/bt` zostało odrzucone przez warstwę
SECURITY jako HTTP `403` z ciałem `{"error":"request_rejected"}`. Produkcyjny
kontrakt klasyfikuje `/update/bt` jako `ProtectedUpdate` i wymaga poprawnego
same-origin `Origin` również w stanie OPEN. Żądanie nie zawierało tego
nagłówka. Readback bezpośrednio po STOP miał:

- `receiving=false`, `pending=false`, `active=false`;
- `state=0`, `phase=0`, `error=0`;
- `totalBytes=0`, `confirmedBytes=0`, `percent=0`;
- brak `FW_BEGIN`, `FW_VERIFY`, `FW_OK` i `[AUDIO_PATH]` w obu logach.

Nie był to częściowy zapis ani niejednoznaczne HTTP 200: staging i sender w
ogóle nie wystartowały. Zgodnie z zakazem automatycznego ponawiania nie
wysłano drugiego żądania z poprawionym nagłówkiem. Nie wykonano A2DP CONNECT,
PCM, PAUSE/PLAY ani aktualizacji powrotnej. Rollback d76b139 był zbędny, bo
obraz diagnostyczny nigdy nie trafił do urządzenia.

## Sonda i bramka offline

Profil `esp32dev-audio-path-probe` pozostaje osobnym, domyślnie wyłączonym
wariantem (`VOXONE_AUDIO_PATH_PROBE_ENABLED=0` w normalnym buildzie). Sonda:

- czyta RAW i post-volume bez modyfikacji PCM lub gain;
- w jednym przebiegu liczy peak oraz niezerowe próbki kanałów;
- raportuje callbacks/bytes, ABSVOL i rzeczywisty `volumeFactor`;
- raportuje żądania i rzeczywiste wywołania/bajty `i2s_write`, zakończone
  requesty, inactive, errors i każde short write;
- nie loguje, nie kopiuje buforów i nie wykonuje operacji blokującej w callbacku;
  skan PCM odbywa się poza krótką sekcją aktualizacji liczników.

Testy `A2dpAudioGate`, `RawVuMeter`, `AudioPathProbe`, `I2sWriteProbe`,
`AvrcTgVolumeSession`, `VolumeSessionProtocol` i rzeczywisty
`A2DPDefaultVolumeControl` przeszły zwykle i pod ASan/UBSan: **14/14 PASS**.
Build `esp32dev` przeszedł przy RAM 44 880 B / Flash 1 141 405 B; czysty build
probe przeszedł przy RAM 44 952 B / Flash 1 142 897 B. Oba obrazy mają dokładnie
jeden poprawny manifest `VoxOneBT V0`, FW `0.6.7-dev`, PROTO 2. Normalny ELF
nie zawiera formatu `[AUDIO_PATH]`; diagnostyczny go zawiera.

| Obraz | Bajty | SHA-256 | Wynik |
|---|---:|---|---|
| diagnostyczny `esp32dev-audio-path-probe` | 1 149 472 | `72659798c65e8839b811b5fcda946e8028898c3fe833df7106ef378a6da9b645` | build/manifest PASS, **nie wdrożony** |
| zatwierdzony rollback/produkcja d76b139 | 1 147 872 | `6cd1e9f52527a931799cddd10f6e2e7b287a3722e42c6049388ee342031ba744` | SHA zgodny, urządzenie pozostało na tym obrazie |

## Preflight fizyczny

- brak aktywnego procesu HIL; oba locki runnerów były wolne;
- L0: `4ef1e53`, `app1 VALID/validated`, READY, WWW READY/verified,
  `bootId=e7582463`;
- VoxOneBT: `0.6.7-dev` z obrazu d76b139, online, PROTO 2, peer
  Paired/Bonded i rozłączony;
- stan: stacja #3 `Italo4you`, RADIO/PLAY, volume100=12 (`volume=7/50`),
  MUTE OFF, balance 0;
- BlueALSA/BlueZ/bluealsa-aplay były aktywne, bez procesu PCM/HIL;
- porty potwierdzone przez `/dev/serial/by-id`: FT232R `A7039N0Z` oraz
  VoxOneBT `5B09012537` (`USB Single Serial`, aktualnie ttyACM1).

Oba ograniczone loggery użyły po jednym otwarciu na całą planowaną sesję,
115200 8N1, RX-only fd, flow control i HUPCL wyłączone. BT USB dostarczył
okresowe `[DIAG]`, FT232R dane L0, a L0 zachował ten sam bootId. Nie wykryto
markera resetu, panic/assert/WDT. Manifesty są `INTERRUPTED` wyłącznie dlatego,
że po bezpiecznym STOP loggery zakończono przed żądanym czasem; integralność
capture ma `invalidReason=null`, `unwrittenReadBytes=0`.

## Aktualizacje i planowany pomiar

| Etap | Wynik |
|---|---|
| Aktualizacja diagnostyczna VoxOneBT przez WWW | **NOT STARTED / HTTP SECURITY REJECTED**; 403 przed stagingiem, 0 B sendera |
| Terminalne FW_VERIFY/FW_OK/VALID | **NOT OBSERVED**, ponieważ sender nie wystartował |
| Fizyczny CONNECT -> PLAY -> PAUSE -> PLAY | **NOT RUN** po warunku STOP |
| Aktualizacja powrotna d76b139 | **NOT REQUIRED / NOT RUN**; produkcyjny obraz nie został zastąpiony |

Nie istnieją więc uczciwe próbki RAW/post-volume/MAIN z tej sesji:

| Okno | RAW peak/nonzero | post-volume peak/nonzero | I2S modułu | MAIN peak/RX/TX | ABSVOL/factor |
|---|---|---|---|---|---|
| PLAY | NOT MEASURED | NOT MEASURED | NOT MEASURED | NOT MEASURED | NOT MEASURED |
| PAUSE | NOT MEASURED | NOT MEASURED | NOT MEASURED | NOT MEASURED | NOT MEASURED |
| PLAY po PAUSE | NOT MEASURED | NOT MEASURED | NOT MEASURED | NOT MEASURED | NOT MEASURED |

Nie przyznano PASS ani FAIL żadnej warstwie amplitudy i nie przygotowano
poprawki produkcyjnego gain/VU.

## Stan końcowy i dowody

Końcowy readback jest zgodny ze stanem początkowym: L0 `4ef1e53`, `app1`
VALID/validated, READY, WWW READY/verified, `bootId=e7582463`; VoxOneBT
`0.6.7-dev`/PROTO 2 online, peer rozłączony; RADIO/PLAY `Italo4you`,
volume100=12, MUTE OFF, balance 0. MAIN, SPIFFS, NVS, SECURITY, DSP, bonding i
firmware VoxOneBT nie zostały zmienione. Nie wykonano OTA MAIN, USB flash,
resetu, commit ani push.

Dowody i komplet sum:
`/home/piotrek/voxone-hil/VOXONE-BT-VOLUME-VU-PHYSICAL-PROBE-1-20261102T190255+0100/`.
Istotne SHA logów: BT USB raw
`31abe4100f90de33d4119bef7d7a9232dd658babad151c0c4461c581ece80686`,
L0 UART raw
`9e5c28cd0e27deb41e44d202f0d44a81219b462e2446759007732a5b0f08d016`.
`SHA256SUMS.verify` potwierdza komplet plików.

Minimalny retest nadal jest dokładnie jeden: po nowej jawnej decyzji wykonać
pojedynczy upload z wymaganym `Origin: http://192.168.1.56`, terminalny odczyt
sendera, krótki znany WAV PLAY -> PAUSE -> PLAY i powrót do d76b139. Nie
powtarzać stressu ani wcześniejszych zaliczonych scenariuszy.
