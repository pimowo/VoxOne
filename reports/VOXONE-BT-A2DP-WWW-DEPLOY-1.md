# VOXONE-BT-A2DP-WWW-DEPLOY-1

Data: 2026-11-02, 13:38–13:45 CET. Wynik zbiorczy: **PARTIAL**. Obie
zatwierdzone aktualizacje przez WWW mają PASS, natomiast pojedynczy zbiorczy
HIL zatrzymano przed pełną sekwencją po niejednoznacznym oknie pomiarowym
PAUSE. Nie wykonywano drugiej próby.

## Obrazy i aktualizacje

- VoxOneBT: obraz z commita `d76b13911944a413145e54f07e193040a4d87418`,
  1147872 B, SHA-256
  `6cd1e9f52527a931799cddd10f6e2e7b287a3722e42c6049388ee342031ba744`.
  Wewnętrzny manifest obrazu potwierdził `0.6.7-dev` i PROTO 2. Przed
  wdrożeniem moduł raportował tę samą wersję tekstową `0.6.7-dev`/PROTO 2,
  dlatego runtime version sama nie rozróżnia bajtowo starego i nowego obrazu.
  Jeden `POST /update/bt` zwrócił `202 STAGED`; sender potwierdził wszystkie
  1147872 B, `FW_VERIFY`, `FW_OK`, restart BT, `PENDING_VERIFY`, następnie
  `OTA_STATE VALID`. Terminalny odczyt miał `state=9`, `error=0`,
  `confirmedVersion=0.6.7-dev`. Mechanizm WWW po sukcesie wykonał swój
  zaprojektowany, kontrolowany restart MAIN; log zawiera kolejno terminalny
  sukces BT, quiesce sieci i `restarting VoxOne after successful update`.
- MAIN L0: przed wdrożeniem `0.8.0-dev`/`bf1d147`, `app0 VALID`, początkowy
  `bootId=ca5d5657`; po kontrolowanym restarcie kończącym update BT
  `bootId=72befcbc`. Obraz MAIN-only z commita
  `4ef1e53e32d78af94165addf3d2953e0b3939451`, 1764656 B, SHA-256
  `4dd6af3c3b3da4b5a7b9b08bb8b979ed44de98f81b95afae4466dea530873ed6`.
  Jeden `POST /update` z `updatetarget=firmware` zwrócił `200 OK`. Po
  kontrolowanym restarcie monitoring uchwycił `app1 PENDING_VERIFY`, health
  `stabilizing`, następnie `validated`, `app1 VALID`, READY i nowy
  `bootId=e7582463`. WWW pozostało `READY`/`verified`/`compatible` z tym samym
  assetSet `1d7762d8eac4570ba7bee2954ac86abb3d32af61842b681989a9219275915c61`.

Nie wysyłano SPIFFS ani full-flash. Nie używano esptool ani USB do
programowania, nie kasowano NVS/parowań, nie zmieniano SECURITY i nie
ponawiano żadnego uploadu.

## Zbiorczy HIL A2DP/AVRCP

Raspberry zachował istniejący bond z `30:76:F5:EF:F3:5A`; BlueALSA 4.3.1
użyła A2DP Source/SBC i WAV PCM16 stereo 48 kHz o SHA-256
`d0b913799f15bd0f30715ba975c6b1b50b405c5871cf7afa39a6fdbf763a0d41`.

- CONNECT bez PCM dał `BT/STOP/stopped`, więc brak autoplay po samym
  połączeniu ma PASS.
- Uruchomienie PCM dało `BT/PLAY/playing`. UART potwierdził I2S1 RX i I2S0 TX:
  `rx=45997 B/s`, `tx=45792 B/s`, `bytes=230400`, `txBytes=229376`, bez
  read/write errors i short writes. Naprawiony A2DP PLAY gate ma w tym
  zakresie fizyczny PASS.
- Pojedynczy ręczny `webtransport=toggle` dał w WS `PAUZA/paused`, a UART
  zapisał `route=BT rate=0`. Następny okresowy raport obejmował jednak część
  interwału sprzed PAUSE i nadal miał `rx=87578 B/s`, `tx=87783 B/s` oraz
  `txBytes=669696`. Runner wymagał w tym zbyt krótkim oknie jednocześnie
  `RX>0` i `TX=0`, więc zgłosił STOP. To nie jest wystarczający dowód awarii
  firmware ani PASS wyciszenia: okno agregacji było niejednoznaczne.
- Zgodnie z zakazem ponowienia nie wykonano drugiego pomiaru. Jawny PLAY po
  PAUSE, planowany disconnect/reconnect i `RADIO -> BT -> RADIO` pozostały w
  tej sesji niewykonane. Nie było odsłuchu analogowego, więc AUDIO PASS nie
  jest przyznany.

Cleanup zatrzymał wyłącznie własny proces `aplay`, rozłączył peer i przywrócił
dokładnie stan sprzed wdrożenia: stacja #3 `Italo4you`, RADIO/PLAY/playing,
`volume=7`, `volume100=12`, MUTE OFF, balance 0. Końcowo VoxOneBT jest online
`0.6.7-dev`/PROTO 2, a peer pozostaje `Paired=yes`, `Bonded=yes`,
`Connected=no`.

## Stabilność i dowody

Końcowy L0 ma build `4ef1e53`, `app1 VALID`, READY, WWW READY/verified,
`bootId=e7582463`, VoxOneBT online i RADIO PLAY. Pełny pasywny UART ma 111270 B
i 2001/2001 chunków; rekonstrukcja chunks jest identyczna z raw,
`unwrittenReadBytes=0`. Manifest jest `INTERRUPTED` wyłącznie z powodu
kontrolowanego zakończenia loggera po zadaniu i poprawnie rejestruje trzy
bootId związane z dwoma oczekiwanymi restartami. W całym zapisie nie ma
panic, assert, WDT, stack canary ani korupcji heap.

Dowody i sumy: `/home/piotrek/voxone-hil/VOXONE-BT-A2DP-WWW-DEPLOY-1-20261102T133800+0100/`.
Nie wykonano commit ani push.
