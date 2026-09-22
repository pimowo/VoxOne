# AUDIO ARCHITECTURE

## Current runtime status

MAIN has one final I2S TX owned by `AudioOutputManager`. RADIO and
PLAY_MEDIA feed signed 16-bit stereo PCM through the shared `PcmGainOutput`;
logical volume remains 0..100 and the current maximum Q15 gain is 16384
(0.5 amplitude, about -6.02 dB). The new ESP-IDF I2S driver uses 8 DMA
descriptors of 512 frames each. RadioService keeps an 8192-byte MP3 input
buffer, reads at most 4096 available network bytes per App iteration and
passes at most 4608 decoded PCM bytes per write.

External BT PCM still needs the future `BtPcmInput` I2S RX slave and source
router. A Ready UART module alone does not permit BT audio to claim MAIN I2S.
VoxOneBT is separate firmware: UART 115200 8N1 carries protocol v1 control
and status, while VoxOneBT is Philips-I2S master TX for signed 16-bit stereo
PCM at the negotiated native sample rate and MAIN will be slave RX.
The local-A2DP description below is historical.

Kontrakt docelowy; opis aktualnego DESK jest oddzielony od planu. Normatywne
decyzje projektu: [MASTER_SPEC](MASTER_SPEC.md).

## DESK — stan obecny

`AudioOutputManager` jest jedynym właścicielem I2S TX/PCM5102A. Dzierżawę
posiada najwyżej jeden `AudioOutputOwner`: Bluetooth albo Radio (PlayMedia
jest zarezerwowany w enum, bez runtime). `PcmGainOutput` stosuje ten sam gain
PCM do obu źródeł: logiczna głośność 0..100, amplituda
`pow(volume / 100, 2.2)`, przeliczenie na Q15. AVRCP jest tylko granicą
protokołu/synchronizacji z telefonem, nie osobnym gainem. Ten model uznajemy
za rozwiązany; nie przywracać oddzielnych krzywych BT/Radio.

Radio dekoduje HTTP MP3 przez Helix. BT używa A2DP Sink/AVRCP. Oba źródła
nie mogą jednocześnie pisać do I2S. PLAY_MEDIA, I2S RX i TDA7719 nie są
obecnie uruchomione. TestTone jest starszą niezależną ścieżką, nie aktywnym
źródłem runtime.

## Model źródeł — do implementacji

`baseSource = STOP | RADIO | BT` opisuje intencję użytkownika. Osobno
`temporaryOverride = NONE | PLAY_MEDIA` wyznacza chwilowego producenta PCM.
`activeAudio = temporaryOverride == PLAY_MEDIA ? PLAY_MEDIA : baseSource`.
PLAY_MEDIA nie występuje w ręcznym cyklu źródeł.

Przed override Main zapamiętuje bazę, wybraną stację/stan transportu oraz
logical volume; zwalnia bieżącą dzierżawę, uruchamia PlayMedia, a po końcu,
błędzie lub timeout zwalnia ją i przywraca bazę oraz volume. Z RADIA powraca
ta sama stacja; ze STOP pozostaje STOP. HA odpowiada za kolejkę żądań,
VoxOne za snapshot i restore. Gdy użytkownik jawnie zmieni bazę podczas
override, przyszły SourceManager musi jednoznacznie rozstrzygnąć, czy
przywrócić nową intencję; nie przywracać ślepo starej bazy.

Powrót BT bez ręcznego parowania, wyboru źródła i restartu to kryterium
akceptacji, **nie potwierdzona cecha lokalnego DESK**. Przy obecnym
jednoukładowym lifecycle B przełączenie BT/RADIO zatrzymuje profil, a
telefon inicjuje ponowne połączenie. W docelowym dual-MCU VoxOneBT telefon
pozostaje połączony: Main wysyła PAUSE, odtwarza PLAY_MEDIA, następnie
PLAY, jeśli BT grał przed override; BT PCM wraca bez reconnectu. Nie mylić
obu scenariuszy.

## Jeden AudioRouter — docelowo

```text
RADIO PCM / BT PCM (lokalny lub I2S RX) / PLAY_MEDIA PCM
                         |
                     AudioRouter
                         |
                 common volume 0..100
                         |
                       I2S TX
                         |
                  jeden końcowy DAC
```

Router arbitruje jednego aktywnego producenta, format PCM/sample rate,
bezpieczny drain oraz odbiór I2S RX w wariancie z VoxOneBT. Nie jest
gotową klasą. Bluetooth na osobnym ESP32U nie dostaje drugiego DAC:
wysyła I2S PCM do Main. I2S RX, synchronizacja zegarów, buforowanie i
latencja wymagają osobnego etapu sprzętowego.

`AudioProcessor` jest dalszą opcjonalną warstwą wyjścia, nie drugim
źródłem. `NoAudioProcessor` dla Desk/DIN; `TDA7719Processor` dla Salon
z API `setInput`, `setVolume`, `setMute`, `setBass`, `setMid`, `setTreble`,
`setBalance`, `setFader`, `setLoudness`. Mapowanie do analogowego TDA7719
i relacja do cyfrowego gainu muszą zostać zweryfikowane przed implementacją;
logiczna skala core pozostaje 0..100.

## Przyszła polityka BT

BT CONNECTED nie musi zmieniać bazy. BT PLAYING może ją zmienić według
przyszłego `bluetooth.auto_switch_on_play` (proponowany default `true`).
Jeśli trwa PLAY_MEDIA, nie przerywać go: zapisać pending `baseSource=BT`
i zastosować po zakończeniu override. Dwuklik enkodera ma przełączać
RADIO -> BT -> RADIO, nigdy PLAY_MEDIA.
## Dual-MCU preparation checkpoint

`src/audio/AudioRouter.h` now defines the future route/input boundary
(`Radio`, `BluetoothRx`, `PlayMedia`) but does not instantiate a router or
change `AudioOutputManager`. MAIN no longer contains local A2DP; RADIO
continues using the shared gain and I2S TX. VoxOneBT produces 16-bit stereo PCM;
the future Main I2S RX is a slave to the BT module's clock. The router will
arbitrate one active PCM producer, adapt sample rate, and pass it through
the existing common gain to the single PCM5102A output.

Clock-domain crossing, PCM buffering/underrun behavior, sample-rate changes,
and isolation when UART/BT power is absent remain to be implemented and
tested. The interface alone is not an operational remote audio path.
