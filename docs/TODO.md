# VoxOne — aktualne zadania

> Jedyna kanoniczna lista bieżących prac. Po zakończeniu i odpowiedniej weryfikacji usuwamy zadanie; nie prowadzimy sekcji DONE.

## 1. LCD SALON — poprawki BT

- Zmienić „Oczekuję na połączenie” na „Oczekuję na połączenie...”; przy rozłączonym BT nie pokazywać STOP.
- Ukrywać tekst „Not Provided”; stacja ma pokazywać nazwę urządzenia, a artysta i utwór pozostawać puste bez prawdziwych metadata.

## 2. LCD SALON — dolna część PLAYER

- Zastąpić „VOL” ikoną głośnika; pokazać „WiFi” bliżej słupków RSSI.
- Wyśrodkować bitrate/audio info między VU a zegarem, ikonę BT nad nim i Volume względem VU.
- Umieścić PLAY/PAUZA/STOP oraz ROCK/POP/USER/LOUDNESS w cienkich ramkach; zachować symetrię i odstępy.

## 3. BT-AUDIO-INFO-1

- Przy aktywnym BT pokazywać sample rate 44.1/48 kHz zamiast starego bitrate RADIO.
- Później rozważyć codec i rzeczywisty bitrate A2DP.

## 4. Ikona BT

- Na wszystkich ekranach LCD pokazywać ikonę BT, gdy telefon jest fizycznie połączony, niezależnie od aktywnego źródła.

## 5. Scroll tekstu

- Przeanalizować scroll stacji, artysty i utworu na podstawie `reference/yoPilot/`; katalog `reference/` pozostawić tylko do odczytu.

## 6. LCD AKTUALIZACJA

- Pokazać czarne tło oraz wycentrowany pionowo i poziomo czerwony napis „AKTUALIZACJA” czcionką jak nazwa stacji, bez elementów PLAYER.
- Zweryfikować osobno ekran aktualizacji DESK i nie naruszyć stabilnego ScrollWidget/HOLD.

## 7. Branding

- Przygotować logo VoxOne do ekranu startowego LCD i WWW, favicon, a później do README/GitHub.
- Trzymać źródłowe SVG i warianty PNG w jednym miejscu, np. `assets/branding/`; nie obciążać LCD dużą grafiką.

## 8. PLAY_MEDIA / TTS

- RADIO PLAY → TTS → RADIO PLAY i RADIO STOP → TTS → RADIO STOP działają; poprawić BT PLAY → TTS → BT PLAY oraz BT STOP → TTS → BT STOP.
- Uporządkować ownership I2S0, przywracanie sample rate, źródła i playback oraz cleanup po błędzie, przerwaniu i timeout. Gdy BT zniknie podczas TTS, zakończyć w RADIO STOP bez autoplay.
- Podczas TTS pokazać na LCD: stacja „KOMUNIKAT TTS”, artysta „Powiadomienie głosowe”, utwór pusty.
- Później rozważyć HTTPS, redirecty i opcjonalną Volume TTS.

## 9. Zachowanie startowe / zmiana źródła

- Docelowo start urządzenia i każda zmiana źródła mają pozostawiać odtwarzanie w STOP: RADIO → BT → STOP oraz BT → RADIO → STOP.
- Sprawdzić automatyczny wybór BT po nowym połączeniu telefonu, fallbacki niedostępnego źródła oraz cold boot.
- Oddzielić tę semantykę od istniejącego Startup Volume. Konfigurację startowej stacji/źródła wprowadzać tylko w zgodzie z powyższą zasadą.

## 10. WWW — aktywne źródło

- Dane PLAYER pobierać z Source Managera. RADIO: stacja, artysta, utwór, codec, bitrate. BT: nazwa urządzenia, artysta, utwór, playback, sample rate.
- Przy aktywnym BT nie pokazywać poprzednich danych RADIO; dodać sterowanie/status BT w WWW i HA.

## 11. WWW — STATUS jako ekran startowy

- Po otwarciu WWW i po refresh/F5 otwierać STATUS, bez przywracania poprzedniej zakładki jako startowej.

## 12. WWW — stopka

- Na podstawie `reference/yoPilot/` zaprojektować własną, schludną stopkę z nazwą projektu, wersją i informacjami systemowymi; nie kopiować 1:1 i nie zmieniać `reference/`.

## 13. WWW — porządki

- STATUS: sygnał i źródło zgodne z aktywnym RADIO/BT.
- USTAWIENIA → Sieć: uporządkować profile Wi-Fi, enabled, SSID, hasło, clear i priority.
- USTAWIENIA → Sen: screensaver, wygaszanie i sleep timer.
- SYSTEM → Informacje systemowe: firmware, profil, IP, RSSI, uptime, heap, MAC, capabilities i dane VoxOneBT.
- AKTUALIZACJA → Urządzenie: online/offline, firmware, protocol, BT name i capabilities VoxOneBT; dodać przyszłą aktualizację VoxOneBT.
- Zmienić nazwy „Firmware” na „VoxOne Firmware” i „WWW/system plików” na „VoxOne system plików”.
- Fizycznie sprawdzić na DESK ukrycie suwaka jasności LCD oraz restart WWW i powrót Wi-Fi bez utraty stacji/config.

## 14. WWW — auto reload

