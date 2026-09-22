# WEB API

WebService starts on every MAIN boot in the single normal runtime mode.
The schema-7 `feat_web` value is retained in NVS only and has no runtime
effect; it is hidden from the configuration response and form.
`GET /api/v1/status` includes `bt_module_state` (Disabled, Waiting, Ready
or Unavailable). It is live status in the normal runtime.
`features.bluetoothEnabled` in the form now refers to the external VoxOneBT
module. A missing NVS key defaults OFF, but an existing saved `true` remains.
Holding the encoder at boot does not select a special mode. Saving settings
still validates and writes NVS, responds to the browser, then restarts.
When STA has an address, WWW is reachable over LAN; with no enabled Wi-Fi
profile, AP starts immediately. If no STA address arrives within about 10 s,
AP starts as fallback. Once AP is up, later STA recovery leaves AP active,
and the HTTP server listens on both interfaces.

## Aktualne endpointy

- `GET /`
- `GET /assets/voxone.css`
- `GET /api/v1/status`
- `GET /api/v1/config`
- `POST /api/v1/config`
- `POST /api/v1/config/reset`
- `GET /api/v1/stations`
- `POST /api/v1/stations`
- `POST /api/v1/stations/update`
- `POST /api/v1/stations/delete`
- `POST /api/v1/stations/play`
- `POST /api/v1/stations/move-up`
- `POST /api/v1/stations/move-down`
- `POST /api/v1/stations/default`
- `POST /wifi/save`
- `POST /wifi/clear`
- `POST /reboot`

Schema 8: `GET /api/v1/config` exposes `wifi.networks.0..4.ssid`,
`.enabled`, `.priority` and `.password_set`, never password values. Full
`POST /api/v1/config` requires each `.ssid`, `.enabled`, `.priority`,
`.password` and `.clearPassword` (0/1). An empty password input keeps
the stored secret; `.clearPassword=1` explicitly deletes it. Priorities
range 0..100. `wifi.last_good_profile` is read-only. Status includes
`wifi_active_profile` (-1 if none), `wifi_active_ssid`,
`wifi_last_good_profile` and `wifi_enabled_profiles`, never secrets.
`POST /wifi/save` remains a profile-0 compatibility shortcut;
`POST /wifi/clear` clears all five profiles.

API VoxOne pozostaje wersjonowane jako `/api/v1/...`.

Station mutations use the same per-boot `_token` returned by
`GET /api/v1/config`. Add/update accept `name`, direct `http://` or `https://` `url`
and `volumeTrim` (-20..20); all other station actions accept a stable
internal `id`. The Stations tab uses these endpoints without restarting.
HTTP 400 means malformed input, 404 an unknown ID, 409 a state/limit conflict,
and 500 a verified NVS write failure.

The historical `features.haDiscoveryEnabled` field remains hidden,
stored-only and ignored at runtime. MQTT exposes only the yoRadio-compatible
`command`, `status`, `volume` and `playlist` topics; WWW has no native HA
Discovery switch.
VoxOne nie obsługuje OTA; GET/POST `/update` nie są zarejestrowane i zwracają 404.
Firmware aktualizuje się przez USB/serial.
Pole `ui.navigationTimeoutMs` (dokumentacyjnie `ui.navigation_timeout_ms`)
ustawia po restarcie timeout BT NAV i przyszłej listy stacji: 1000–30000 ms,
domyślnie 5000 ms. Ekran głośności nadal ma timeout 2500 ms.
Formularz konfiguracji przesyła pełny `RuntimeConfig` jako
`application/x-www-form-urlencoded`. Odpowiedź GET nie zawiera haseł Wi-Fi
ani MQTT; puste pole hasła w POST zachowuje starą wartość. Walidacja błędnego
formularza zwraca HTTP 400, błąd NVS HTTP 500. Udany zapis pełnego snapshotu
odpowiada HTTP 200 i planuje restart po około 1000 ms, bez hot-reloadu.
Reset wymaga `confirm=RESET`; czyszczenie Wi-Fi i reboot wymagają
`confirm=YES`. Wszystkie mutujące endpointy wymagają pola `_token`
zwracanego przez GET konfiguracji. Token ogranicza CSRF, ale nie jest
uwierzytelnianiem użytkownika. WWW działa teraz równolegle z audio i RADIO;
trasy sterowania odtwarzaniem i głośnością nadal nie są rejestrowane (404).
Stała dostępność panelu zwiększa powierzchnię dostępu. Obecny AP jest
otwarty (`AP_PASSWORD=""`), a token formularza nie uwierzytelnia użytkownika.
Do czasu dodania kontroli dostępu nie należy traktować panelu jako bezpiecznego
w niezaufanej sieci LAN ani w zasięgu AP.
