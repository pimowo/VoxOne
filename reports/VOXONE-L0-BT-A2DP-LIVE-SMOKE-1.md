# VOXONE-L0-BT-A2DP-LIVE-SMOKE-1

Data: 2026-11-02 (Europe/Warsaw). Kandydat MAIN: `0.8.0-dev` / `bf1d147`.

## Wynik

**PARTIAL — STOP przed audio.** Preflight infrastruktury, identyfikacja celu i niezmienność L0 zostały potwierdzone. Jedna próba parowania z jedynym autoryzowanym urządzeniem zgłosiła chwilowo `Pairing successful` i `Paired: yes`, po czym BlueZ zgłosił `Paired: no` i `Connected: no`. Końcowy readback: `Paired: no`, `Bonded: no`, `Trusted: no`, `Connected: no`. Nie było trwałego bondingu, PCM A2DP ani transmisji WAV. Nie wykonano przejść RADIO/BT, disconnect/reconnect ani odsłuchu. Nie przypisywać im PASS ani nie zaliczać tego jako BT LIVE smoke lub pełnego CHAOS.

## Preflight i identyfikacja

- Nie wykryto innego aktywnego procesu HIL; port `L0_LOG_RX` był wolny. Pi 3B udostępniał `L0_NATIVE` (`ttyACM0`), `L0_LOG_RX` FT232R `A7039N0Z` (`ttyUSB0`) i `VOXONEBT_USB` (`ttyACM1`). Port serwisowy VoxOneBT nie był otwierany.
- BlueZ `5.82`, BlueALSA `4.3.1`; `bluealsa.service` i `bluealsa-aplay.service` aktywne. `bluealsa-cli status` reklamował A2DP-source/SBC oraz A2DP-sink/SBC; `hci0` był włączony, rfkill soft/hard `no`. Nie było aktywnego PCM ani konfliktu z ALSA. Profil/PCM **rzeczywiście zestawionego połączenia** pozostaje UNVERIFIED.
- Pi: `vcgencmd get_throttled=0x50000` przed i po próbie. Bity aktualnego stanu były zerowe; ustawione były historyczne flagi undervoltage/throttling. Nie zaobserwowano bieżącej zmiany tych flag.
- Ograniczony scan wykrył dokładnie `VoxOneBT-EFF35A`, publiczny MAC `30:76:F5:EF:F3:5A`, klasę audio-card i UUID Audio Sink. MAC odpowiada wcześniejszemu niezależnemu skanowi opisanemu w `VOXONE-RC1-L0-RADIO-AND-A2DP-CLOSURE-1`; bieżący WS L0 raportował nazwę `VoxOneBT-EFF35A`, firmware `0.6.7-dev`, PROTO 2, online. Tożsamości nie wywiedziono wyłącznie z końcówki nazwy.
- Skan zakończono; końcowy `bluetoothctl show` wskazywał `Discovering: no`. Nie skanowano ponownie po nieudanym utrwaleniu parowania.

## Przebieg ograniczony

