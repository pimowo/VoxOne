# VOXONEBT-PAIRING-HCI-CAPTURE-1

Data: 2026-11-02 (Europe/Warsaw). Raspberry `voxone-bridge` / BlueZ 5.82 / BlueALSA 4.3.1; L0 `bf1d147`; cel: `VoxOneBT-EFF35A`, publiczny MAC `30:76:F5:EF:F3:5A`.

## Wynik i zakres

**PAIR/BOND PASS w tej jednej próbie; przyczyna historycznej utraty bondu UNVERIFIED.** Bieżący ślad potwierdza zakończenie SSP i uwierzytelnienia, utworzenie klucza oraz trwały readback `Paired=yes`, `Bonded=yes` po rozłączeniu i zakończeniu skanu. Połączenie ACL zakończył lokalny host Raspberry po SDP. Nie było komendy `connect`, transmisji WAV ani potwierdzonej sesji A2DP/PCM. Nie przypisywać temu BT LIVE smoke ani audio PASS.

## Preflight i wykonanie

- Uruchomiony wcześniej przez użytkownika `btmon` (PID 12422) zapisywał `/tmp/voxonebt-pairing.btsnoop`; potwierdzono otwarty deskryptor, nagłówek `btsnoop` i rosnący plik. Uprawnienia pliku zmieniono z `0644` na `0600` przed parowaniem.
- L0: READY, `bootId=ca5d5657`, `app0 VALID`, WWW READY; RADIO/PLAY, stacja #1, `volume100=4`, MUTE OFF, balance 0; VoxOneBT `0.6.7-dev`, PROTO 2, online, `btConnected=false`. Pi: `hci0` powered, BlueALSA A2DP-source/SBC; `get_throttled=0x50000` (historyczne flagi, bez bieżących bitów throttling).
- Aktywny scan ponownie wykrył dokładny MAC i nazwę oraz UUID Audio Sink. Tuż przed próbą `Paired=no`, `Bonded=no`, `Trusted=no`, `Connected=no`. Wysłano dokładnie **jedną** komendę `pair 30:76:F5:EF:F3:5A` z klienta `bluetoothctl` z agentem `NoInputNoOutput`; nie wykonano `connect` ani ponownego `pair`.
- Ograniczenie proceduralne: pierwsza, omyłkowo uruchomiona sesja `bluetoothctl --agent NoInputNoOutput --monitor` nie przyjmowała stdin i pozostała zarejestrowana w tle. Do właściwej próby uruchomiono drugą sesję tego samego typu agenta; obie współistniały. Po próbie oba procesy zakończono. Nie można na podstawie tego przebiegu izolować wpływu liczby agentów. Rejestracja agenta przejściowo ustawiła `Pairable=yes`; po próbie przywrócono początkowe `Pairable=no`.

## Zredagowana sekwencja HCI

1. BR/EDR `Create Connection`: success. `Authentication Requested`, następnie wymiana IO Capability i potwierdzenie SSP.
2. `Simple Pairing Complete`: **Success (0x00)**. `Link Key Notification` oraz `MGMT New Link Key` wystąpiły; samych bajtów klucza nie kopiowano do raportu.
3. `Auth Complete`: **Success (0x00)**; `Encryption Change`: **Success (0x00)**. `bluetoothctl` zgłosił `Bonded: yes`, `ServicesResolved: yes`, `Paired: yes`, `Pairing successful`.
4. L2CAP/SDP odczytał rekordy usług, w tym AVDTP i AVCTP. Nie wykryto zestawienia kanału AVDTP, negocjacji kodeka SBC ani PCM A2DP. SDP/L2CAP zakończył się przed disconnect.
5. Około **4,204 s po `Auth Complete`** Raspberry wysłał `HCI Command: Disconnect` (event #120). Kod `0x13` w tej **komendzie** jest przyczyną przekazaną peer-owi, a nie dowodem, że peer rozłączył połączenie. `Disconnect Complete` (event #122) podał **Connection Terminated By Local Host (0x16)**; MGMT także wskazał local host. Przyczyna decyzji warstwy hosta/BlueZ o zakończeniu ACL nie jest zapisana w dostępnych dziennikach. Możliwa jest normalna obsługa `Pair` bez późniejszego `Connect`, ale nie jest to tutaj potwierdzona przyczyna implementacyjna.

## Dzienniki i readback

- `bluetooth.service` nie zapisał zdarzenia authentication lub powodu disconnect w badanym oknie. `bluealsa.service` zapisał pojawienie się obiektu celu podczas scanu, bez błędu profilu; brak aktywnego PCM w końcowym `bluealsa-cli list-pcms`. Te dzienniki nie zastępują śladu HCI.
- Po `scan off` i zamknięciu agenta: `Paired=yes`, `Bonded=yes`, `Trusted=no`, `Connected=no`, `Pairable=no`, `Discovering=no`. Bond nadal był widoczny w dodatkowym readbacku około 09:38:23. Nie ustawiano `trusted`, nie usuwano bondów ani NVS.
- L0 po próbie: READY, `bootId=ca5d5657`, `app0 VALID`, WWW READY, VoxOneBT online i `btConnected=false`; RADIO/PLAY, stacja, volume, MUTE i balance bez zmian. USB diagnostycznego VoxOneBT nie otwierano. Stan zapisu bondu po stronie NVS VoxOneBT nie jest bezpośrednio eksportowany przez API L0.

## Ochrona dowodów

Użytkownikowy proces `btmon` zatrzymano sygnałem INT po zakończeniu jednej próby. Oryginał pozostaje jako `/tmp/voxonebt-pairing.btsnoop` (root, `0600`). Kopia prywatna: `/home/piotrek/voxone-hil/logs/l0/VOXONEBT-PAIRING-HCI-CAPTURE-1-20261102T0933/btmon.btsnoop` (katalog `0700`, plik `0600`). Oba pliki: **12618 B**, SHA-256 `0bca31c6cb4135d976722cea031d58e8529a6c6534938c9e66e6536220a0abaf`. Surowy ślad może zawierać materiał klucza: nie publikować go ani nieselektywnego zrzutu `btmon -r`.

## Wniosek przyczynowy

Ta próba **wyklucza niepowodzenie SSP/authentication jako przyczynę bieżącego rozłączenia**: oba zakończyły się sukcesem, bond pozostał. Rozłączenie ACL zainicjował lokalny host Raspberry, po wykryciu usług i przed A2DP. Historyczne `Paired=yes → no` z poprzedniej próby nie zostało odtworzone; jej dokładna przyczyna pozostaje UNVERIFIED (różniły się m.in. trwałość sesji scanu i jawny agent, a obecny przebieg miał dwa zarejestrowane procesy agenta). Nie ma podstaw do przypisania błędu firmware VoxOneBT ani do kasowania bondów. Następny osobno zatwierdzony krok to jedno połączenie z już sparowanym peer-em i odrębny bounded A2DP smoke; bez odsłuchu analogowe audio pozostaje UNVERIFIED.

TODO: zaktualizowano wyłącznie bieżący stan Pair/Bond; BT lifecycle, A2DP LIVE i CHAOS pozostają otwarte.
