# VOXONE-L0-TTS-ABC-CLOSURE-1

Data: 2026-11-01. Zakres: formalne zamknięcie regresji TTS A/B/C na fizycznym L0 raportującym firmware `0.8.0-dev`, build `bf1d147`, aktywną partycję `app0 VALID`. Nie wykonywano nowych testów ani kontaktu z L0 w tym kroku.

## Werdykt

**A/B/C: PHYSICAL HIL PASS w zakresach obserwowalnych kontraktów na kandydacie L0 `bf1d147`.** Każdy scenariusz miał osobny preflight, prośbę o pojedynczą publikację MQTT z HA, pojedyncze żądanie fixture, pasywny L0_LOG_RX i najwyżej jednego równoczesnego klienta WS. W A/B późniejszy postcheck użył kolejnego, niejednoczesnego połączenia WS. Nie powtarzano A/B/C w ramach zamknięcia ani zakończonego wcześniej D.

| Scenariusz | Fizyczny dowód i wynik |
|---|---|
| A — HTTP 503 przy RADIO STOP | Jeden GET, HTTP 503 bez body; oczekiwany błąd HTTP i STOP w UART, brak trwałego RADIO autoplay w WS i według użytkownika. **PASS**. |
| B — stalled body przy RADIO STOP | HTTP 200, wysłano 32812 z deklarowanych 176444 B, następnie 15 s bez body. Watchdog UART nastąpił około **10,06 s** po ostatnich bajtach; autonomiczny cleanup do RADIO STOP, brak autoplay. **PASS**. |
| C — stalled body przy RADIO PLAY | Ten sam rozmiar prefiksu i stall na osobnym URL. Watchdog UART nastąpił około **10,41 s** po ostatnich bajtach; RADIO samoczynnie wróciło do PLAY/playing po około **10,80 s**, na tej samej stacji. Tytuł aktywny bezpośrednio przed TTS wrócił, a użytkownik potwierdził prawidłowy odsłuch RADIO. **PASS**. |

We wszystkich trzech próbach utrzymały się `READY=true`, `app0 VALID`, WWW READY i stały `bootId=ca5d5657`; UART nie wykazał panic/assert/WDT ani nieoczekiwanego restartu. Manifesty loggerów A/B/C mają `status=PASS` **wyłącznie dla integralności przechwycenia**, odpowiednio 1459/1459, 1744/1744 i 2481/2481 B, bez przerwania. Wynik aplikacyjny wynika z łącznych dowodów HTTP, WS, UART i obserwacji użytkownika, nie z samego statusu loggera.

## Pochodzenie i integralność dowodów

- A: `/home/piotrek/voxone-hil/logs/l0/VOXONE-L0-TTS-ABC-PHYSICAL-HIL-1-A-m3YTTDLM/REPORT.md`; `sha256sum -c SHA256SUMS` zwrócił OK dla raportu, HTTP, WS, postcheck, UART, manifestu loggera i fixture.
- B: `/home/piotrek/voxone-hil/logs/l0/VOXONE-L0-TTS-ABC-PHYSICAL-HIL-1-B-WHGJdIK0/REPORT.md`; `sha256sum -c EVIDENCE.sha256` zwrócił OK dla HTTP, WS, postcheck, UART i manifestu loggera.
- C i raport zbiorczy: `/home/piotrek/voxone-hil/logs/l0/VOXONE-L0-TTS-ABC-PHYSICAL-HIL-1-C-jXNKWV2J/REPORT.md` oraz `ABC-REPORT.md`; `sha256sum -c EVIDENCE.sha256` zwrócił OK dla obu raportów, HTTP, WS, UART, manifestu loggera oraz manifestu fixture.
- Fixture: `/home/piotrek/voxone-hil/tts-abc-release-fixtures-1/MANIFEST.json`; `sha256sum -c SHA256SUMS` zwrócił OK dla skryptów, WAV i wyników offline. SHA256 WAV: `781f1a9def10816a70b07c3de77198e92d9a23b547519908b7da78cd0906980c`.

Oryginalne fixture i manifesty z 2026-10-29 nie zostały odzyskane. Nowe materiały są deterministycznym zastępstwem z jawnym, innym kontraktem; historyczny PASS na `fbbdfbc` pozostaje osobnym dowodem i nie jest utożsamiany z próbami na `bf1d147`.

## Granice i stan bramek

Standardowy firmware nie udostępnia wewnętrznego licznika `cleanup` ani bezpośredniego odczytu `temporaryBusy`; liczba wewnętrznych wywołań cleanup dla A/B/C pozostaje **UNVERIFIED**. Obserwowalne zakończenie temporary audio i restore są potwierdzone przez sekwencje HTTP/UART/WS. Żywe metadane RADIO mogą zmieniać się niezależnie od TTS, dlatego porównano stan bezpośrednio przed każdą próbą i po restore. Build ID nie jest odczytem SHA całych obrazów MAIN/SPIFFS z flash.

`RC1-TTS-RESTORE` pozostaje **OPEN/PARTIAL**: pełny HIL S0+BT oraz pozostałe wymagania profili i pomiarów nie są zamknięte. To zamknięcie nie nadaje L0 statusu RC READY ani nie zmienia innych bramek Stable. W `docs/TODO.md` zastąpiono nieaktualne PENDING/BLOCKED dla regresji L0 A/B/C potwierdzonym PASS w tym konkretnym zakresie; pozostawiono historię nowych fixture, odrębny historyczny PASS, ograniczenie licznika cleanup i wszystkie pozostałe otwarte zadania.

Nie zmieniono firmware ani konfiguracji. Bez OTA, flashowania, restartów, commitów i push.
