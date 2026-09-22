# MQTT

VoxOne implements only the yoRadio-compatible MQTT boundary under the
configured root (or the automatic `voxone-XXXXXX` root):

| Topic | Direction | Payload |
| --- | --- | --- |
| `<root>/command` | client -> VoxOne | yoRadio plain-text command |
| `<root>/status` | VoxOne -> clients | retained five-field yoRadio JSON |
| `<root>/volume` | VoxOne -> clients | retained decimal 0..254 |
| `<root>/playlist` | VoxOne -> clients | retained `http://<LAN-IP>/data/playlist.csv` |

On connect VoxOne subscribes only to `<root>/command`, then publishes status,
volume and playlist. Status is republished only when its exact JSON payload
changes. Volume is republished only when its converted wire value changes.
Playlist is republished after MQTT reconnect and after a LAN IP change. The
endpoint is generated dynamically from StationStore, so station edits do not
change its URL.

There is no VoxOne-native `state/*` or `command/*` protocol, LWT/availability,
event topic or MQTT HA Discovery. Home Assistant integration uses
`ha_yoradio`. MQTT remains optional and must not affect basic device operation.
