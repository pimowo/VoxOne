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

  function resetRuntime() {
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
    if (typeof message.current === "number") state.current = message.current;
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