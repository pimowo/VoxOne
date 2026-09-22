# yoRadio reference for VoxOne

Repo referencyjne:
[https://github.com/e2002/yoradio](https://github.com/e2002/yoradio)

## Cel

yoRadio jest materiałem referencyjnym dla VoxOne, szczególnie dla:

- radia internetowego,
- obsługi listy stacji,
- playlist,
- metadanych stacji/utworu,
- buforowania i odtwarzania streamu,
- obsługi błędów/reconnect streamu,
- WebSocket/API, jeśli będzie przydatne,
- zachowania UI na małym ST7789 284x76.

## Zasady

- nie kopiować architektury yoRadio 1:1,
- nie mieszać kodu yoRadio z `src/` VoxOne,
- zachować własną architekturę VoxOne: Core / StateStore / CommandQueue / ownership źródeł,
- yoRadio traktować jako źródło wiedzy i przykład implementacyjny,
- przed kopiowaniem konkretnych fragmentów sprawdzać licencję i zgodność,
- rozwiązania przenosić do architektury VoxOne, a nie odwrotnie.

## Local setup

```text
git clone https://github.com/e2002/yoradio.git reference/yoRadio
```

## Important areas to inspect before VoxOne 0.5.0

Miejsce na późniejsze wskazanie konkretnych plików/funkcji yoRadio dotyczących:

- network radio,
- station list,
- stream decoder,
- metadata,
- reconnect,
- display ST7789_76.

Kod z `reference/` nie jest częścią firmware VoxOne.
