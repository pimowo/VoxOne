(() => {
  "use strict";

  const tabs = ["status", "stations", "settings", "system"];
  const buttons = {
    prev: document.getElementById("prev-button"),
    play: document.getElementById("play-button"),
    next: document.getElementById("next-button")
  };
  const volumeSlider = document.getElementById("volume-slider");
  const volumeMeter = document.getElementById("volume-meter");
  const stationList = document.getElementById("station-list");
  const stationSearch = document.getElementById("station-search");
  const stationClear = document.getElementById("station-search-clear");
  const stationRetry = document.getElementById("stations-retry");
  const state = {
    connection: "connecting",
    source: null,
    station: null,
    metadata: null,
    codec: null,
    bitrate: null,
    rssi: null,
    volume: null,
    playing: null,
    current: null,
    stations: [],
    stationsStatus: "idle",
    stationQuery: "",
    ip: null,
    profile: typeof voxOneProfile === "string" ? voxOneProfile : null,
    version: typeof voxOneVersion === "string" ? voxOneVersion : null,
    baseVersion: typeof yoRadioVersion === "string" ? yoRadioVersion : null
  };

  let socket = null;
  let reconnectTimer = 0;
  let reconnectDelay = 1000;
  let extraSyncTimer = 0;
  let extraSyncSent = false;
  let volumeTimer = 0;
  let volumeAckTimer = 0;
  let lastVolumeSentAt = 0;
  let pendingVolume = null;
  let awaitingVolume = null;
  let draggingVolume = false;
  let leaving = false;

  function text(id, value) {
    const node = document.getElementById(id);
    if (node && node.textContent !== value) node.textContent = value;
  }

  function showTab() {
    const requested = location.hash.slice(1);
    const selected = tabs.includes(requested) ? requested : "status";
    for (const tab of tabs) {
      document.getElementById(tab).hidden = tab !== selected;
      const link = document.querySelector('[data-tab="' + tab + '"]');
      if (tab === selected) link.setAttribute("aria-current", "page");
      else link.removeAttribute("aria-current");
    }
    if (selected === "stations" && state.stationsStatus === "idle") loadStations();
  }

  function renderConnection() {
    const badge = document.getElementById("ws-state");
    badge.dataset.state = state.connection;
    badge.textContent = {
      connected: "Połączono",
      connecting: "Łączenie...",
      disconnected: "Rozłączono"
    }[state.connection];
    const connected = state.connection === "connected";
    buttons.prev.disabled = !connected;
    buttons.next.disabled = !connected;
    buttons.play.disabled = !connected || state.playing === null;
    volumeSlider.disabled = !connected || state.volume === null;
    stationList.querySelectorAll(".station-play").forEach(button => {
      button.disabled = !connected;
    });
  }

  function renderStation() {
    text("station-name", state.station || "—");
  }

  function renderMetadata() {
    text("metadata", state.metadata || "—");
  }

  function renderStream() {
    const parts = [];
    if (state.codec) parts.push(state.codec);
    if (state.bitrate > 0) parts.push(state.bitrate + " kb/s");
    text("codec", parts.length ? parts.join(" · ") : "—");
  }

  function renderSource() {
    text("source", state.source || "—");
  }

  function renderRssi() {
    text("rssi", state.rssi === null ? "—" : state.rssi + " dBm");
  }

  function renderPlaying() {
    buttons.play.textContent = state.playing === null ? "—" : state.playing ? "■" : "▶";
    buttons.play.setAttribute("aria-label", state.playing === null ? "Stan odtwarzania niedostępny" : state.playing ? "Zatrzymaj" : "Odtwarzaj");
    renderConnection();
  }

  function showVolume(value) {
    text("volume", value === null ? "—" : String(value));
    volumeMeter.style.width = value === null ? "0%" : (100 * value / 254) + "%";
    volumeSlider.hidden = value === null;
    if (value !== null) volumeSlider.value = String(value);
    renderConnection();
  }

  function renderVolume() {
    if (draggingVolume || awaitingVolume !== null) return;
    showVolume(state.volume);
  }

  function renderIdentity() {
    const version = state.version || "—";
    const profile = state.profile ? state.profile.toUpperCase() : "—";
    text("header-version", "VoxOne " + version);
    text("header-profile", "Profil " + profile);
    text("system-version", "VoxOne " + version);
    text("system-profile", profile);
    text("system-base", "yoRadio " + (state.baseVersion || "—"));
  }

  function updateCurrentMarker(previous) {
    for (const number of [previous, state.current]) {
      if (!Number.isInteger(number)) continue;
      const row = stationList.querySelector('[data-station="' + number + '"]');
      if (!row) continue;
      const active = number === state.current;
      row.classList.toggle("is-current", active);
      row.querySelector(".station-playing").hidden = !active;
      if (active) row.setAttribute("aria-current", "true");
      else row.removeAttribute("aria-current");
    }
  }

  function stationCountLabel(count) {
    if (count === 1) return "1 stacja";
    if (count % 10 >= 2 && count % 10 <= 4 && (count % 100 < 12 || count % 100 > 14)) {
      return count + " stacje";
    }
    return count + " stacji";
  }

  function renderStationList() {
    const query = state.stationQuery.trim().toLocaleLowerCase("pl");
    const visible = state.stations.filter(station =>
      station.name.toLocaleLowerCase("pl").includes(query) ||
      station.url.toLocaleLowerCase("pl").includes(query)
    );
    const fragment = document.createDocumentFragment();
    for (const station of visible) {
      const active = station.number === state.current;
      const row = document.createElement("li");
      row.className = "station-row";
      row.dataset.station = String(station.number);
      if (active) {
        row.classList.add("is-current");
        row.setAttribute("aria-current", "true");
      }

      const number = document.createElement("span");
      number.className = "station-number";
      number.textContent = String(station.number);

      const main = document.createElement("div");
      main.className = "station-main";
      const title = document.createElement("div");
      title.className = "station-title";
      const name = document.createElement("strong");
      name.textContent = station.name;
      name.title = station.name;
      const badge = document.createElement("span");
      badge.className = "station-playing";
      badge.textContent = "GRA";
      badge.hidden = !active;
      title.append(name, badge);
      const url = document.createElement("span");
      url.className = "station-url";
      url.textContent = station.url;
      url.title = station.url;
      main.append(title, url);

      const ovol = document.createElement("span");
      ovol.className = "station-ovol";
      ovol.textContent = (station.ovol > 0 ? "+" : "") + station.ovol;

      const play = document.createElement("button");
      play.type = "button";
      play.className = "station-play";
      play.dataset.number = String(station.number);
      play.textContent = "GRAJ";
      play.setAttribute("aria-label", "Graj: " + station.name);
      play.disabled = state.connection !== "connected";

      row.append(number, main, ovol, play);
      fragment.append(row);
    }
    stationList.replaceChildren(fragment);
    text("stations-count", query ? visible.length + " z " + state.stations.length : stationCountLabel(state.stations.length));
    document.querySelector(".station-list-heading").hidden = state.stations.length === 0;
    text("stations-message", state.stations.length === 0 ? "Brak zapisanych stacji." :
      visible.length === 0 ? "Brak stacji pasujących do wyszukiwania." : "");
  }

  async function loadStations() {
    if (state.stationsStatus === "loading") return;
    state.stationsStatus = "loading";
    text("stations-count", "Ładowanie...");
    text("stations-message", "Pobieranie listy stacji...");
    stationRetry.hidden = true;
    try {
      const response = await fetch("/api/stations", { cache: "no-store" });
      if (!response.ok) throw new Error("HTTP " + response.status);
      const data = await response.json();
      if (!data || !Array.isArray(data.stations) || !Number.isInteger(data.count) ||
          data.count !== data.stations.length || !Number.isInteger(data.current) ||
          data.current < 0 || data.current > data.count ||
          !data.stations.every((station, index) =>
            station.number === index + 1 && typeof station.name === "string" &&
            typeof station.url === "string" && Number.isInteger(station.ovol))) {
        throw new Error("Niepoprawna odpowiedź listy stacji");
      }
      state.stations = data.stations;
      state.stationsStatus = "ready";
      if (state.current === null) state.current = data.current;
      renderStationList();
    } catch (error) {
      console.warn("VoxOne: stations fetch failed", error);
      state.stationsStatus = "error";
      text("stations-count", "Stacje niedostępne");
      text("stations-message", "Nie udało się pobrać listy stacji.");
      stationRetry.hidden = false;
    }
  }
  function resetRuntime() {
    const previousCurrent = state.current;
    for (const key of ["source", "station", "metadata", "codec", "bitrate", "rssi", "volume", "playing", "current", "ip"]) {
      state[key] = null;
    }
    draggingVolume = false;
    awaitingVolume = null;
    pendingVolume = null;
    clearTimeout(volumeTimer);
    clearTimeout(volumeAckTimer);
    renderStation();
    renderMetadata();
    renderStream();
    renderSource();
    renderRssi();
    renderPlaying();
    showVolume(null);
    text("system-ip", "—");
    updateCurrentMarker(previousCurrent);
  }

  function send(command, value) {
    if (!socket || socket.readyState !== WebSocket.OPEN) return false;
    socket.send(command + "=" + value);
    return true;
  }

  function requestExtraSync() {
    if (extraSyncSent) return;
    extraSyncSent = true;
    clearTimeout(extraSyncTimer);
    send("getsystem", 1);
    send("getrssi", 1);
  }

  function updatePayload(id, value) {
    switch (id) {
      case "nameset":
        state.station = String(value || "");
        renderStation();
        break;
      case "meta":
        state.metadata = String(value || "");
        renderMetadata();
        break;
      case "volume": {
        const volume = Number(value);
        if (!Number.isInteger(volume) || volume < 0 || volume > 254) break;
        state.volume = volume;
        if (awaitingVolume === volume) {
          awaitingVolume = null;
          clearTimeout(volumeAckTimer);
        }
        renderVolume();
        break;
      }
      case "rssi": {
        const rssi = Number(value);
        state.rssi = Number.isFinite(rssi) ? rssi : null;
        renderRssi();
        break;
      }
      case "bitrate": {
        const bitrate = Number(value);
        state.bitrate = Number.isFinite(bitrate) && bitrate > 0 ? bitrate : null;
        renderStream();
        break;
      }
      case "fmt":
        state.codec = value && value !== "bitrate" ? String(value) : null;
        renderStream();
        break;
      case "playerwrap":
        state.playing = value === "playing" ? true : value === "stopped" ? false : null;
        renderPlaying();
        break;
      case "source":
        state.source = value ? String(value) : null;
        renderSource();
        break;
      default:
        break;
    }
  }

  function receive(raw) {
    let message;
    try {
      message = JSON.parse(raw);
    } catch (error) {
      console.warn("VoxOne: invalid WebSocket JSON", error);
      return;
    }
    if (Array.isArray(message.payload)) {
      for (const item of message.payload) updatePayload(item.id, item.value);
    }
    if (typeof message.current === "number") {
      const previousCurrent = state.current;
      state.current = Number.isInteger(message.current) ? message.current : null;
      updateCurrentMarker(previousCurrent);
    }
    if (typeof message.playermode === "string") {
      const modes = { modeweb: "WEB", modesd: "SD" };
      state.source = modes[message.playermode] || message.playermode;
      renderSource();
      requestExtraSync();
    }
    if (typeof message.ipaddr === "string") {
      state.ip = message.ipaddr;
      text("system-ip", state.ip || "—");
    }
  }

  function connect() {
    if (leaving || (socket && socket.readyState < WebSocket.CLOSING)) return;
    clearTimeout(reconnectTimer);
    state.connection = "connecting";
    renderConnection();
    const scheme = location.protocol === "https:" ? "wss://" : "ws://";
    const ws = new WebSocket(scheme + location.host + "/ws");
    socket = ws;
    ws.onopen = () => {
      if (socket !== ws) return;
      reconnectDelay = 1000;
      extraSyncSent = false;
      resetRuntime();
      state.connection = "connected";
      renderConnection();
      send("getindex", 1);
      extraSyncTimer = setTimeout(requestExtraSync, 1500);
    };
    ws.onmessage = event => {
      if (socket === ws) receive(event.data);
    };
    ws.onerror = () => ws.close();
    ws.onclose = () => {
      if (socket !== ws) return;
      clearTimeout(extraSyncTimer);
      clearTimeout(volumeTimer);
      clearTimeout(volumeAckTimer);
      socket = null;
      state.connection = "disconnected";
      renderConnection();
      if (!leaving) {
        reconnectTimer = setTimeout(connect, reconnectDelay);
        reconnectDelay = Math.min(reconnectDelay * 2, 15000);
      }
    };
  }

  function flashButton(button) {
    button.classList.add("is-pending");
    setTimeout(() => button.classList.remove("is-pending"), 600);
  }

  buttons.prev.addEventListener("click", () => {
    if (send("prev", 1)) flashButton(buttons.prev);
  });
  buttons.play.addEventListener("click", () => {
    if (send("toggle", 1)) flashButton(buttons.play);
  });
  buttons.next.addEventListener("click", () => {
    if (send("next", 1)) flashButton(buttons.next);
  });

  stationSearch.addEventListener("input", () => {
    state.stationQuery = stationSearch.value;
    stationClear.disabled = state.stationQuery.length === 0;
    if (state.stationsStatus === "ready") renderStationList();
  });
  stationClear.addEventListener("click", () => {
    stationSearch.value = "";
    state.stationQuery = "";
    stationClear.disabled = true;
    if (state.stationsStatus === "ready") renderStationList();
    stationSearch.focus();
  });
  stationRetry.addEventListener("click", loadStations);
  stationList.addEventListener("click", event => {
    const button = event.target.closest(".station-play");
    if (!button || button.disabled) return;
    const number = Number(button.dataset.number);
    if (Number.isInteger(number) && send("playstation", number)) flashButton(button);
  });

  function sendPendingVolume() {
    clearTimeout(volumeTimer);
    if (pendingVolume === null) return;
    if (send("vol", pendingVolume)) lastVolumeSentAt = performance.now();
    pendingVolume = null;
  }

  volumeSlider.addEventListener("input", () => {
    if (state.volume === null || state.connection !== "connected") return;
    draggingVolume = true;
    pendingVolume = Number(volumeSlider.value);
    showVolume(pendingVolume);
    const wait = 120 - (performance.now() - lastVolumeSentAt);
    if (wait <= 0) sendPendingVolume();
    else {
      clearTimeout(volumeTimer);
      volumeTimer = setTimeout(sendPendingVolume, wait);
    }
  });

  volumeSlider.addEventListener("change", () => {
    if (!draggingVolume) return;
    const finalVolume = Number(volumeSlider.value);
    draggingVolume = false;
    pendingVolume = finalVolume;
    awaitingVolume = finalVolume;
    sendPendingVolume();
    clearTimeout(volumeAckTimer);
    volumeAckTimer = setTimeout(() => {
      awaitingVolume = null;
      renderVolume();
    }, 1500);
  });

  window.addEventListener("hashchange", showTab);
  window.addEventListener("online", () => {
    if (!socket) connect();
  });
  window.addEventListener("beforeunload", () => {
    leaving = true;
    clearTimeout(reconnectTimer);
    if (socket) socket.close();
  });
  renderIdentity();
  renderConnection();
  showTab();
  connect();
})();