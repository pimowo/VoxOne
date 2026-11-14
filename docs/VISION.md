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
Poziom LINE OUT jest zapamiętywany oddzielnie od zwykłej głośności urządzenia,
a jego włączenie i konfiguracja są dostępne zarówno w WWW, jak i w menu na
urządzeniu.

Podczas odtwarzania przez Bluetooth tryb LINE OUT korzysta z AVRCP Absolute
Volume do automatycznego ustawienia uzgodnionego poziomu źródła. Zapobiega to
niezamierzonemu ściszeniu sygnału po stronie telefonu przy zachowaniu stałego
poziomu wyjścia VoxOne.

VoxOne powinien również oferować wyjście przeznaczone do podłączenia
zewnętrznej końcówki mocy klasy D. Pozwoli to budować zarówno samodzielne radio
ze wzmacniaczem, jak i sieciowy transport audio pracujący w trybie LINE OUT.

### DSPmini

DSPmini jest posiadanym modułem sprzętowym opartym na ADAU1701. Docelowo
odpowiada za:

- korekcję parametryczną i podstawowy EQ,
- crossover dla zestawów wielodrożnych,
- obsługę wyjścia subwoofera,
- Loudness,
- dalsze funkcje DSP, takie jak AutoEQ.

Konfiguracja DSP powinna być dostępna z tego samego interfejsu co pozostałe
ustawienia audio VoxOne.

### Rozszerzenia sprzętowe

- **PCM1808 / AUX:** posiadany moduł wejścia analogowego pozwalający używać
  VoxOne z gramofonem wyposażonym w przedwzmacniacz, odtwarzaczem CD lub innym
  źródłem liniowym.
- **W5500 / Ethernet:** dedykowany kontroler sieci przewodowej na magistrali
  SPI3 profilu L0, szczególnie przydatny przy odtwarzaniu bezstratnym i pracy
  multiroom.
- **Local Files:** odtwarzanie plików z karty microSD podłączonej przez SPI.
  Magistrala jest współdzielona z wyświetlaczem w kontrolowany sposób, a karta
  ma osobny pin CS, aby operacje plikowe nie zakłócały renderowania.

## Enkoder

Sposób działania enkodera jest wybierany w konfiguracji urządzenia:

| Tryb | Obrót enkodera |
| --- | --- |
| Normalny | Reguluje głośność; obrót zgodnie z ruchem wskazówek zegara ją zwiększa. |
| Odwrócony | Zamiast głośności przełącza stacje w odwróconym modelu sterowania. |
| Tylko stacje | Przełącza wyłącznie stacje lub pozycje źródła; regulacja głośności enkoderem jest wyłączona. |

W trybie LINE OUT poziom wyjścia pozostaje stały we wszystkich trzech trybach,
a regulacja głośności jest pomijana. Interfejs pokazuje `LINE`, zaś obrót może
służyć do nawigacji po stacjach, źródłach lub menu zgodnie z kontekstem.

### Gesty i kontekst

- **Menu:** obrót nawiguje po pozycjach, klik wybiera, dwuklik wraca o poziom,
  a długi klik zamyka menu.
- **DLNA:** długi klik wchodzi do przeglądarki DLNA, klik otwiera folder albo
  uruchamia utwór, a dwuklik wraca do katalogu nadrzędnego.
- **Player:** dwuklik przełącza kolejne dostępne źródła.

## Docelowe źródła

VoxOne ma prezentować wszystkie źródła przez wspólny model wyboru, metadanych,
stanu odtwarzania i powrotu do poprzedniego źródła.

| Źródło | Rola |
| --- | --- |
| WEB Radio | Podstawowe radio internetowe i zapisane stacje. |
| Bluetooth | Odtwarzanie bezpośrednio z telefonu lub komputera. |
| Home Assistant | `SOURCE_HA`: komunikaty TTS i media z metadanymi title/artist oraz sterowanie z automatyzacji domu. |
| DLNA | Przeglądanie i odtwarzanie biblioteki z NAS lub serwera `minidlna`. |
| AUX | Sygnał analogowy doprowadzony przez PCM1808. |
| Local Files | Biblioteka plików na lokalnej karcie microSD. |

Klient DLNA ma docelowo obsługiwać do trzech serwerów równocześnie. Serwery są
wykrywane automatycznie przez SSDP, a opcjonalna konfiguracja ręcznego adresu IP
pozwala korzystać z serwera, którego multicast nie jest widoczny. WWW i menu
urządzenia pokazują wybór serwera przed przeglądaniem jego biblioteki.

## Interfejs urządzenia

Menu używa kontekstowej górnej belki. Widok Player pokazuje nazwę stacji lub
metadane źródła, Menu Główne zastępuje je napisem `MENU`, a podmenu nazwą
sekcji, na przykład `AUDIO`. Po zamknięciu menu belka wraca do informacji o
bieżącym źródle.

Tryb LINE OUT ma własny stan prezentacji z napisem `LINE` zamiast wartości
głośności. Docelowy model ekranów obejmuje również widoki `HOME`, `STATIONS`,
`SETTINGS`, `UPDATE` i `STANDBY`, wspólne znaczeniowo dla TFT i OLED, lecz
dostosowane układem do rozmiaru wyświetlacza.

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

## Przyszłe warianty i rozszerzenia

- **VoxOneBT:** odbiór A2DP i nadawanie A2DP działają naprzemiennie; w danym
  momencie moduł pracuje tylko w trybie RX albo TX.
- **LCD ST7920:** opcjonalny ekran znakowo-graficzny dla wariantów, w których
  taki interfejs jest lepiej dopasowany niż TFT lub OLED.
- **Rewizje sprzętowe:** L1, M1, S1 i DIN1 rozwijają obecne profile na
  podstawie doświadczeń z prototypów i testów HIL.
