# STATE MACHINE

## Current runtime policy

The implemented base source intent is STOP / RADIO / BT. PLAY_MEDIA/TTS is a
temporary highest-priority override. Manual double-click and BT
CONNECTED/DISCONNECTED events select a new base source as STOP/PAUSE; they do
not issue automatic PLAY. CONNECTED/DISCONNECTED automation is logically
implemented and hardware-test-pending until VoxOneBT is connected.

Status: obecny App ma source intent `STOP | RADIO | BT`, RadioService ma
`Idle | WaitingForNetwork | Starting | Playing | Error`, a UiMode ma HOME,
VOLUME i BT NAV (RadioList jest placeholderem). PLAY_MEDIA i poniższy
podział na bazę/override są **planem**. Aktualne zachowanie i ograniczenia
opisuje [MASTER_SPEC](MASTER_SPEC.md).

## Docelowy arbitraż

`baseSource = STOP | RADIO | BT` pozostaje intencją po przejściu UI i
nie jest listą aktywnych fizycznych dzierżaw. Osobno
`temporaryOverride = NONE | PLAY_MEDIA`. Efektywny producent PCM to
PLAY_MEDIA podczas override, w pozostałym czasie baza (albo cisza STOP).
`AudioOutputOwner::PlayMedia` jest fizycznym trzecim ownerem, ale nie
równorzędną bazą.

| Zdarzenie | Baza | Override | Akcja docelowa |
|---|---|---|---|
| użytkownik wybiera RADIO/BT/STOP | nowy wybór | NONE | zatrzymaj starego producenta, zwolnij I2S, uruchom nowego |
| HA zleca media przy RADIO | RADIO | PLAY_MEDIA | zapisz ID stacji/volume, zatrzymaj stream, odtwórz media |
| HA zleca media przy BT | BT | PLAY_MEDIA | utrzymaj połączenie BT w dual-MCU, PAUSE, odtwórz media |
| media kończą się/błąd/timeout | zachowana baza lub nowsza intencja | NONE | posprzątaj lease, przywróć volume i bazę; tę samą stację RADIO lub PLAY BT |
| BT CONNECTED | bez automatycznej zmiany | bez zmiany | sam transport nie przełącza bazy |
| BT PLAYING | BT, jeżeli przyszła opcja auto-switch włączona | zachowaj PLAY_MEDIA, jeśli trwa | zmianę bazy zastosuj po override |
| dwuklik enkodera | RADIO <-> BT | bez zmiany | ręczne źródło bazowe; PLAY_MEDIA nie jest w cyklu |

Przejścia muszą być odporne na stare callbacki/generation i na brak sieci.
MAIN nie używa już lokalnego lifecycle Bluetooth. `BT` pozostaje źródłem
logicznym; bez aktywnego PCM RX wybór BT jest bezpiecznie odrzucany i nie
zajmuje I2S. VoxOneBT ma własny A2DP/AVRCP, ale hardware dual-MCU oraz
powrót po PLAY_MEDIA nie są jeszcze zrealizowane.

Historycznie M1 wykorzystywał STOP/test tonu do weryfikacji fundamentu;
TestTone nie należy do aktywnego runtime DESK.
