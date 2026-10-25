# VOXONE-RC1-M0-HIL-CLOSURE-1

Data: 2026-10-25 (Europe/Warsaw)

Wynik SECURITY 8H: **BLOCKED przed loginem**.

## SECURITY 8H

Semantyka firmware jest jednoznaczna: sesja ma limit bezczynności 8 h i
niezależny limit bezwzględny 24 h. Walidacja chronionego HTTP oraz przyjęta
aktywność powiązanego WebSocketu odnawiają czas bezczynności; samo
publiczne `/api/ready` i `/api/security/state` nie używa cookie testowej sesji.
Granica timeoutu jest fail-closed i obsługuje zawinięcie `millis()`.

Jednorazowy publiczny preflight potwierdził na fizycznym M0:

- `ready=true`, `bootId=5aa49bfc`;
- `app1/app1`, obraz `VALID`, `pending=false`, health `not_pending`;
- SECURITY `protected`, `protectionEnforced=true`, tryb `production`;
- klient preflightu pozostał anonimowy.

Na ASUS DEV nie było istniejącego prywatnego FIFO, zmiennej credential,
credential-store ani kontrolowanej sesji, których można było bezpiecznie użyć.
Zgodnie z regułą STOP nie zgadywano hasła i nie proszono o nie w rozmowie.
Nie utworzono sesji, nie zapisano hasła, cookie ani tokenu, i nie uruchomiono
8-godzinnego timera bez rzeczywistej sesji. Nie otwarto FT232R M0, nie wykonano
OTA, flashowania, resetu ani zmiany SECURITY, Wi-Fi lub konfiguracji.

## Kolejna bramka

Wybrano najwyżej położoną nadal otwartą bramkę: fizyczny L0 RADIO
endurance 4H. Nie uruchomiono drugiego pomiaru ani procesu UART. Istniejące
PID 3635259/3635260 pozostały bez zmian, a transient user service
`voxone-l0-radio-endurance-finalizer-20261025` (PID 3674862) jedynie czeka na
zakończenie monitora i wtedy uruchomi przygotowane `analyze_evidence.py` na
zapisanych plikach. W chwili checkpointu trwał test,
`bootId=0a4c2098` pozostawał stały, a dotychczasowy UART zawierał co najmniej
141 pełnych ramek endurance i zero `Truncated MP3 frame`, panic, assert, heap corruption
lub watchdog. Nie jest to jeszcze końcowy PASS i zadania RADIO nie usunięto.

## Release gate

Nie zamknięto nowej głównej bramki. Według `Definition of Stable` pozostaje
15 głównych warunków: pięć osobnych profili fizycznych oraz dziesię
pozostałych pozycji wymienionych w definicji, w tym końcowy PASS zamrożonego
release commita. Wynik częściowy nie zmniejsza tej liczby.
