# VoxOne — aktualne zadania

> Jedyna kanoniczna lista bieżących prac. Po zakończeniu i odpowiedniej weryfikacji usuwamy zadanie; nie prowadzimy sekcji DONE.

## 3. LCD audio info

- Później rozważyć codec i rzeczywisty bitrate A2DP.

## 4. Ikona BT

- Na pozostałych ekranach LCD dodać ikonę BT, gdy telefon jest fizycznie połączony, niezależnie od aktywnego źródła.

## 6. LCD AKTUALIZACJA

- Fizycznie sprawdzić cleanup Web Update po błędzie/przerwaniu oraz brak samoczynnego wznowienia audio.
- Zweryfikować osobno ekran aktualizacji DESK i nie naruszyć stabilnego ScrollWidget/HOLD.

## 7. Branding

- Przygotować logo VoxOne do ekranu startowego LCD i WWW, favicon, a później do README/GitHub.
- Trzymać źródłowe SVG i warianty PNG w jednym miejscu, np. `assets/branding/`; nie obciążać LCD dużą grafiką.

## 8. PLAY_MEDIA / TTS

- RADIO PLAY → TTS → RADIO PLAY i RADIO STOP → TTS → RADIO STOP działają; poprawić BT PLAY → TTS → BT PLAY oraz BT STOP → TTS → BT STOP.
- Uporządkować ownership I2S0, przywracanie sample rate, źródła i playback oraz cleanup po błędzie, przerwaniu i timeout. Gdy BT zniknie podczas TTS, zakończyć w RADIO STOP bez autoplay.
- Automatyzacja HA wysyła tylko `play_media(URL)`. VoxOne sam zatrzymuje lub pauzuje bieżące źródło, przejmuje I2S0, ustawia sample rate i głośność TTS, przywraca poprzednie źródło oraz stan odtwarzania i sprząta po błędzie lub timeout.
- TTS Volume: CURRENT albo FIXED 0–100; FIXED nie zmienia głównego `userVolume`. TTS jest tymczasowym audio override, nie zwykłym źródłem użytkownika. Docelowo ma działać dla RADIO, BT, DLNA, AUX i SPDIF.
- Podczas TTS pokazać na LCD: stacja „KOMUNIKAT TTS”, artysta „Powiadomienie głosowe”, utwór pusty. Sprawdzić też etykietę źródła TTS na PLAYER SALON.
- Później rozważyć HTTPS i redirecty.

## 9. Zachowanie startowe / zmiana źródła

- Docelowo start urządzenia i każda zmiana źródła mają pozostawiać odtwarzanie w STOP: RADIO → BT → STOP oraz BT → RADIO → STOP.
- Sprawdzić automatyczny wybór BT po nowym połączeniu telefonu, fallbacki niedostępnego źródła oraz cold boot.
- Oddzielić tę semantykę od istniejącego Startup Volume. Konfigurację startowej stacji/źródła wprowadzać tylko w zgodzie z powyższą zasadą.

## 10. WWW — aktywne źródło

- Dane PLAYER pobierać z Source Managera. RADIO: stacja, artysta, utwór, codec, bitrate. BT: nazwa urządzenia, artysta, utwór, playback, sample rate.
- Przy aktywnym BT nie pokazywać poprzednich danych RADIO; dodać sterowanie/status BT w WWW i HA.

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
- Przyszłe źródła: DLNA — VU z PCM, codec i bitrate; AUX — VU z PCM po ADC, sample rate i format PCM zamiast bitrate; SPDIF — VU z PCM, sample rate i format. Na wyjściu może być aktywne tylko jedno źródło audio naraz.
- Zweryfikować semantykę `status.on = config.store.dspon` w `ha_yoradio`.

## 18. Playlisty

- Dopracować import/export yoRadio: walidacja, preview, raport błędnych rekordów i ewentualny import URL.
- Sprawdzić duże listy 50/100/250 stacji, power-loss recovery, brak miejsca SPIFFS, `current/lastStation` i zachowanie po usunięciu bieżącej stacji.
- Zweryfikować eksport/import między DESK i SALON, ID, kolejność, OVOL, A↔T, odświeżenie WWW oraz migrację/restore przy Web Update.
- Fizycznie przetestować Radio Directory na SALON i DESK, w tym dodanie, odtwarzanie, restart, pamięć i brak zakłóceń audio.
- Sprawdzić prezentację A↔T na LCD/WWW/Nextion/MQTT, zachowanie po reorder i usunięciu stacji, pstryknięcie audio przy mutacji oraz wyłączenie legacy `/upload`.

## 19. Standard sterowania enkoderem i Source Control

- WEB PLAYER: obrót → Volume; klik → PLAY/STOP; dwuklik → następne źródło; trójklik → brak akcji; przytrzymanie → lista stacji.
- BT PLAYER: obrót → Volume; klik → PLAY/PAUSE; dwuklik → następne źródło; trójklik → brak akcji; przytrzymanie → sterowanie BT.
- DLNA PLAYER: obrót → Volume; klik → PLAY/PAUSE; dwuklik → następne źródło; trójklik → tryb ALL/RND/ONE; przytrzymanie → biblioteka DLNA.
- Cykl źródeł: WEB → BT → DLNA → WEB. Source Manager pomija źródła niedostępne według capabilities.
- DLNA ALL/FOLDER odtwarza cały folder kolejno w pętli; RND odtwarza każdy utwór folderu raz, po czym tasuje ponownie; ONE zapętla bieżący utwór.
- Przeglądarka DLNA: obrót wybiera pozycję; klik folderu wchodzi do niego; klik „ODTWÓRZ FOLDER” rozpoczyna od pierwszego utworu; klik utworu rozpoczyna od wybranego; dwuklik wraca poziom wyżej; przytrzymanie wraca do PLAYER; timeout około 15 s wraca do PLAYER; trójklik nie wykonuje akcji.
- Gesty klik, dwuklik i trójklik rozstrzygać po krótkim oknie czasowym, aby trójklik nie został wcześniej wykonany jako dwuklik.
- DLNA pozostaje niezaimplementowane. Do ustalenia i wykonania: IP serwera, skanowanie, ContentDirectory, browsing, pagination, kolejny utwór oraz integracja Source Manager, LCD i WWW.

