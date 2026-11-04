# VOXONE-RC1-L0-BT-LIFECYCLE-REMAINING-PREFLIGHT-1

Data: 2026-11-03. Werdykt wykonawczy: **BLOCKED dla kontynuacji BT
lifecycle bez zmiany firmware**. Stanowisko i zdalne sterowanie są dostępne,
ale nierozstrzygnięta anomalia zerowych próbek po PAUSE → PLAY nie ma
wystarczającej obserwowalności w produkcyjnym VoxOneBT `d76b139`.

Etap był wyłącznie odczytowym audytem i preflightem. Nie uruchomiono nowych
cykli, PCM ani komend transportowych. Nie wykonano OTA, flashowania, restartu,
resetu, zmiany konfiguracji ani ingerencji w przewody.

## Rozliczenie SourceManager

Zachowane zostają wszystkie nowe wyniki PHYSICAL PASS dla kontrolowanego
peer-a A2DP: disconnect podczas PLAY, fallback RADIO PLAY/STOP, reconnect bez
autoplay, ręczny priorytet RADIO oraz stabilność normalnego powrotu. Nie
powtarzano tych scenariuszy.

Utrata peer-a A2DP nie jest utratą modułu VoxOneBT. Zanik runtime/heartbeat
samego modułu oraz terminalna stabilność po jego powrocie pozostają
**BLOCKED / PENDING**. Nie ma produkcyjnej komendy ani zatwierdzonego
izolatora zatrzymującego wyłącznie heartbeat/UART bez resetu, odłączenia
przewodu albo innego firmware.

## Chronologia stanu L0

| Moment | Stan |
|---|---|
| Koniec stress V2, 2026-11-02 | #3 `Italo4you`, RADIO/PLAY, volume100=12 (`volume=7`), MUTE OFF, balance 0, peer disconnected. |
| Koniec closeoutu P1, 2026-11-03 ok. 19:30 | `Italo4you`, RADIO/STOP, volume100=10, MUTE OFF, peer connected. |
| Pierwszy snapshot SourceManager, 21:04:54 | #4 `Złote Przeboje Wrocław`, RADIO/PLAY, volume100=10 (`volume=5`), MUTE OFF, balance 0, peer connected. |
| Koniec SourceManager, 21:08:37 | Dokładnie ten sam stan funkcjonalny. |
| Bieżący preflight, 21:14 | Dokładnie ten sam stan funkcjonalny; zmieniła się tylko naturalna metadata stacji. |

SourceManager preflight nie spowodował przejścia STOP → PLAY ani zmiany
stacji. Jego trzy dzienniki zawierają wyłącznie 15 operacji `ws_readback` i
zero komend mutujących. Nowy stan istniał już przed pierwszym odczytem tego
zadania, więc jego pochodzenia nie można przypisać temu HIL; mógł powstać
od działania użytkownika albo innego klienta między raportami.

Między closeoutem P1 a SourceManager głośność nie zmieniła się:
`volume100=10` pozostało `10`. Pole `volume=5` jest odpowiadającą mu
wewnętrzną skalą legacy, a nie spadkiem 10 → 5. Wcześniejsza zmiana 12 → 10
nastąpiła przed closeoutem P1 i nie ma w tym zadaniu dowodu jej sprawcy.

Nie wykonano restore: obecny stan jest późniejszym stanem użytkownika i był
stabilny przez cały SourceManager preflight oraz bieżący odczyt. Automatyczne
przywrócenie `Italo4you`/STOP nadpisałoby możliwą późniejszą intencję.

Jeżeli użytkownik jawnie zażąda powrotu do baseline'u P1, bezpieczny plan to:

1. wykonać nowy snapshot i przerwać, jeśli stan zmienił się od tego raportu;
2. potwierdzić jako cel `Italo4you`, RADIO/STOP, volume100=10, MUTE OFF oraz
   decyzję, czy zachować bieżące połączenie peer-a;
