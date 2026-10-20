# FIRST BOOT — selektywny USB restore

Ta procedura jest ostatecznym fallbackiem dla komunikatu:

`FIRST BOOT transaction recovery failed; boot halted`

Nie zastępuje automatycznego rollbacku. Typowe przerwania zapisu
`wifi.csv.tmp` / `wifi.csv.old` / `wifi.csv` są naprawiane automatycznie przed
uruchomieniem sieci. USB restore jest wymagany wyłącznie wtedy, gdy dziennik
jest uszkodzony, ma nieznaną wersję albo trwały stan nie odpowiada ani
baseline'owi, ani kandydatowi zapisanym w poprawnym dzienniku.

## Zasady bezpieczeństwa

- Nie kasuj samego klucza `voxfirstboot/transaction`. Bez poprawnego dziennika
  nie można udowodnić, czy hardware, Wi-Fi i SECURITY tworzą jedną generację.
- Nie wykonuj `erase_flash`, `nvs_erase_all` ani formatowania SPIFFS.
- Nie kopiuj NVS między urządzeniami. Backup musi pochodzić z tego samego ESP i
  poprzedzać nieudaną transakcję.
- Nie usuwaj ani nie zastępuj `voxsecurity/settings` osobno. W szczególności
  nie wolno obniżyć `PROTECTED` do `OPEN`.
- Przed zapisem odczytaj cały flash dwa razy, porównaj SHA-256 i zachowaj oba
  obrazy poza repozytorium.

## Procedura

1. Odłącz urządzenie od sieci i pozostaw je w kontrolowanym trybie serwisowym.
2. Ustal profil MCU, rozmiar flash oraz offsety `nvs` i `spiffs` z odczytanej
   tablicy partycji. Nie przepisuj offsetów z innego profilu.
3. Wykonaj dwa pełne backupy bez automatycznego resetu lub ładowania stubu.
   Dla M0 użyj read-only workflow `scripts/m0_flash_backup.py` i jego `inspect`.
4. Zweryfikuj tablicę partycji, NVS, SPIFFS oraz obrazy aplikacji. Jeżeli
   backupy nie są identyczne albo inspekcja nie przechodzi, przerwij.
5. Z zatwierdzonego, wcześniejszego backupu tego samego urządzenia wyodrębnij
   całe partycje `nvs` i `spiffs` zgodnie z jego tablicą partycji.
6. Zapisz wyłącznie te dwie partycje pod odczytane offsety. Nie zapisuj
   bootloadera, `otadata`, tablicy partycji ani slotów aplikacji.
7. Odczytaj zapisane zakresy ponownie i porównaj SHA-256 z materiałem źródłowym.
8. Dopiero po zgodnej weryfikacji wykonaj jeden kontrolowany restart i sprawdź
   UART, stan SECURITY, decyzję Setup AP oraz hardware/Wi-Fi.

Jeżeli nie istnieje zatwierdzony backup sprzed transakcji, nie ma bezpiecznego
automatycznego sposobu odtworzenia nieznanej konfiguracji. Należy zatrzymać
urządzenie i uzyskać osobną zgodę na factory recovery, które może utracić dane.

Ta procedura nie ma jeszcze fizycznego HIL PASS. W bieżącym etapie wolno
zweryfikować wyłącznie software recovery i diagnostykę; nie wolno otwierać
USB/UART, flashować ani resetować urządzeń.
