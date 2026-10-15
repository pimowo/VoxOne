# VoxOne — oficjalny zestaw logo (kandydat zaakceptowany wizualnie)

Logo powstało na podstawie wzoru zaakceptowanego przez użytkownika. Grafika ma przezroczyste tło i jest przeznaczona do WWW, dokumentacji, instalatora, ekranów startowych oraz przyszłych obudów i PCB.

## Katalogi

- `svg/` — wektorowe obrysy liter i symbolu (zrekonstruowane z zaakceptowanego obrazu). Warianty na jasne/ciemne tło i monochromatyczne.
- `png/` — PNG z przezroczystością oraz lżejsze wersje 1035 px; osobne symbole.
- `icons/` — sam symbol w rozmiarach 16–512 px, favicon.ico i favicon.svg.
- `display/` — przykładowe plansze boot dla ST7789 284x76, ST7796 480x320 i OLED 128x64, surowe dane BMP/XBM, 1-bit / RGB565; **jeszcze nie zintegrowane z firmware**.
- `pcb/` — czarny jednokolorowy znak do sitodruku. W docelowym CAD trzeba zweryfikować rozmiar i grubość linii.
- `docs/VoxOne_brandbook.pdf` — skrócona instrukcja wizualna.
- `VoxOne_branding_preview.png` — podgląd wariantów.

## Użycie

**WWW na ciemnym tle NexaPanel**: `svg/voxone-logo-dark-background.svg` lub `png/voxone-logo-dark-background.png`.

**Instrukcje na białym tle**: `svg/voxone-logo-light-background.svg`.

**Favicon / aplikacja**: `icons/favicon.svg`, `icons/favicon.ico`; SVG i PNG symbolu.

**OLED monochromatyczny**: `display/boot-oled-128x64.png` i `display/boot-oled-128x64.h`. Dane nagłówka są row-major MSB-first; trzeba dostosować kolejność bajtów do użytej biblioteki SSD1306/SSD1309.

**Kolorowe TFT**: obrazy w `display/`; pliki `.bin` RGB565 little-endian są tylko referencyjnymi zasobami. W firmware trzeba potwierdzić orientację, porządek pikseli, szybkość SPI i sposób prezentacji bez zakłócania audio.

**PCB / grawer**: `pcb/*.svg`. Kolor sitodruku nadaje zakład produkujący PCB; symbole są monochromatyczne.

## Ograniczenia

- Samo przygotowanie plików NIE zmienia VoxOne na DEV, repozytorium ani urządzeń.
- SVG są prawdziwymi kształtami wektorowymi, automatycznie odrysowanymi z zaakceptowanego logo, nie oryginalnymi krzywymi projektanta; przed masową produkcją mogą wymagać ręcznego wyrównania ścieżek.
- Wariant biały służy ciemnym tłom, granatowy jasnym.
- Integrację startowych ekranów należy przeprowadzić osobno i offline zweryfikować, aby nie blokować bootu i OTA.
- Przed komercyjnym wykorzystaniem należy potwierdzić prawa do znaku i ewentualne podobieństwo do istniejących marek.