3. wysłać wyłącznie allowlistowane komendy z readbackiem po każdej zmianie;
   `playstation=3` może chwilowo uruchomić stację, dlatego końcowe `stop=1`
   jest obowiązkowe;
4. potwierdzić dwoma klientami WS dokładny stan końcowy i nie zmieniać
   volume/MUTE/balance, jeśli już odpowiadają celowi.

## Bieżący preflight

- MAIN: `0.8.0-dev`, build `4ef1e53`, profil L0, `app1/app1 VALID`, READY,
  `bootId=3cb6512e`, rollback dostępny, WWW READY/verified/compatible;
- VoxOneBT: runtime `0.6.7-dev`, online, PROTO 2, capability A2DP/AVRCP/
  ABSVOL/I2S/VU; ostatni kontrolowany produkcyjny restore zapisał dokładny
  obraz `d76b139`, lecz runtime nie eksportuje commita ani SHA, więc bieżąca
  bitowa tożsamość jest wnioskowana z historii, a nie atestowana odczytem;
- Raspberry: Bluetooth, BlueALSA i `bluealsa-aplay` active; peer
  `30:76:F5:EF:F3:5A` paired/bonded/connected; endpoint A2DP Source istnieje,
  brak aktywnego procesu PCM/HIL/loggera;
- porty logiczne są obecne i wolne; portu VoxOneBT nie otwierano;
- zasilanie: `get_throttled=0x50000` zawiera wyłącznie historyczne sticky
  bity. Bieżące bity są zerowe i od baseline'u 21:04 nie pojawił się nowy
  wpis undervoltage/throttling.

## Audyt 13 cykli i cyklu 14

Trzynaście pełnych cykli zachowuje **PHYSICAL PASS** na MAIN `4ef1e53` i
VoxOneBT `d76b139`. Każdy wykonał:

`CONNECT bez autoplay → PLAY → PAUSE → PLAY → disconnect podczas PLAY
→ RADIO`, z dwoma pełnymi oknami każdej fazy, zgodnością dwóch klientów WS,
wzrostem RX/TX w PLAY i stałym TX w PAUSE. W 41 zaakceptowanych parach nie
było przyrostu ani resetu `errors`, `writeErrors` lub `shortWrites`.
Volume 12 → 11 → 12 i MUTE ON/OFF oraz reconnect bez PCM mają osobny PASS.

Cykl 14 nie zawiódł na parserze ani widżecie VU:

- pierwsze PLAY: `peak=27/27`, około 192 kB/s RX/TX;
- PAUSE: dwa pełne okna `rx=tx=peak=0`, stałe `txBytes`;
- PLAY po PAUSE: odrzucone okno i dwa pełne okna przez około 15 s miały
  RX/TX `192166/191948/191948 B/s`, rosnące `bytes` i `txBytes`, 48 kHz oraz
  `errors=writeErrors=shortWrites=0`, lecz `peak=0/0/0`;
- znany WAV jest niezerowy w każdym 5-sekundowym oknie.

MAIN oblicza ten `peak` bezpośrednio z próbek I2S1 przed MUTE i zapisem I2S0.
Zatem MAIN otrzymywał zerowe próbki mimo aktywnych zegarów i rosnących
liczników bajtów. Nie jest to dowód awarii prezentacji VU.

Dokładne źródło zer pozostaje **UNVERIFIED**. Log nie rozdziela:

- zer dostarczonych przez BlueALSA/A2DP przed RAW callbackiem;
- wyzerowania/kwantyzacji lub braku nowych callbacków w VoxOneBT;
- aktywnego, ale zerowanego DMA/I2S przed MAIN.

Podwójne tłumienie zostało wykluczone dla zachowanego HIL: BlueALSA miała
`SoftVolume=true`, ale `Volume=127/127`, czyli gain 1,0. Jedynym potwierdzonym
tłumikiem był `A2DPDefaultVolumeControl` VoxOneBT: ABSVOL 21, factor 69/4096,
`1642 → 27`. Matematycznie ponowne zastosowanie dałoby `27 → 0`, lecz kod
wywołuje gain raz, a PAUSE/PLAY nie zmienia factor. Brak równoczesnego RAW,
post-volume i I2S nie pozwala dowieść mechanizmu.