## 20. Późniejsze

- LAN / W5500: dodać opcjonalny moduł na osobnej magistrali SPI, niezależnej od ST7796, oraz tryby AUTO, LAN i Wi-Fi. W AUTO preferować Ethernet przy aktywnym linku LAN, a przy braku linku przechodzić na Wi-Fi.
- LAN / W5500: uruchamiać DHCP, później rozważyć statyczny IP. W WWW pokazywać aktywny interfejs, link LAN, IP i podstawowy status Ethernet. Warstwa sieciowa RADIO/DLNA nie może zakładać na sztywno `WiFiClient` ani Wi-Fi; DLNA/SSDP ma działać przez LAN i Wi-Fi.
- LAN / W5500: w stałym obszarze systemowym LCD pokazywać dla Wi-Fi tylko słupki RSSI i obok małą ikonę BT; dla LAN zastąpić słupki małą ikoną RJ45/Ethernet. Bez napisów „WiFi” i „LAN” oraz bez przesuwania tego obszaru.
- LCD SKIN — YAMAHA AMBER: opcjonalna, przełączalna skórka z czarnym tłem i interfejsem w jednym bursztynowo-pomarańczowym kolorze, w stylu starszych amplitunerów Yamaha. Punkt startowy: #FF9A1F, RGB565 0xFCC3.
- Sleep/screensaver, sterowanie istniejącym MUTE z WWW/HA/MQTT, SALON_DSP/TDA7719, AUX, SPDIF i Alarm.
- AMP_POWER: sterowanie zasilaniem wzmacniacza z anti-pop — mute/fade przed wyłączeniem, opóźnienie po włączeniu i unmute/fade po stabilizacji.
- SALON_DSP: EQ, Loudness, Balance, Fader, Subwoofer, 2.0/2.1, presety, storage oraz WWW/LCD. DS3231 i TDA7719 mają współdzielić jedną magistralę I²C GPIO7/8.
- DIN: fizycznie sprawdzić PCM5102A GPIO1/2/3, VoxOneBT, WWW, MQTT/HA i NoDisplay; ustalić docelową definicję ESP32-S3 Zero.
- Dodatkowe wyświetlacze i opcjonalny czujnik światła rozwijać po podstawowych funkcjach.

## 21. Stabilność / release

- Ustalić i podłączyć GPIO XSMT PCM5102A na SALON, potem fizycznie sprawdzić LOW przy PAUZA/STOP, HIGH przy PLAY, ciszę podczas przejść i Web Update oraz czerwoną ramkę VOL bez zmiany działania MUTE.
- Fizycznie sprawdzić końcowy PLAYER SALON po usunięciu górnego separatora: dolną linię y=280, stałą geometrię slotów LOUD/MODE oraz brak kolizji z VU, audio-info, zegarem i datą.
- Wykonać audit DESK, testy regresyjne DESK/SALON, długie testy audio i diagnostykę problemów występujących na sprzęcie, w tym logów czasu i wcześniejszego `ipc1` panic.
- Fizycznie sprawdzić MQTT/HA SALON, Web Update firmware SALON oraz firmware/SPIFFS DESK, w tym postęp, błędy, backup i powrót WWW.
- Sprawdzić recovery AP przy błędnych danych Wi-Fi, Serial CLI, ekran AP na telefonie i jego kolory oraz `/update.html` i `/emergency` przy niedostępnym SPIFFS.
- Dopracować backup/restore całej konfiguracji z walidacją schematu oraz bezpieczeństwo WWW: uwierzytelnianie, CSRF, mutujące REST/WS i ekspozycję danych.
- Sprawdzić wielokrotne BT connect/disconnect, reconnect i wcześniejszy HCI allocation assert.
- Uzupełnić fizyczną kontrolę BT VU przy STOP/disconnect, niezależności L/R oraz synchronizacji VU ON/OFF między klientami WWW i po restarcie.
- Dokończyć dokumentację profili, API, MQTT/HA, Source Managera, VoxOneBT i update/recovery; cleanup yoRadio wykonywać dopiero po potwierdzeniu stabilności.
- Po potwierdzeniu wszystkich wymagań przygotować finalny release; checkpointy techniczne nie zmieniają `VOXONE_VERSION`.

## Kolejność najbliższych prac

1. PLAY_MEDIA / TTS BT oraz TTS Volume CURRENT/FIXED.
2. Start i zmiana źródła → STOP.
3. WWW zależne od aktywnego źródła.
4. AAC/AAC+.
5. M3U/PLS i redirecty.
6. Testy playlist i stacji.
7. Audit DESK/DIN i stabilność.
8. Stabilne RADIO+BT.
9. Później: DLNA, W5500/LAN, AUX/SPDIF i DSP.