- Po ZAPISZ, restarcie, aktualizacji firmware i filesystem pokazywać „Restartowanie”, odpytywać urządzenie co około 1 s, a po powrocie otwierać STATUS.
- Dodać rozsądny timeout i komunikat, gdy urządzenie nie wróci; przetestować błędy backendu i nieudany backup.

## 15. AAC / AAC+

- Obsłużyć AAC, AAC+ i `audio/aacp` z poprawną detekcją kodeka; nie przekazywać AAC do dekodera MP3.
- Sprawdzić rzeczywiste stacje AAC+, w tym RMF DLA DZIECI.

## 16. M3U / PLS

- Obsłużyć M3U, PLS, redirect i końcowy URL streamu; ograniczyć rekurencję i chronić przed pętlą.
- Później rozważyć ASX/M3U8.

## 17. Audio / kompatybilność

- Tylko jeśli nadal występuje `Truncated MP3 frame`: przeanalizować ICY stripping, buforowanie, MP3 sync/frame length i Helix przed zmianą dekodowania.
- Przed stable przetestować SHOUTcast/Icecast, `ICY 200`, MIME, HTTP/HTTPS, reconnect, dead stream oraz stream z metadata i bez.
- Zweryfikować semantykę `status.on = config.store.dspon` w `ha_yoradio`.

## 18. Playlisty

- Dopracować import/export yoRadio: walidacja, preview, raport błędnych rekordów i ewentualny import URL.
- Sprawdzić duże listy 50/100/250 stacji, power-loss recovery, brak miejsca SPIFFS, `current/lastStation` i zachowanie po usunięciu bieżącej stacji.
- Zweryfikować eksport/import między DESK i SALON, ID, kolejność, OVOL, A↔T, odświeżenie WWW oraz migrację/restore przy Web Update.
- Fizycznie przetestować Radio Directory na SALON i DESK, w tym dodanie, odtwarzanie, restart, pamięć i brak zakłóceń audio.
- Sprawdzić prezentację A↔T na LCD/WWW/Nextion/MQTT, zachowanie po reorder i usunięciu stacji, pstryknięcie audio przy mutacji oraz wyłączenie legacy `/upload`.

## 19. DLNA

- Docelowy cykl źródeł: RADIO | BT | DLNA według capabilities i dostępności.
- Sterowanie: double click na PLAYER zmienia źródło; long click na DLNA otwiera katalogi; obrót wybiera pozycję; click wchodzi/odtwarza; double click wraca; triple click zmienia tryb odtwarzania.
- Tryby: jeden utwór w pętli, folder kolejno w pętli, folder losowo w pętli.
- Ustalić IP serwera, skanowanie, ContentDirectory, browsing, pagination, kolejny utwór, repeat/random oraz integrację Source Manager, LCD i WWW.

## 20. Późniejsze

- Sleep/screensaver, MUTE/AMP_POWER, SALON_DSP/TDA7719, AUX, SPDIF i Alarm.
- SALON_DSP: EQ, Loudness, Balance, Fader, Subwoofer, 2.0/2.1, presety, storage oraz WWW/LCD. DS3231 i TDA7719 mają współdzielić jedną magistralę I²C GPIO7/8.
- DIN: fizycznie sprawdzić PCM5102A GPIO1/2/3, VoxOneBT, WWW, MQTT/HA i NoDisplay; ustalić docelową definicję ESP32-S3 Zero.
- Dodatkowe wyświetlacze i opcjonalny czujnik światła rozwijać po podstawowych funkcjach.

## 21. Stabilność / release

- Wykonać audit DESK, testy regresyjne DESK/SALON, długie testy audio i diagnostykę problemów występujących na sprzęcie, w tym logów czasu i wcześniejszego `ipc1` panic.
- Fizycznie sprawdzić MQTT/HA SALON, Web Update firmware SALON oraz firmware/SPIFFS DESK, w tym postęp, błędy, backup i powrót WWW.
- Sprawdzić recovery AP przy błędnych danych Wi-Fi, Serial CLI, ekran AP na telefonie i jego kolory oraz `/update.html` i `/emergency` przy niedostępnym SPIFFS.
- Dopracować backup/restore całej konfiguracji z walidacją schematu oraz bezpieczeństwo WWW: uwierzytelnianie, CSRF, mutujące REST/WS i ekspozycję danych.
- Sprawdzić wielokrotne BT connect/disconnect, reconnect i wcześniejszy HCI allocation assert.
- Uzupełnić fizyczną kontrolę BT VU przy STOP/disconnect, niezależności L/R oraz synchronizacji VU ON/OFF między klientami WWW i po restarcie.
- Dokończyć dokumentację profili, API, MQTT/HA, Source Managera, VoxOneBT i update/recovery; cleanup yoRadio wykonywać dopiero po potwierdzeniu stabilności.
- Po potwierdzeniu wszystkich wymagań przygotować finalny release; checkpointy techniczne nie zmieniają `VOXONE_VERSION`.

## Kolejność najbliższych prac

1. BT-AUDIO-INFO-1.
2. Drobne poprawki LCD BT.
3. Dolna część LCD PLAYER.
4. Ikona BT.
5. Scroll na podstawie yoPILOT.
6. TTS BT i LCD TTS.
7. WWW zależne od aktywnego źródła.
8. Porządki WWW.
9. Stopka i logo WWW.
10. Start i zmiana źródła → STOP.
11. DLNA.
12. AAC, M3U i dalsza zgodność.