Późniejsza sonda diagnostyczna została raz wdrożona i prawidłowo
przywrócona do `d76b139`, ale jedyny CONNECT osiągnął timeout przed PCM.
Nie powstały więc próbki rozstrzygające. Obecny produkcyjny obraz nie
zawiera tej sondy.

## Pozostałe scenariusze i wymagane dowody

Jeżeli zachowane 13 cykli wolno agregować z osobną kontynuacją na tych samych
wersjach, liczbowo pozostaje **7 pełnych cykli**: dokończony odpowiednik cyklu
14 oraz sześć dalszych, do łącznego 20/20. Każdy musi dostarczyć:

- CONNECT tego samego bondu bez autoplay: BT/STOP i zerowy PCM;
- PLAY: dwie pełne sąsiednie próbki 48 kHz z dodatnimi RX, TX i `peak`,
  rosnącymi `bytes`, `txBytes`, `lastDataMs`;
- PAUSE: odrzucone okno przejściowe, następnie dwie pełne próbki
  `tx=peak=0` i stałe `txBytes`;
- PLAY po PAUSE z identycznym dodatnim kryterium oraz brak przyrostu/resetu
  `errors`, `writeErrors`, `shortWrites` między pełnymi oknami;
- disconnect podczas PLAY, brak residual PCM/połączenia oraz fallback do
  niezmienionej intencji RADIO;
- zgodny readback dwóch klientów WS, stałe READY/bootId/build/PROTO 2,
  brak HCI allocation assert, panic, WDT, heap corruption i module offline;
- heap/PSRAM powyżej progów oraz dokładny restore stanu wejściowego,
  uwzględniający także początkowe połączenie peer-a.

Volume/MUTE, reconnect-zero-VU i zaliczone normalne przejścia nie wymagają
powtórzenia. Track title/artist nie są publikowane przez syntetyczny `aplay`;
jeśli pełna metadata jest obowiązkowa dla zamknięcia bramki, potrzebny jest
jednorazowy dowód z istniejącego fixture MPRIS, a nie powtórzenie stressu.

Agregacja 13 + 7 nie daje tego samego dowodu ciągłego heap/endurance co jedna
sesja 20 cykli. Przed przypisaniem 20/20 potrzebna jest jawna decyzja, czy
kontrakt akceptuje sumę dwóch kontrolowanych sesji na identycznych wersjach.
Bez niej zachowane 13 cykli pozostają PASS, ale nie wolno oznaczyć pełnej
macierzy 20/20.

## Zdalna wykonalność i plan ograniczonego HIL

Raspberry technicznie umożliwia w pełni zdalne sterowanie logicznym HIL:
BlueALSA Source, znany WAV, connect/disconnect, AVRCP przez MPRIS, dwa WS,
READY, UART oraz monitoring zasilania. Obecność użytkownika nie jest potrzebna
do dowodów cyfrowych; byłaby potrzebna wyłącznie do osobnego odsłuchu lub
oceny fizycznego wyświetlacza.

Nie można jednak uruchomić dotychczasowego harnessu bez zmian: wymaga on na
wejściu peer-a rozłączonego, a obecny peer jest połączony; jego restore nie
odtwarza początkowego `connected=true`. Przed przyszłym HIL trzeba poprawić
wyłącznie narzędzie, nie firmware: zapisać pełny baseline, odtworzyć stan
peer connected/disconnected i przerwać przy każdym dryfie niepochodzącym od
harnessu. Zmiana wymaga testów offline i osobnej akceptacji okna HIL.

Plan po usunięciu blockerów:

1. ustalić metodę rozstrzygnięcia zerowych próbek; obecny produkcyjny
   firmware nie udostępnia wymaganych RAW/post-volume/I2S;
