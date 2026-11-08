# VoxOne — Wizja produktu

VoxOne ma być modułowym centrum audio łączącym radio internetowe, źródła
lokalne i automatykę domową w jednym spójnym urządzeniu. Ten dokument opisuje
kierunek produktu i docelowe doświadczenie użytkownika. Kanoniczna lista
otwartych prac pozostaje w `docs/TODO.md`.

## Sprzęt i tor audio

### Tryb LINE OUT

Tryb LINE OUT udostępnia sygnał o stałym, skonfigurowanym poziomie dla
zewnętrznego wzmacniacza lub aktywnych kolumn. Regulacja głośności VoxOne jest
wtedy zablokowana, aby przypadkowy obrót enkodera nie zmienił poziomu sygnału.
Wyświetlacz pokazuje czytelny napis `LINE` w miejscu wartości głośności.

VoxOne powinien również oferować wyjście przeznaczone do podłączenia końcówki
mocy. Pozwoli to budować zarówno samodzielne radio ze wzmacniaczem, jak i
sieciowy transport audio pracujący w trybie LINE OUT.

### DSPmini

DSPmini jest zewnętrznym modułem opartym na ADAU1701. Docelowo odpowiada za:

- korekcję parametryczną i podstawowy EQ,
- crossover dla zestawów wielodrożnych,
- obsługę wyjścia subwoofera,
- dalsze funkcje DSP, takie jak Loudness i AutoEQ.

Konfiguracja DSP powinna być dostępna z tego samego interfejsu co pozostałe
ustawienia audio VoxOne.

### Rozszerzenia sprzętowe

- **PCM1808 / AUX:** wejście analogowe pozwalające używać VoxOne z
  gramofonem wyposażonym w przedwzmacniacz, odtwarzaczem CD lub innym źródłem
  liniowym.
- **W5500 / Ethernet:** stabilne połączenie przewodowe jako alternatywa dla
  Wi-Fi, szczególnie przy odtwarzaniu bezstratnym i pracy multiroom.
- **Local Files:** odtwarzanie plików z karty microSD podłączonej przez SPI.
  W wariantach z wyświetlaczem LCD magistrala SPI powinna być współdzielona w
  kontrolowany sposób, z osobnymi liniami CS i bez zakłócania renderowania.

## Enkoder

Sposób działania enkodera jest wybierany w konfiguracji urządzenia:

| Tryb | Obrót enkodera |
| --- | --- |
| Normalny | Standardowy kierunek regulacji: zgodnie z ruchem wskazówek zegara zwiększa wartość. |
| Odwrócony | Kierunek regulacji jest odwrócony względem trybu normalnego. |
| Tylko stacje | Obrót przełącza stacje lub pozycje bieżącego źródła zamiast regulować głośność. |

W trybie LINE OUT poziom wyjścia pozostaje stały we wszystkich trzech trybach.
Interfejs pokazuje `LINE`, a obrót enkodera może służyć do nawigacji po
stacjach, źródłach lub menu zgodnie z wybranym kontekstem. Tryby Normalny i
Odwrócony nadal określają kierunek tej nawigacji.

## Docelowe źródła

VoxOne ma prezentować wszystkie źródła przez wspólny model wyboru, metadanych,
stanu odtwarzania i powrotu do poprzedniego źródła.

| Źródło | Rola |
| --- | --- |
| WEB Radio | Podstawowe radio internetowe i zapisane stacje. |
| Bluetooth | Odtwarzanie bezpośrednio z telefonu lub komputera. |
| Home Assistant | Komunikaty TTS, media i sterowanie z automatyzacji domu. |
| DLNA | Przeglądanie i odtwarzanie biblioteki z NAS lub serwera `minidlna`. |
| AUX | Sygnał analogowy doprowadzony przez PCM1808. |
| Local Files | Biblioteka plików na lokalnej karcie microSD. |

Spotify pozostaje możliwym rozszerzeniem w przyszłości, zależnym od dostępnego
modelu integracji i ograniczeń platformy.

## Multiroom

Docelowa synchronizacja wielu urządzeń VoxOne opiera się na Snapcast. Serwer
Snapcast może działać na OMV i rozsyłać wspólny strumień do odbiorników w
różnych pomieszczeniach. Integracja powinna wykorzystywać synchronizację czasu
i buforowanie Snapcast, aby urządzenia odtwarzały materiał bez słyszalnych
przesunięć.

## VoxOnePilot

VoxOnePilot jest osobnym pilotem zbudowanym na ESP32-C3. Łączy się z radiem
przez WebSocket, pokazuje bieżący stan i wysyła polecenia bezpośrednio do
VoxOne. Odbiornik IR pozwala mu również przyjmować komendy z klasycznych
pilotów i przekazywać je do urządzenia przez ten sam protokół sterowania.
