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
  const stationAdd = document.getElementById("station-add");
  const stationImport = document.getElementById("station-import");
  const stationExport = document.getElementById("station-export");
  const stationImportFile = document.getElementById("station-import-file");
  const stationImportDialog = document.getElementById("station-import-dialog");
  const stationImportDetails = document.getElementById("station-import-details");
  const stationImportWarning = document.getElementById("station-import-warning");
  const stationImportCancel = document.getElementById("station-import-cancel");
  const stationImportConfirm = document.getElementById("station-import-confirm");
  const stationSaving = document.getElementById("stations-saving");
  const stationFeedback = document.getElementById("stations-feedback");
  const stationDialog = document.getElementById("station-dialog");
  const stationForm = document.getElementById("station-form");
  const stationName = document.getElementById("station-name");
  const stationUrl = document.getElementById("station-url");
  const stationOvol = document.getElementById("station-ovol");
  const stationFormError = document.getElementById("station-form-error");
  const stationCancel = document.getElementById("station-cancel");
  const stationSave = document.getElementById("station-save");
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
    count: 0,
    revision: null,
    loading: false,
    mutationInProgress: false,
    importing: false,
    error: "",
    notice: "",
    editorNumber: null,
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
  let wsCurrentSequence = 0;

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
    renderStationActions();
  }

  function renderStation() {
    text("current-station-name", state.station || "—");
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

  function renderStationFeedback() {
    stationFeedback.textContent = state.error || state.notice;
    stationFeedback.dataset.kind = state.error ? "error" : "notice";
    stationFormError.textContent = stationDialog.open ? state.error : "";
  }

  function setStationFeedback(message, isError = false) {
    state.error = isError ? message : "";
    state.notice = isError ? "" : message;
    renderStationFeedback();
  }

  function renderStationActions() {
    const locked = state.loading || state.mutationInProgress ||
      stationImportDialog.open || state.stationsStatus !== "ready" || !state.revision;
    stationAdd.disabled = locked;
    stationImport.disabled = locked;
    const exportLocked = state.loading || state.mutationInProgress || stationImportDialog.open;
    stationExport.setAttribute("aria-disabled", String(exportLocked));
    stationExport.tabIndex = exportLocked ? -1 : 0;
    stationRetry.disabled = state.loading || state.mutationInProgress;
    stationSaving.hidden = !state.mutationInProgress;
    stationSaving.textContent = state.importing ? "Importowanie..." : "Zapisywanie…";
    stationList.querySelectorAll(".station-mutate").forEach(button => {
      button.disabled = locked || button.dataset.boundary === "true";
    });
    for (const input of [stationName, stationUrl, stationOvol, stationCancel, stationSave]) {
      input.disabled = state.mutationInProgress;
    }
  }

  function stationAction(action, label, ariaLabel, number, boundary = false) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "station-action station-mutate";
    button.dataset.action = action;
    button.dataset.number = String(number);
    if (boundary) button.dataset.boundary = "true";
    button.textContent = label;
    button.setAttribute("aria-label", ariaLabel);
    if (action === "up" || action === "down") button.title = ariaLabel;
    button.disabled = boundary || state.loading || state.mutationInProgress;
    return button;
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

      const actions = document.createElement("div");
      actions.className = "station-actions";
      const play = document.createElement("button");
      play.type = "button";
      play.className = "station-play";
      play.dataset.number = String(station.number);
      play.textContent = "GRAJ";
      play.setAttribute("aria-label", "Graj: " + station.name);
      play.disabled = state.connection !== "connected";
      actions.append(
        play,
        stationAction("edit", "Edytuj", "Edytuj: " + station.name, station.number),
        stationAction("delete", "Usuń", "Usuń: " + station.name, station.number),
        stationAction("up", "↑", "Przesuń w górę: " + station.name, station.number, station.number === 1),
        stationAction("down", "↓", "Przesuń w dół: " + station.name, station.number, station.number === state.count)
      );
      row.append(number, main, ovol, actions);
      fragment.append(row);
    }
    stationList.replaceChildren(fragment);
    text("stations-count", query ? visible.length + " z " + state.count : stationCountLabel(state.count));
    document.querySelector(".station-list-heading").hidden = state.count === 0;
    text("stations-message", state.count === 0 ? "Brak zapisanych stacji." :
      visible.length === 0 ? "Brak stacji pasujących do wyszukiwania." : "");
    renderStationActions();
  }

  async function loadStations() {
    if (state.loading) return false;
    state.loading = true;
    state.stationsStatus = "loading";
    const currentSequence = wsCurrentSequence;
    text("stations-count", "Ładowanie...");
    text("stations-message", "Pobieranie listy stacji...");
    stationRetry.hidden = true;
    renderStationActions();
    try {
      const response = await fetch("/api/stations", { cache: "no-store" });
      if (!response.ok) throw new Error("HTTP " + response.status);
      const data = await response.json();
      if (!data || !Array.isArray(data.stations) || !Number.isInteger(data.count) ||
          data.count !== data.stations.length || !Number.isInteger(data.current) ||
          data.current < 0 || data.current > data.count ||
          typeof data.revision !== "string" || !/^[0-9a-f]{8}$/i.test(data.revision) ||
          !data.stations.every((station, index) =>
            station.number === index + 1 && typeof station.name === "string" &&
            typeof station.url === "string" && Number.isInteger(station.ovol))) {
        throw new Error("Niepoprawna odpowiedź listy stacji");
      }
      state.stations = data.stations;
      state.count = data.count;
      state.revision = data.revision;
      state.stationsStatus = "ready";
      state.error = "";
      state.notice = "";
      if (wsCurrentSequence === currentSequence) state.current = data.current;
      renderStationFeedback();
      renderStationList();
      return true;
    } catch (error) {
      console.warn("VoxOne: stations fetch failed", error);
      state.stationsStatus = "error";
      state.revision = null;
      state.error = "Nie udało się pobrać listy stacji.";
      state.notice = "";
      renderStationFeedback();
      text("stations-count", "Stacje niedostępne");
      text("stations-message", "Nie udało się pobrać listy stacji.");
      stationRetry.hidden = false;
      return false;
    } finally {
      state.loading = false;
      renderStationActions();
    }
  }

  function utf8Length(value) {
    if (typeof TextEncoder !== "undefined") return new TextEncoder().encode(value).length;
    return new Blob([value]).size;
  }

  function validateStationForm() {
    const name = stationName.value.trim();
    const url = stationUrl.value.trim();
    const ovolText = stationOvol.value.trim();
    if (!name) return { error: "Podaj nazwę stacji." };
    if (utf8Length(name) > 169) return { error: "Nazwa może mieć najwyżej 169 bajtów UTF-8." };
    if (!url) return { error: "Podaj URL stacji." };
    if (utf8Length(url) > 169) return { error: "URL może mieć najwyżej 169 bajtów." };
    if (!/^[+-]?\d+$/.test(ovolText) || !Number.isInteger(Number(ovolText)) ||
        Number(ovolText) < -30 || Number(ovolText) > 30) {
      return { error: "OVOL musi być liczbą całkowitą od −30 do 30." };
    }
    return { name, url, ovol: String(Number(ovolText)) };
  }

  function mutationErrorMessage(status) {
    return ({
      400: "Nieprawidłowe dane.",
      404: "Stacja już nie istnieje.",
      409: "Lista stacji została zmieniona w innym oknie.",
      422: "Nazwa, URL lub OVOL są nieprawidłowe.",
      507: "Brak miejsca na bezpieczny zapis playlisty.",
      500: "Nie udało się zapisać playlisty. Poprzednia lista została zachowana."
    })[status] || "Nie udało się zapisać zmiany. Sprawdź połączenie i spróbuj ponownie.";
  }

  async function mutateStation(route, fields, successMessage) {
    if (state.loading || state.mutationInProgress || state.stationsStatus !== "ready" ||
        !state.revision) return;
    state.mutationInProgress = true;
    setStationFeedback("");
    renderStationActions();
    try {
      const body = new URLSearchParams({ revision: state.revision, ...fields });
      const response = await fetch("/api/stations/" + route, {
        method: "POST", body, cache: "no-store"
      });
      const result = await response.json().catch(() => null);
      if (response.status === 409 && result && result.error === "revision_conflict") {
        if (stationDialog.open) stationDialog.close();
        state.editorNumber = null;
        const refreshed = await loadStations();
        setStationFeedback(refreshed ?
          "Lista stacji została zmieniona w innym oknie. Odświeżono aktualne dane." :
          "Lista stacji została zmieniona w innym oknie. Nie udało się pobrać aktualnych danych.", !refreshed);
        return;
      }
      if (!response.ok) throw { status: response.status };
      if (!result || result.ok !== true) throw { status: 500 };
      if (stationDialog.open) stationDialog.close();
      state.editorNumber = null;
      const refreshed = await loadStations();
      setStationFeedback(refreshed ? successMessage :
        "Zmiana została zapisana, ale nie udało się odświeżyć listy. Użyj przycisku Ponów.", !refreshed);
    } catch (error) {
      console.warn("VoxOne: station mutation failed", error);
      setStationFeedback(mutationErrorMessage(error.status), true);
    } finally {
      state.mutationInProgress = false;
      renderStationActions();
    }
  }

  function importErrorMessage(status) {
    return ({
      400: "Nieprawidłowe żądanie importu.",
      409: "Lista stacji została zmieniona w innym oknie.",
      413: "Plik playlisty jest zbyt duży.",
      422: "Plik playlisty ma nieprawidłowy format.",
      507: "Brak miejsca na bezpieczny zapis playlisty.",
      500: "Nie udało się zaimportować playlisty. Poprzednia lista została zachowana."
    })[status] || "Nie udało się zaimportować playlisty. Sprawdź połączenie i spróbuj ponownie.";
  }

  async function importPlaylist() {
    if (state.loading || state.mutationInProgress || state.stationsStatus !== "ready" ||
        !state.revision) return;
    const file = stationImportFile.files[0];
    if (!file) {
      stationImportDialog.close();
      return;
    }
    if (file.size > 8192) {
      stationImportDialog.close();
      setStationFeedback(importErrorMessage(413), true);
      return;
    }
    const revision = state.revision;
    state.mutationInProgress = true;
    state.importing = true;
    stationImportDialog.close();
    setStationFeedback("");
    renderStationActions();
    try {
      const body = new FormData();
      body.append("revision", revision);
      if (file.size === 0) body.append("empty", "1");
      else body.append("file", file, file.name);
      const response = await fetch("/api/stations/import", {
        method: "POST", body, cache: "no-store"
      });
      const result = await response.json().catch(() => null);
      if (response.status === 409) {
        const refreshed = await loadStations();
        setStationFeedback(refreshed ?
          "Lista stacji została zmieniona w innym oknie. Odświeżono aktualne dane. Wybierz plik ponownie, jeśli nadal chcesz go zaimportować." :
          "Lista stacji została zmieniona w innym oknie. Nie udało się pobrać aktualnych danych.", !refreshed);
        return;
      }
      if (!response.ok) throw { status: response.status };
      if (!result || result.ok !== true) throw { status: 500 };
      const refreshed = await loadStations();
      setStationFeedback(refreshed ? "Playlista została zaimportowana." :
        "Playlista została zaimportowana, ale nie udało się odświeżyć listy. Użyj przycisku Ponów.", !refreshed);
    } catch (error) {
      console.warn("VoxOne: playlist import failed", error);
      setStationFeedback(importErrorMessage(error.status), true);
    } finally {
      stationImportFile.value = "";
      state.importing = false;
      state.mutationInProgress = false;
      renderStationActions();
    }
  }

  function openStationForm(number = null) {
    if (state.loading || state.mutationInProgress || state.stationsStatus !== "ready") return;
    const station = number === null ? null : state.stations[number - 1];
    if (number !== null && (!station || station.number !== number)) return;
    state.editorNumber = number;
    stationForm.reset();
    text("station-dialog-title", station ? "Edytuj stację" : "Dodaj stację");
    stationName.value = station ? station.name : "";
    stationUrl.value = station ? station.url : "";
    stationOvol.value = station ? String(station.ovol) : "0";
    setStationFeedback("");
    stationDialog.showModal();
    stationName.focus();
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
      wsCurrentSequence++;
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
  stationAdd.addEventListener("click", () => openStationForm());
  stationImport.addEventListener("click", () => {
    if (!stationImport.disabled) stationImportFile.click();
  });
  stationExport.addEventListener("click", event => {
    if (stationExport.getAttribute("aria-disabled") === "true") event.preventDefault();
  });
  stationImportFile.addEventListener("change", () => {
    const file = stationImportFile.files[0];
    if (!file) {
      stationImportFile.value = "";
      return;
    }
    if (file.size > 8192) {
      stationImportFile.value = "";
      setStationFeedback(importErrorMessage(413), true);
      return;
    }
    stationImportDetails.textContent = file.name + " · " + file.size + " B";
    stationImportWarning.textContent = file.size === 0 ?
      "Wybrany plik jest pusty.\nImport usunie wszystkie stacje z listy.\nKontynuować?" :
      "Import zastąpi obecną listę stacji. Aktualna playlista zostanie zachowana bezpiecznie do czasu zakończenia zapisu.";
    setStationFeedback("");
    stationImportDialog.showModal();
    renderStationActions();
    stationImportConfirm.focus();
  });
  stationImportCancel.addEventListener("click", () => stationImportDialog.close());
  stationImportConfirm.addEventListener("click", importPlaylist);
  stationImportDialog.addEventListener("cancel", event => {
    if (state.mutationInProgress) event.preventDefault();
  });
  stationImportDialog.addEventListener("close", () => {
    stationImportFile.value = "";
    renderStationActions();
  });
  stationCancel.addEventListener("click", () => stationDialog.close());
  stationDialog.addEventListener("cancel", event => {
    if (state.mutationInProgress) event.preventDefault();
  });
  stationDialog.addEventListener("close", () => {
    state.editorNumber = null;
    stationFormError.textContent = "";
  });
  stationForm.addEventListener("submit", event => {
    event.preventDefault();
    if (state.mutationInProgress) return;
    const fields = validateStationForm();
    if (fields.error) {
      setStationFeedback(fields.error, true);
      return;
    }
    if (state.editorNumber === null) {
      mutateStation("add", fields, "Dodano stację.");
    } else {
      mutateStation("edit", { number: String(state.editorNumber), ...fields }, "Zapisano stację.");
    }
  });
  stationList.addEventListener("click", event => {
    const button = event.target.closest("button");
    if (!button || button.disabled || !stationList.contains(button)) return;
    const number = Number(button.dataset.number);
    if (!Number.isInteger(number) || number < 1 || number > state.count) return;
    if (button.classList.contains("station-play")) {
      if (send("playstation", number)) flashButton(button);
      return;
    }
    if (state.loading || state.mutationInProgress || state.stationsStatus !== "ready") return;
    const station = state.stations[number - 1];
    if (!station || station.number !== number) return;
    switch (button.dataset.action) {
      case "edit":
        openStationForm(number);
        break;
      case "delete": {
        const activeNote = number === state.current ? "\nTo jest aktualnie odtwarzana stacja." : "";
        if (window.confirm("Usunąć stację „" + station.name + "”?" + activeNote)) {
          mutateStation("delete", { number: String(number) }, "Usunięto stację.");
        }
        break;
      }
      case "up":
      case "down": {
        const target = number + (button.dataset.action === "up" ? -1 : 1);
        if (target >= 1 && target <= state.count) {
          mutateStation("reorder", { from: String(number), to: String(target) }, "Zmieniono kolejność stacji.");
        }
        break;
      }
      default:
        break;
    }
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