2. podjąć decyzję o dopuszczalności agregacji 13 + 7;
3. przygotować i przetestować offline continuation harness z numeracją
   14–20, bez powtarzania volume/MUTE/reconnect-zero-VU i z restore pełnego
   bieżącego baseline'u;
4. w wyłącznym oknie wykonać siedem cykli na `4ef1e53`/produkcyjnym
   VoxOneBT, z pasywnym UART L0 i monitoringiem Raspberry;
5. zakończyć co najmniej 180 s stabilności, dwoma pełnymi snapshotami WS,
   kontrolą loggera i dokładnym restore.

Punkt 1 jest obecnie blockerem: poprzednie raporty wymagają krótkiej sondy
diagnostycznej, a jej wdrożenie byłoby zmianą firmware zabronioną w tym
zadaniu. Nie wolno zastępować tego dowodu samymi licznikami bajtów ani
zwiększeniem amplitudy fixture, które mogłoby zamaskować kwantyzację.

## Kryteria natychmiastowego STOP

- utrata READY, zmiana `bootId`, OTA inne niż VALID lub WWW inne niż READY;
- zmiana build/profile, VoxOneBT offline, PROTO inne niż 2 albo zmiana
  tożsamości/bondingu peer-a;
- nowy incydent undervoltage, aktywny bit throttlingu lub utrata usługi
  Bluetooth/BlueALSA;
- zajęty lock/port, niepełny logger, niezapisane bajty UART albo aktywny obcy
  proces PCM/HIL;
- panic, assert, WDT, HCI allocation assert, stack canary, heap corruption,
  module offline lub błąd UART/I2S;
- pojedynczy timeout CONNECT; bez automatycznego retry;
- autoplay po CONNECT, rozbieżność dwóch klientów WS albo trwały mismatch
  source/transport;
- brak dodatniego RX/TX/`peak` w PLAY, niezerowy TX/peak albo wzrost `txBytes`
  w PAUSE, ponowne `peak=0` po resume w oknie przejściowym i dwóch pełnych
  oknach;
- przyrost lub reset `errors`, `writeErrors`, `shortWrites` pomiędzy pełnymi
  ustabilizowanymi oknami;
- `freeHeap < 30000 B`, `psramFree < 1 MiB`, terminalny spadek heap ponad
  12 KiB lub PSRAM ponad 64 KiB;
- nieoczekiwana zmiana stacji, PLAY/STOP, volume, MUTE, balance lub połączenia
  peer-a, której nie wysłał harness;
- brak dokładnego restore. Po STOP zabezpieczyć dowody, zakończyć PCM,
  rozłączyć wyłącznie peer-a sterowanego przez harness i nie ponawiać
  scenariusza bez analizy.

## Dowody

- bieżący snapshot:
  `/home/piotrek/voxone-hil/bt-lifecycle-remaining-preflight-085044c2/snapshot.json`,
  SHA-256 `f808f51a69bb7ba49a5cb24b993313d7ba2045d443f45c6fbe9af353f57cc904`;
- stress V2 `RESULT.json`: SHA-256
  `f041a80a1ecc3ffd39cfee57632e535e9c7c1b33b4ad37073951a6a331a5e0d2`;
- stress V2 `events.jsonl`: SHA-256
  `115d7fb3afab83bf58571a3ed5d7c138887ed42e2e3318cda55924a7286bed62`;
- stress V2 UART raw: SHA-256
  `d3851b844f789757e6cfca797eb634b542601760ea151902a2c4f3ece18f57d8`;
- WAV 60 s: SHA-256
  `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1`.

`RC1-BT-LIFECYCLE` pozostaje **OPEN / PARTIAL**. Infrastruktura zdalna jest
gotowa, ale bez rozstrzygnięcia cyklu 14 i decyzji o agregacji nie ma podstaw
do uruchomienia dalszych cykli ani przypisania pełnego PASS.
