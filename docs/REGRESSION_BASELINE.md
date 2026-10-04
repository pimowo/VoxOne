# PROJECT-CLEANUP-1A — baza regresji

Punkt odniesienia: VoxOne commit 4436dce4d71ad1bb7d3d9d9b785c2825a33ee086, tag voxone-dsp-web-live-1. Baza została zmierzona w oddzielnym worktree VoxOne-cleanup na branchu project-cleanup-1. Stary yoVoxOne-salon pozostaje fizycznie sprawdzonym punktem odniesienia.

## Uruchomienie

W katalogu VoxOne-cleanup:

    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify.ps1

Skrypt uruchamia wszystkie znalezione testy *_native.cpp (28 w bazie), istniejący test Chrome Web Update, PlatformIO buildfs (wraz z generowaniem WWW), sprawdza sześć wygenerowanych gzip względem web-src i powtarzalność po kolejnych buildach, buduje desk/din/salon, uruchamia git diff --check i zwraca niezerowy kod przy błędzie. Nie wymaga USB ani nie wykonuje uploadu. salon_dsp jest celowo wykluczony, bo obecny profil ma niekompletną mapę pinów i jawny błąd kompilacji.

## Rozmiary referencyjne

| Artefakt | Rozmiar w bajtach | Uwagi |
|---|---:|---|
| desk firmware.bin | 1466096 | ESP32, build desk |
| din firmware.bin | 1402080 | ESP32-S3, build din |
| salon firmware.bin | 1519600 | ESP32-S3, build salon |
| salon spiffs.bin | 196608 | buildfs; obraz SPIFFS jest wspólnym zestawem WWW w tym etapie |

Rozmiary są sygnałem regresji, nie progiem PASS/FAIL. Jednobajtowa lub większa uzasadniona zmiana nie blokuje weryfikacji automatycznie. Logi i pliki testowe trafiają do ignorowanego katalogu .pio/verify/.

## Powtarzalność WWW i Windows

scripts/build_web_assets.py używa gzip z mtime=0 i pustą nazwą pliku. Skrypt weryfikacyjny porównuje rozpakowane gzip ze źródłami, w tym podstawienia hash w HTML, a następnie porównuje SHA-256 gzip po buildfs i po buildach firmware.

Na pierwszym checkout Windows z core.autocrlf=true zmieniał końce linii audytowanego assets/yoradio_glcdfont.c, przez co pre-script odrzucał build. Nowe .gitattributes utrzymuje dokładne bajty tego pliku i LF źródeł web-src. Dwa wcześniej śledzone gzip (advanced-audio.js.gz i voxone.html.gz) miały bajty wynikające z końcowych CRLF w starym working tree; po normalizacji źródeł zostały odtworzone deterministycznie. Nie zmieniono działania strony ani kodu JS/CSS/HTML.

Istniejący test test/voxone_update_headless.py wymagał załadowania w Chrome dodanych wcześniej advanced-audio.js i dsp-client.js. Zaktualizowano wyłącznie harness testu, bez zmiany WWW. Test ten jest osobną bramką obok 28 testów C++ native.

\n