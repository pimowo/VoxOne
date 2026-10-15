# VoxOne — BRANDING-INTEGRATION-PLAN (do wykonania na DEV osobno)

## 0. Ochrona bieżącego stanu
Najpierw dokończ i zabezpiecz niezależny etap WWW-UI-POLISH-2 (7 plików), jeśli użytkownik go zaakceptuje. Nie łącz CSS z brandingiem w jednym commicie. Zachowaj historyczny hunk docs/TODO.md OTA-SAFE-ROLLBACK-1.

## 1. Zasoby
Skopiuj zawartość tego pakietu do wybranego katalogu repo, np. `assets/branding/`. Zachowaj pliki master SVG, PNG i licencję/atrybucję zgodnie z przyszłym audytem. Nie traktuj plików 1-bit/RGB565 jako gotowych do bezpośredniego wysłania na kontroler bez sprawdzenia formatu.

## 2. Wdrożenia w oddzielnych checkpointach
- BRANDING-ASSETS-1 — źródłowe wersje wektorowe, transparentne, mono, favicon, instrukcja, testy spójności i rozmiarów; bez zmian runtime.
- WWW-BRANDING-1 — nagłówek na ciemnym WWW, logowanie/recovery, favicon. Uwzględnij istniejący tymczasowy voxone-logo.svg, cache, gzip, SPIFFS, headless, ochronę SECURITY i niewystawianie starego Vn. Nie wprowadzaj do SPIFFS pełnego zestawu wysokiej rozdzielczości; zoptymalizuj wariant webowy.
- DISPLAY-BOOT-BRANDING-1 — wykorzystanie uproszczonych wariantów na aktywnych wyświetlaczach; 1-bit OLED i odpowiednio skompresowane TFT, bez zwiększenia czasu blokowania audio/boot. Aktualizacja/rollback i recovery muszą działać bez regresji. Domyślna faza bez operacji na urządzeniach.
- INSTALLER-DOCS-BRANDING-1 — instalator, dokumentacja, przyszłe PCB i nadruki (po sprawdzeniu geometrii).

## 3. HIL
Przed wgraniem na W0/M0/L0/S0/DIN0 wymagane odrębne zgody i testy. HIL P0 WebSocket, Buttons3, ST7789 i audio W0 pozostają PENDING.

Nie resetuj, nie flashuj ani nie aktualizuj SPIFFS na M0 bez osobnego zatwierdzenia. Pierwszy etap dla Codexa powinien być READ-ONLY preflight zasobów i raport różnic, nie automatyczna integracja wszystkiego.