1. Przed działaniem L0: READY, `bootId=ca5d5657`, `app0 VALID`, WWW READY/verified/compatible. WS: `RADIO/PLAY/playing`, stacja `#1 Afterhours` (AH.FM), `volume100=4` (surowe volume `2`), MUTE OFF, balance `0`, VoxOneBT disconnected. Free heap `117580 B`, PSRAM free `7800775 B`, RSSI `-60 dBm`.
2. Uzbrojono niezależny RX-only logger FT232R: 09:14:44.298. Potwierdzono fizyczny port i zapis UART. Scan o limicie 30 s pokazał docelowy MAC i nazwę; tuż przed parowaniem `Paired/Bonded/Trusted/Connected: no`.
3. Wykonano **jedną** standardową próbę `bluetoothctl --timeout 20 pair 30:76:F5:EF:F3:5A`. Log klienta: `Connected: yes`, `ServicesResolved: yes`, `Paired: yes`, `Pairing successful`, następnie `ServicesResolved: no`, `Paired: no`, `Connected: no`. Polecenie zakończyło się po ok. 20 s; końcowy odczyt potwierdził brak bondu. Nie użyto PIN-u, nie zmieniono ustawień adaptera ani nie parowano z innymi urządzeniami.
4. Po tej anomalii odstąpiono od `trust`, `connect` i `aplay`; nie uruchomiono `chaos_v2.py`, aby jego warunek wejścia (sparowany, trusted, rozłączony peer) nie został obchodzony. Powodu cofnięcia parowania **nie ustalono**. Zbieżność czasowa z końcem scanu nie dowodzi przyczyny; `Pairable: no` po stronie Pi również nie wystarcza do jej przypisania.
5. Logger zatrzymano celowo o 09:18:47.264; manifest ma `INTERRUPTED`, nie PASS za żądane 900 s. Jest to odrębne od potwierdzonej integralności 838 B zapisu.

## Postcheck, bezpieczeństwo i dowody

- L0: READY, `bootId=ca5d5657`, `app0 VALID`, WWW READY/verified/compatible, RADIO/PLAY/playing, `#1 Afterhours`, `volume100=4`, surowe volume `2`, MUTE OFF, balance `0`, BT disconnected. Metadane AH.FM mogły zmieniać się naturalnie; source, stacja i nastawy nie zostały zmienione. Free heap `117668 B`, PSRAM free `7805431 B`, RSSI `-58 dBm`.
- Pi: trzy porty USB nadal obecne, BlueALSA i bluealsa-aplay aktywne, `hci0` powered, `Discovering: no`, `throttled=0x50000`. Peer nie był sparowany ani połączony; nie ma niepotrzebnej aktywnej sesji A2DP. Nie wymagał przywracania L0, ponieważ nie wysłano mu komendy zmieniającej stan.
- UART: `838 B`, 5 chunków; rekonstrukcja chunków = surowe bajty, rozmiar manifestu = rozmiar RAW, `unwrittenReadBytes=0`, `bootIdChanged=false`. Cztery wpisy ENDURANCE pokazywały `playerRunning=1`, `audioBuffer=282989–283615 B`, RSSI `-62…-59`; w zarejestrowanym UART brak panic/assert/WDT i błędów audio. Nie jest to dowód jakości analogowej.
- Dowody na Pi: `/home/piotrek/voxone-hil/logs/l0/VOXONE-L0-BT-A2DP-LIVE-SMOKE-1-20261102T0814Z/`. SHA256: `uart.raw` `815ff58c87943a8ce26a05b0b2934ee58d1c3f0615c5135c4563624fe9ee7f10`; `uart-chunks.jsonl` `847414fc9fb2e850fbbadc1df6f76e1277f03ce6c5f2abe50267f8bb7699af45`; `manifest.json` `360eb0eb4bca2f07e2fb56d83eadf78e8b0dff9b4cf7c60c30858272910d1065`.

## Otwarte warunki

Brakuje trwałego Pair/Bond, potwierdzenia PCM A2DP-source/SBC w aktywnej sesji, transmisji WAV, readbacku L0 BT, dwóch cykli odtwarzania, disconnect/reconnect, RADIO→BT→RADIO i fizycznej oceny audio. Najbliższy krok to ograniczona diagnostyka **samego parowania** (rejestracja zdarzeń BlueZ po obu stronach i potwierdzenie wymaganego trybu/okna pairing), bez resetu ani zmiany firmware. Dopiero po trwałym `Paired: yes`/`Bonded: yes` można uruchomić właściwy bounded LIVE smoke. Nie stwierdzono defektu firmware MAIN na podstawie tej próby.

TODO: dopisano aktualny wynik infrastruktury i nieudanego utrwalenia parowania; istniejące bramki BT/CHAOS pozostają otwarte.
