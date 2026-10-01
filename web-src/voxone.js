(() => {
  "use strict";

  const tabs = ["status", "stations", "audio", "settings", "system", "update"];
  const updateImages = {
    firmware: {
      file: document.getElementById("update-firmware-file"),
      name: document.getElementById("update-firmware-name"),
      button: document.getElementById("update-firmware-button")
    },
    spiffs: {
      file: document.getElementById("update-spiffs-file"),
      name: document.getElementById("update-spiffs-name"),
      button: document.getElementById("update-spiffs-button")
    }
  };
  const updateStatus = document.getElementById("update-status");
  const updateProgress = document.getElementById("update-progress");
  const updateBtFile = document.getElementById("update-bt-file");
  const updateBtFileName = document.getElementById("update-bt-file-name");
  let updateBusy = false;
  const rebootButton = document.getElementById("reboot-button");
  const rebootDialog = document.getElementById("reboot-dialog");
  const rebootCancel = document.getElementById("reboot-cancel");
  const rebootConfirm = document.getElementById("reboot-confirm");
  const rebootStatus = document.getElementById("reboot-status");
  let rebootStarted = false;
  const buttons = {
    prev: document.getElementById("prev-button"),
    play: document.getElementById("play-button"),
    next: document.getElementById("next-button")
  };
  const volumeSliders = [
    document.getElementById("volume-slider"),
    document.getElementById("audio-volume-slider")
  ];
  const maximumVolumeSlider = document.getElementById("maximum-volume-slider");
  const maximumVolumeValue = document.getElementById("maximum-volume-value");
  const startupModeSelect = document.getElementById("startup-mode-select");
  const startupFixedControl = document.getElementById("startup-fixed-control");
  const startupFixedSlider = document.getElementById("startup-fixed-slider");
  const startupFixedValue = document.getElementById("startup-fixed-value");
  const volumeSettingsPending = document.getElementById("volume-settings-pending");
  const displaySettingsCard = document.getElementById("display-settings-card");
  const flipScreenControl = document.getElementById("flip-screen-control");
  const flipScreenToggle = document.getElementById("flip-screen-toggle");
  const flipScreenFeedback = document.getElementById("flip-screen-feedback");
  const vuMeterControl = document.getElementById("vu-meter-control");
  const vuMeterToggle = document.getElementById("vu-meter-toggle");
  const vuMeterValue = document.getElementById("vu-meter-value");
  const brightnessControl = document.getElementById("brightness-control");
  const brightnessSlider = document.getElementById("brightness-slider");
  const brightnessValue = document.getElementById("brightness-value");
  const autoReturnControl = document.getElementById("auto-return-control");
  const stationListTimeoutInput = document.getElementById("station-list-timeout");
  const btTimeoutControl = document.getElementById("bt-timeout-control");
  const btTransportTimeoutInput = document.getElementById("bt-transport-timeout");
  const rtcCard = document.getElementById("rtc-card");
  const rtcNtpInterval = document.getElementById("rtc-ntp-interval");
  const rtcWriteInterval = document.getElementById("rtc-write-interval");
  const rtcSyncNow = document.getElementById("rtc-sync-now");
  const rtcFeedback = document.getElementById("rtc-feedback");
  const audioControls = {
    bass: { slider: document.getElementById("audio-bass-slider"), command: "bass" },
    middle: { slider: document.getElementById("audio-middle-slider"), command: "middle" },
    treble: { slider: document.getElementById("audio-treble-slider"), command: "trebble" },
    balance: { slider: document.getElementById("audio-balance-slider"), command: "balance" }
  };
  const tonePresets = {
    flat: [0, 0, 0],
    bass: [6, -2, 0],
    rock: [5, -2, 4],
    pop: [3, 2, 3],
    mowa: [-4, 4, 2]
  };
  const tonePresetButtons = [...document.querySelectorAll("[data-tone-preset]")];
  const tonePresetUser = document.getElementById("audio-preset-user");
  const myStationsTab = document.getElementById("my-stations-tab");
  const directoryTab = document.getElementById("directory-tab");
  const myStationsView = document.getElementById("my-stations-view");
  const directoryView = document.getElementById("directory-view");
  const directoryForm = document.getElementById("directory-form");
  const directoryQuery = document.getElementById("directory-query");
  const directoryCountry = document.getElementById("directory-country");
  const directorySearchButton = document.getElementById("directory-search");
  const directoryStatus = document.getElementById("directory-status");
  const directoryResults = document.getElementById("directory-results");
  let directoryBusy = false;
  let directoryStations = [];
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
  const mqttForm = document.getElementById("mqtt-form");
  const mqttEnabled = document.getElementById("mqtt-enabled");
  const mqttFields = document.getElementById("mqtt-fields");
  const mqttHost = document.getElementById("mqtt-host");
  const mqttPort = document.getElementById("mqtt-port");
  const mqttUsername = document.getElementById("mqtt-username");
  const mqttPassword = document.getElementById("mqtt-password");
  const mqttPasswordState = document.getElementById("mqtt-password-state");
  const mqttClearPassword = document.getElementById("mqtt-clear-password");
  const mqttCustomRootWrap = document.getElementById("mqtt-custom-root-wrap");
  const mqttRootTopic = document.getElementById("mqtt-root-topic");
  const mqttEffectiveRoot = document.getElementById("mqtt-effective-root");
  const mqttRootModeLabel = document.getElementById("mqtt-root-mode-label");
  const mqttTopicList = document.getElementById("mqtt-topic-list");
  const mqttFeedback = document.getElementById("mqtt-feedback");
  const mqttSave = document.getElementById("mqtt-save");
  const haName = document.getElementById("ha-name");
  const haYaml = document.getElementById("ha-yaml");
  const haCopy = document.getElementById("ha-copy");
  const haCopyStatus = document.getElementById("ha-copy-status");
  let mqttConfigLoaded = false;
  let mqttPasswordSet = false;
  let mqttEffectiveRootValue = "";
  let mqttSaving = false;
  let rtcSyncing = false;
  let rtcStatusLoading = false;
  const state = {
    connection: "connecting",
    webStatus: null,
    source: null,
    station: null,
    metadata: null,
    codec: null,
    bitrate: null,
    rssi: null,
    volume: null,
    volume100: null,
    maximumVolume: null,
    startupMode: null,
    startupFixedVolume: null,
    brightness: null,
    canBrightness: false,
    stationListTimeout: null,
    btTransportTimeout: null,
    canBtTransport: false,
    flip: null,
    canFlip: false,
    vu: null,
    canVu: false,
    bass: null,
    middle: null,
    treble: null,
    balance: null,
    playing: null,
    current: null,
    stations: [],
    stationsStatus: "idle",
    stationQuery: "",
    count: 0,
    revision: null,
    pendingMetadataNumber: null,
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
  const volumeSettingEditing = { maximumVolume: null, startupMode: null, startupFixedVolume: null };
  const volumeSettingTimers = { maximumVolume: 0, startupMode: 0, startupFixedVolume: 0 };
  const volumeSettingDragging = { maximumVolume: false, startupFixedVolume: false };
  let draggingVolume = false;
  let activeVolumeSlider = null;
  let leaving = false;
  let wsCurrentSequence = 0;
  let flipPending = false;
  let flipTimer = 0;
  let vuAwaiting = null;
  let vuTimer = 0;
  let brightnessDragging = false;
  let brightnessPending = null;
  let brightnessAwaiting = null;
  let brightnessTimer = 0;
  let brightnessAckTimer = 0;
  let lastBrightnessSentAt = 0;

  function text(id, value) {
    const node = document.getElementById(id);
    if (node && node.textContent !== value) node.textContent = value;
  }

  function renderUpdateFiles() {
    for (const image of Object.values(updateImages)) {
      const file = image.file.files[0];
      image.name.textContent = file ? file.name + " · " +
        (file.size / 1024 / 1024).toFixed(2) + " MB" : "Nie wybrano pliku.";
      image.file.disabled = updateBusy;
      image.button.disabled = updateBusy || !file;
    }
    updateBtFileName.textContent = updateBtFile.files[0]?.name || "Nie wybrano pliku.";
  }

  function renderBtModule() {
    const module = state.webStatus?.btModule;
    const online = state.connection === "connected" && module?.online === true;
    text("update-bt-online", online ? "TAK" : "NIE");
    text("update-bt-firmware", online && module.firmware ? module.firmware : "—");
    text("update-bt-protocol", online && module.protocol ? String(module.protocol) : "—");
    text("update-bt-name", online && module.name ? module.name : "—");
    text("update-bt-capabilities", online && module.capabilities ? module.capabilities : "—");
  }

  function setUpdateStatus(message, error = false) {
    updateStatus.textContent = message;
    updateStatus.dataset.kind = error ? "error" : "status";
  }

  async function waitForDeviceRestart({ delay = 2500, timeout = 30000, onReturn, onTimeout }) {
    await new Promise(resolve => setTimeout(resolve, delay));
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
      const controller = new AbortController();
      const requestTimeout = setTimeout(() => controller.abort(), 2500);
      try {
        const response = await fetch("/variables.js?update=" + Date.now(), {
          cache: "no-store", signal: controller.signal
        });
        if (response.ok) {
          onReturn();
          return;
        }
      } catch (_) { /* The device may still be restarting. */ }
      finally { clearTimeout(requestTimeout); }
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
    onTimeout();
  }

  async function waitForUpdateRestart() {
    setUpdateStatus("Aktualizacja zakończona. Restart urządzenia...");
    await waitForDeviceRestart({
      onReturn: () => location.replace("/voxone.html?updated=" + Date.now() + "#update"),
      onTimeout: () => {
        updateBusy = false;
        renderUpdateFiles();
        setUpdateStatus("Urządzenie nie wróciło w ciągu 30 s. Sprawdź połączenie i odśwież stronę.", true);
      }
    });
  }

  function setRebootStatus(message, error = false) {
    rebootStatus.textContent = message;
    rebootStatus.dataset.kind = error ? "error" : "status";
  }

  async function rebootDevice() {
    if (rebootStarted) return;
    rebootStarted = true;
    rebootButton.disabled = true;
    rebootConfirm.disabled = true;
    rebootDialog.close();
    setRebootStatus("Restartowanie urządzenia...");
    if (!send("reboot", 1)) {
      rebootStarted = false;
      rebootButton.disabled = false;
      rebootConfirm.disabled = false;
      setRebootStatus("Brak połączenia z urządzeniem.", true);
      return;
    }
    await waitForDeviceRestart({
      delay: 1200,
      onReturn: () => {
        setRebootStatus("VoxOne uruchomiony");
        setTimeout(() => location.reload(), 700);
      },
      onTimeout: () => setRebootStatus("Nie udało się ponownie połączyć z VoxOne.", true)
    });
  }

  function uploadUpdateImage(target) {
    if (updateBusy) return;
    const file = updateImages[target].file.files[0];
    if (!file) return;
    const lowerName = file.name.toLowerCase();
    if (!lowerName.endsWith(".bin") || lowerName.endsWith("full.bin")) {
      setUpdateStatus("Wybierz obraz .bin dla urządzenia. Nie używaj full.bin.", true);
      return;
    }
    if ((target === "firmware" && lowerName.includes("spiffs")) ||
        (target === "spiffs" && lowerName.includes("firmware"))) {
      setUpdateStatus("Nazwa pliku nie pasuje do wybranego rodzaju aktualizacji.", true);
      return;
    }
    if (!file.size) {
      setUpdateStatus("Wybrany plik jest pusty.", true);
      return;
    }
    updateBusy = true;
    renderUpdateFiles();
    updateProgress.hidden = false;
    updateProgress.value = 0;
    setUpdateStatus("Wysyłanie " + file.name + "...");
    const body = new FormData();
    body.append("updatetarget", target);
    body.append("filesize", String(file.size));
    body.append("update", file, file.name);
    const request = new XMLHttpRequest();
    request.open("POST", "/update");
    request.upload.addEventListener("progress", event => {
      if (!event.lengthComputable) return;
      const percent = Math.min(100, Math.round(event.loaded * 100 / event.total));
      updateProgress.value = percent;
      setUpdateStatus("Wysyłanie " + file.name + ": " + percent + "%");
    });
    request.upload.addEventListener("load", () => {
      updateProgress.value = 100;
      setUpdateStatus("Walidacja obrazu i zapis aktualizacji...");
    });
    request.addEventListener("load", () => {
      if (request.status === 200 && request.responseText.trim() === "OK") {
        setUpdateStatus("Aktualizacja zakończona.");
        waitForUpdateRestart();
        return;
      }
      updateBusy = false;
      renderUpdateFiles();
      updateProgress.hidden = true;
      setUpdateStatus(request.responseText.trim() || "Błąd aktualizacji: HTTP " + request.status, true);
    });
    request.addEventListener("error", () => {
      setUpdateStatus("Połączenie zostało przerwane. Sprawdź urządzenie przed ponowną próbą; odśwież stronę, aby odblokować formularz.", true);
    });
    try {
      request.send(body);
    } catch (error) {
      updateBusy = false;
      renderUpdateFiles();
      updateProgress.hidden = true;
      setUpdateStatus("Nie udało się rozpocząć wysyłania: " + error.message, true);
    }
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
    if (selected === "settings" && !mqttConfigLoaded) loadMqttConfig();
    if (selected === "settings") loadTimeStatus();
    if (selected === "update") send("getwebstatus", 1);
  }

  function showStartupTab() {
    history.replaceState(null, "", "#status");
    showTab();
  }

  function setRtcFeedback(message, error = false) {
    rtcFeedback.textContent = message;
    rtcFeedback.dataset.kind = error ? "error" : "status";
  }

  async function loadTimeStatus() {
    if (rtcStatusLoading) return null;
    rtcStatusLoading = true;
    try {
      const response = await fetch("/api/time", { cache: "no-store" });
      if (!response.ok) throw new Error("HTTP " + response.status);
      const status = await response.json();
      rtcCard.hidden = status.rtcSupported !== true;
      if (rtcCard.hidden) return status;
      text("rtc-found", status.rtcFound ? "Wykryty" : "Brak");
      text("rtc-system-time", status.systemTime || "—");
      text("rtc-time", status.rtcTime || "—");
      rtcNtpInterval.value = String(status.timeSyncInterval);
      rtcWriteInterval.value = String(status.timeSyncIntervalRTC);
      rtcNtpInterval.disabled = false;
      rtcWriteInterval.disabled = false;
      rtcSyncNow.disabled = rtcSyncing;
      return status;
    } catch (error) {
      if (!rtcCard.hidden) setRtcFeedback("Nie udało się pobrać stanu RTC: " + (error.message || "błąd połączenia"), true);
      return null;
    } finally {
      rtcStatusLoading = false;
    }
  }

  async function saveTimeInterval(input, command, minimum, maximum) {
    const value = Number(input.value);
    if (!Number.isInteger(value) || value < minimum || value > maximum) {
      setRtcFeedback("Podaj liczbę z dozwolonego zakresu.", true);
      loadTimeStatus();
      return;
    }
    if (!send(command, value)) {
      setRtcFeedback("Brak połączenia z urządzeniem.", true);
      return;
    }
    setRtcFeedback("Oczekiwanie na potwierdzenie urządzenia…");
    await new Promise(resolve => setTimeout(resolve, 500));
    const status = await loadTimeStatus();
    if (status && status[command === "timeint" ? "timeSyncInterval" : "timeSyncIntervalRTC"] === value)
      setRtcFeedback("Ustawienie zapisane.");
    else setRtcFeedback("Nie potwierdzono zapisu. Sprawdź połączenie.", true);
  }

  async function syncTimeNow() {
    if (rtcSyncing) return;
    rtcSyncing = true;
    rtcSyncNow.disabled = true;
    setRtcFeedback("Oczekiwanie na synchronizację NTP…");
    try {
      const response = await fetch("/api/time/sync", { method: "POST", cache: "no-store" });
      const result = await response.json();
      if (!response.ok || result.ok !== true) throw new Error(result.error || ("HTTP " + response.status));
      for (let attempt = 0; attempt < 15; attempt++) {
        await new Promise(resolve => setTimeout(resolve, 2000));
        const status = await loadTimeStatus();
        if (status && status.syncCount > result.syncCount) {
          setRtcFeedback(status.rtcFound ? "Ręczna synchronizacja NTP zakończona; czas przekazano do RTC." : "Ręczna synchronizacja NTP zakończona; RTC nie został wykryty.");
          return;
        }
      }
      setRtcFeedback("Brak potwierdzenia synchronizacji NTP. Sprawdź połączenie i spróbuj ponownie.", true);
    } catch (error) {
      setRtcFeedback("Nie udało się rozpocząć synchronizacji: " + (error.message || "błąd połączenia"), true);
    } finally {
      rtcSyncing = false;
      rtcSyncNow.disabled = false;
      loadTimeStatus();
    }
  }

  function mqttRootMode() {
    const selected = mqttForm.querySelector('input[name="mqtt-root-mode"]:checked');
    return selected ? selected.value : "auto";
  }

  function renderMqttRoot() {
    const custom = mqttRootMode() === "custom";
    mqttCustomRootWrap.hidden = !custom;
    mqttRootModeLabel.textContent = custom ? "Własny" : "Automatyczny";
    mqttEffectiveRoot.textContent = mqttEffectiveRootValue || "—";
    mqttTopicList.textContent = mqttEffectiveRootValue ? ["command", "status", "volume", "playlist"].map(suffix => mqttEffectiveRootValue + "/" + suffix).join(" · ") : "—";
    renderHaYaml();
  }

  function renderHaYaml() {
    const name = haName.value.trim() || "VoxOne";
    const escapedName = name.replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/[\r\n]/g, " ");
    const root = mqttEffectiveRootValue || "—";
    haYaml.textContent = "media_player:\n  - platform: yoradio\n    name: \"" + escapedName + "\"\n    root_topic: " + root;
    haCopy.disabled = !mqttEffectiveRootValue;
  }

  function setMqttFeedback(message, isError) {
    mqttFeedback.textContent = message;
    mqttFeedback.dataset.kind = isError ? "error" : "success";
  }

  function updateMqttFormState() {
    mqttFields.classList.toggle("is-disabled", !mqttEnabled.checked);
    mqttPasswordState.textContent = mqttPasswordSet ? "Hasło zapisane. Puste pole zachowuje obecne hasło." : "";
    mqttSave.disabled = mqttSaving;
  }

  async function loadMqttConfig() {
    try {
      const response = await fetch("/api/mqtt", { cache: "no-store" });
      const config = await response.json();
      if (!response.ok) throw new Error(config.error || ("HTTP " + response.status));
      mqttEnabled.checked = config.enabled === true;
      mqttHost.value = typeof config.host === "string" ? config.host : "";
      mqttPort.value = String(config.port || 1883);
      mqttUsername.value = typeof config.username === "string" ? config.username : "";
      mqttPassword.value = "";
      mqttPasswordSet = config.passwordSet === true;
      mqttClearPassword.checked = false;
      const custom = typeof config.rootTopic === "string" && config.rootTopic.length > 0;
      mqttForm.querySelector('input[name="mqtt-root-mode"][value="' + (custom ? "custom" : "auto") + '"]').checked = true;
      mqttRootTopic.value = custom ? config.rootTopic : "";
      mqttEffectiveRootValue = typeof config.effectiveRoot === "string" ? config.effectiveRoot : "";
      mqttConfigLoaded = true;
      setMqttFeedback("", false);
      renderMqttRoot();
      updateMqttFormState();
    } catch (error) {
      setMqttFeedback("Nie udało się pobrać konfiguracji MQTT: " + (error.message || "błąd połączenia"), true);
    }
  }

  async function saveMqttConfig(event) {
    event.preventDefault();
    if (mqttSaving) return;
    const body = new URLSearchParams();
    body.set("enabled", mqttEnabled.checked ? "true" : "false");
    body.set("host", mqttHost.value);
    body.set("port", mqttPort.value);
    body.set("username", mqttUsername.value);
    body.set("rootTopic", mqttRootMode() === "custom" ? mqttRootTopic.value : "");
    if (mqttPassword.value) body.set("password", mqttPassword.value);
    else if (mqttClearPassword.checked) body.set("clearPassword", "true");
    mqttSaving = true;
    updateMqttFormState();
    setMqttFeedback("Zapisywanie konfiguracji…", false);
    try {
      const response = await fetch("/api/mqtt", { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" }, body: body.toString(), cache: "no-store" });
      const result = await response.json();
      if (!response.ok || result.ok !== true) throw new Error(result.error || ("HTTP " + response.status));
      mqttPassword.value = "";
      setMqttFeedback("Konfiguracja zapisana. VoxOne uruchomi się ponownie.", false);
    } catch (error) {
      setMqttFeedback("Nie zapisano konfiguracji MQTT: " + (error.message || "błąd połączenia"), true);
    } finally {
      mqttSaving = false;
      updateMqttFormState();
    }
  }

  async function copyHaYaml() {
    const value = haYaml.textContent;
    try {
      if (navigator.clipboard && window.isSecureContext) await navigator.clipboard.writeText(value);
      else {
        const temporary = document.createElement("textarea");
        temporary.value = value;
        temporary.setAttribute("readonly", "");
        temporary.style.position = "fixed";
        temporary.style.opacity = "0";
        document.body.appendChild(temporary);
        temporary.select();
        const copied = document.execCommand("copy");
        temporary.remove();
        if (!copied) throw new Error("clipboard unavailable");
      }
      haCopyStatus.textContent = "Skopiowano";
    } catch (_) {
      haCopyStatus.textContent = "Nie udało się skopiować";
    }
    setTimeout(() => { haCopyStatus.textContent = ""; }, 2200);
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
    const btDisconnected = state.webStatus?.source === "BT" && !state.webStatus.btConnected;
    buttons.prev.disabled = !connected || btDisconnected;
    buttons.next.disabled = !connected || btDisconnected;
    buttons.play.disabled = !connected || btDisconnected ||
      (state.webStatus ? !state.webStatus.playback : state.playing === null);
    for (const slider of volumeSliders) slider.disabled = !connected || state.volume100 === null;
    maximumVolumeSlider.disabled = !connected || state.maximumVolume === null;
    startupModeSelect.disabled = !connected || state.startupMode === null;
    startupFixedSlider.disabled = !connected || state.startupMode !== 1 || state.startupFixedVolume === null;
    brightnessSlider.disabled = !connected || !state.canBrightness || state.brightness === null;
    for (const [key, control] of Object.entries(audioControls)) {
      control.slider.disabled = !connected || state[key] === null;
    }
    for (const button of tonePresetButtons) {
      button.disabled = !connected || state.bass === null || state.middle === null || state.treble === null;
    }
    stationList.querySelectorAll(".station-play").forEach(button => {
      button.disabled = !connected;
    });
    renderStationActions();
    renderDisplaySettings();
  }

  function renderStation() {
    const status = state.webStatus;
    text("status-name-label", status?.source === "BT" ? "URZĄDZENIE" : "STACJA");
    text("current-station-name", (status ? status.name : state.station) || "—");
  }

  function renderMetadata() {
    const status = state.webStatus;
    const bt = status?.source === "BT";
    document.getElementById("metadata").hidden = bt;
    document.getElementById("metadata-hint").hidden = bt;
    document.getElementById("bt-artist").hidden = !bt || !status.btConnected;
    document.getElementById("bt-title").hidden = !bt || !status.btConnected;
    document.getElementById("bt-disconnected").hidden = !bt || status.btConnected;
    text("metadata", (status ? status.metadata : state.metadata) || "—");
    text("bt-artist", "Artysta: " + (bt && status.btConnected && status.artist ? status.artist : "—"));
    text("bt-title", "Utwór: " + (bt && status.btConnected && status.title ? status.title : "—"));
  }

  function renderStream() {
    const status = state.webStatus;
    if (status?.source === "BT") {
      const rate = status.btConnected ? status.sampleRate : 0;
      text("codec", rate > 0 ? (Number.isInteger(rate / 1000) ? rate / 1000 : (rate / 1000).toFixed(1)) + " kHz" : "—");
      return;
    }
    const parts = [];
    const codec = status ? status.codec : state.codec;
    const bitrate = status ? status.bitrate : state.bitrate;
    if (codec) parts.push(codec);
    if (bitrate > 0) parts.push(bitrate + " kb/s");
    text("codec", parts.length ? parts.join(" · ") : "—");
  }

  function renderSource() {
    text("source", (state.webStatus ? state.webStatus.source : state.source) || "—");
  }

  function renderRssi() {
    text("rssi", state.rssi === null ? "—" : state.rssi + " dBm");
  }

  function renderPlaying() {
    const status = state.webStatus;
    const playback = status ? status.playback : state.playing === null ? "" : state.playing ? "PLAY" : "STOP";
    const bt = status?.source === "BT";
    buttons.play.textContent = !playback ? "—" : playback === "PLAY" ? (bt ? "❚❚" : "■") : "▶";
    buttons.play.setAttribute("aria-label", !playback ? "Stan odtwarzania niedostępny" :
      playback === "PLAY" ? (bt ? "Pauza" : "Zatrzymaj") : "Odtwarzaj");
    text("playback-state", bt && !status.btConnected ? "Brak połączenia" : playback || "—");
    renderConnection();
  }

  function renderStatus() {
    renderStation();
    renderMetadata();
    renderStream();
    renderSource();
    renderPlaying();
  }

  function showVolume(value) {
    text("volume", value === null ? "—" : String(value));
    text("audio-volume", value === null ? "—" : String(value));
    if (value !== null) {
      for (const slider of volumeSliders) {
        if (draggingVolume && slider === activeVolumeSlider) continue;
        slider.value = String(value);
      }
    }
    renderConnection();
  }

  function renderVolume() {
    showVolume(state.volume100);
  }

  function renderVolumeSettings() {
    const maxShown = volumeSettingEditing.maximumVolume ?? state.maximumVolume;
    if (!volumeSettingDragging.maximumVolume && maxShown !== null) maximumVolumeSlider.value = String(maxShown);
    maximumVolumeValue.textContent = maxShown === null ? "—" : String(maxShown);
    if (state.startupMode !== null && volumeSettingEditing.startupMode === null) {
      startupModeSelect.value = String(state.startupMode);
    }
    startupFixedControl.hidden = state.startupMode !== 1;
    const fixedShown = volumeSettingEditing.startupFixedVolume ?? state.startupFixedVolume;
    if (!volumeSettingDragging.startupFixedVolume && fixedShown !== null) startupFixedSlider.value = String(fixedShown);
    startupFixedValue.textContent = fixedShown === null ? "—" : String(fixedShown);
    const pending = Object.values(volumeSettingEditing).some(value => value !== null);
    volumeSettingsPending.hidden = !pending;
    renderConnection();
  }

  function renderDisplaySettings() {
    displaySettingsCard.hidden = !state.canFlip && !state.canVu && !state.canBrightness;
    flipScreenControl.hidden = !state.canFlip;
    flipScreenToggle.disabled = state.connection !== "connected" ||
      !state.canFlip || state.flip === null || flipPending;
    if (!flipPending && state.flip !== null) flipScreenToggle.checked = state.flip;
    vuMeterControl.hidden = !state.canVu;
    vuMeterToggle.disabled = state.connection !== "connected" ||
      !state.canVu || state.vu === null || vuAwaiting !== null;
    if (vuAwaiting === null && state.vu !== null) vuMeterToggle.checked = state.vu;
    vuMeterValue.textContent = vuAwaiting !== null ? "Zapisywanie…" :
      state.vu === null ? "—" : state.vu ? "ON" : "OFF";
    brightnessControl.hidden = !state.canBrightness;
    brightnessSlider.disabled = state.connection !== "connected" ||
      !state.canBrightness || state.brightness === null;
    const brightnessShown = brightnessDragging && brightnessPending !== null
      ? brightnessPending : brightnessAwaiting !== null ? brightnessAwaiting : state.brightness;
    if (!brightnessDragging && brightnessAwaiting === null && state.brightness !== null) {
      brightnessSlider.value = String(state.brightness);
    } else if (brightnessDragging && brightnessPending !== null) {
      brightnessSlider.value = String(brightnessPending);
    }
    brightnessValue.textContent = brightnessShown === null ? "—" : brightnessShown + "%";
    autoReturnControl.hidden = !state.canFlip;
    stationListTimeoutInput.disabled = state.connection !== "connected" ||
      !state.canFlip || state.stationListTimeout === null;
    btTimeoutControl.hidden = !state.canBtTransport;
    btTransportTimeoutInput.disabled = state.connection !== "connected" ||
      !state.canBtTransport || state.btTransportTimeout === null;
    if (document.activeElement !== stationListTimeoutInput && state.stationListTimeout !== null)
      stationListTimeoutInput.value = String(state.stationListTimeout);
    if (document.activeElement !== btTransportTimeoutInput && state.btTransportTimeout !== null)
      btTransportTimeoutInput.value = String(state.btTransportTimeout);
    flipScreenFeedback.hidden = !state.canFlip;
    flipScreenFeedback.textContent = flipPending
      ? "Oczekiwanie na potwierdzenie urządzenia…"
      : "Zmiana działa od razu, bez restartu.";
  }

  function confirmVolumeSetting(key, value) {
    state[key] = value;
    if (volumeSettingEditing[key] !== null) {
      clearTimeout(volumeSettingTimers[key]);
      volumeSettingEditing[key] = null;
    }
    renderVolumeSettings();
  }

  function sendVolumeSetting(key, command, value) {
    if (!send(command, value)) {
      volumeSettingEditing[key] = null;
      renderVolumeSettings();
      return;
    }
    volumeSettingEditing[key] = value;
    clearTimeout(volumeSettingTimers[key]);
    volumeSettingTimers[key] = setTimeout(() => {
      volumeSettingEditing[key] = null;
      renderVolumeSettings();
    }, 2000);
    renderVolumeSettings();
  }

  function signedValue(value) {
    if (value === null) return "—";
    return (value > 0 ? "+" : "") + value;
  }

  function renderAudio() {
    text("audio-bass", signedValue(state.bass));
    text("audio-middle", signedValue(state.middle));
    text("audio-treble", signedValue(state.treble));
    text("audio-balance", signedValue(state.balance));
    text("audio-balance-note", state.balance === null ? "" : state.balance < 0 ? "lewo" : state.balance > 0 ? "prawo" : "środek");
    for (const [key, control] of Object.entries(audioControls)) {
      if (state[key] !== null && !control.dragging) control.slider.value = String(state[key]);
    }
    const hasTone = state.bass !== null && state.middle !== null && state.treble !== null;
    const activePreset = hasTone ? Object.keys(tonePresets).find(name => {
      const [bass, middle, treble] = tonePresets[name];
      return state.bass === bass && state.middle === middle && state.treble === treble;
    }) : null;
    for (const button of tonePresetButtons) {
      button.setAttribute("aria-pressed", String(button.dataset.tonePreset === activePreset));
    }
    tonePresetUser.classList.toggle("is-active", hasTone && !activePreset);
    renderConnection();
  }

  function renderIdentity() {
    const version = state.version || "—";
    const profile = state.profile ? state.profile.toUpperCase() : "—";
    text("header-version", "VoxOne " + version);
    text("header-profile", "Profil " + profile);
    text("system-version", "VoxOne " + version);
    text("system-profile", profile);
    text("update-version", "VoxOne " + version);
    text("update-profile", profile);
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
        (() => {
          const toggle = stationAction("metadata", "A↔T", "Zamień artystę i utwór: " + station.name, station.number);
          toggle.title = "Zamień artystę i utwór";
          toggle.classList.toggle("is-on", station.swapArtistTitle);
          toggle.setAttribute("aria-pressed", String(station.swapArtistTitle));
          if (state.pendingMetadataNumber === station.number) toggle.textContent = "…";
          return toggle;
        })(),
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
            typeof station.id === "string" && /^[0-9A-F]{16}$/.test(station.id) &&
            typeof station.url === "string" && Number.isInteger(station.ovol) &&
            (station.metadataMode === "normal" || station.metadataMode === "swap") &&
            typeof station.swapArtistTitle === "boolean" &&
            station.swapArtistTitle === (station.metadataMode === "swap"))) {
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

  function showStationView(directory) {
    myStationsView.hidden = directory;
    directoryView.hidden = !directory;
    myStationsTab.setAttribute("aria-selected", String(!directory));
    directoryTab.setAttribute("aria-selected", String(directory));
    if (directory) directoryQuery.focus();
  }

  function directoryMessage(message, error = false) {
    directoryStatus.textContent = message;
    directoryStatus.dataset.kind = error ? "error" : "notice";
  }

  function directoryError(status, code, phase) {
    if (code === "offline") return "VoxOne nie ma połączenia z internetem.";
    if (code === "insufficient_memory") return "Brak pamięci na wyszukiwanie. Spróbuj później.";
    if (code === "upstream_response_too_large") return "Katalog zwrócił zbyt dużą odpowiedź.";
    if (code === "upstream_invalid_json") return "Katalog zwrócił nieprawidłowe dane.";
    if (status === 504) return "Upłynął czas oczekiwania na katalog.";
    if (status === 429) return "Trwa inne wyszukiwanie. Spróbuj za chwilę.";
    if (status === 400 && phase === "start") return "Sprawdź nazwę stacji i kraj.";
    if (code === "bad_job") return "Nieprawidłowy identyfikator wyszukiwania.";
    if (status === 404 && phase === "poll") return "Wynik wyszukiwania nie jest już dostępny.";
    return "Nie udało się pobrać katalogu stacji.";
  }

  function renderDirectoryResults() {
    const fragment = document.createDocumentFragment();
    directoryStations.forEach((station, index) => {
      const row = document.createElement("li");
      row.className = "directory-result";
      const details = document.createElement("div");
      details.className = "directory-result-main";
      const name = document.createElement("strong");
      name.textContent = station.name;
      const meta = document.createElement("span");
      const country = station.countryCode === "PL" ? "Polska" :
        (station.country || station.countryCode || "Kraj nieznany");
      const fields = [country];
      if (station.codec) fields.push(station.codec);
      fields.push(station.bitrate > 0 ? station.bitrate + " kbps" : "bitrate nieznany");
      meta.textContent = fields.join(" · ");
      details.append(name, meta);
      const add = document.createElement("button");
      add.type = "button";
      add.textContent = "Dodaj";
      add.dataset.directoryIndex = String(index);
      row.append(details, add);
      fragment.append(row);
    });
    directoryResults.replaceChildren(fragment);
  }

  async function searchDirectory() {
    if (directoryBusy) return;
    const query = directoryQuery.value.trim();
    if (!query || utf8Length(query) > 80) {
      directoryMessage("Podaj nazwę stacji (najwyżej 80 bajtów).", true);
      return;
    }
    directoryBusy = true;
    directorySearchButton.disabled = true;
    directoryResults.replaceChildren();
    directoryStations = [];
    directoryMessage("Wyszukiwanie stacji...");
    let phase = "start";
    try {
      const parameters = new URLSearchParams({
        q: query, country: directoryCountry.value, limit: "20"
      });
      let response = await fetch("/api/directory/search?" + parameters, { cache: "no-store" });
      let payload = await response.json().catch(() => null);
      if (response.status === 202) {
        if (!payload || !Number.isSafeInteger(payload.job) || payload.job <= 0)
          throw { status: 502, code: "invalid_job_response" };
        const jobId = payload.job;
        phase = "poll";
        let done = false;
        for (let attempt = 0; attempt < 80; attempt++) {
          await new Promise(resolve => setTimeout(resolve, 250));
          response = await fetch("/api/directory/search?job=" + jobId, { cache: "no-store" });
          payload = await response.json().catch(() => null);
          if (response.status === 202) {
            if (!payload || payload.status !== "pending")
              throw { status: 502, code: "invalid_pending_response" };
            continue;
          }
          done = true;
          break;
        }
        if (!done) throw { status: 504 };
      }
      if (!response.ok) throw { status: response.status, code: payload && payload.error };
      if (!payload || !Array.isArray(payload.results) || payload.results.length > 20 ||
          !payload.results.every(station => station && typeof station.name === "string" &&
            typeof station.url === "string" && typeof station.country === "string" &&
            typeof station.countryCode === "string" && typeof station.codec === "string" &&
            Number.isInteger(station.bitrate))) throw { status: 502 };
      directoryStations = payload.results;
      renderDirectoryResults();
      directoryMessage(directoryStations.length ?
        "Znaleziono " + directoryStations.length + " stacji." : "Nie znaleziono stacji.");
    } catch (error) {
      console.warn("VoxOne: directory search failed", error);
      directoryMessage(directoryError(error.status, error.code, phase), true);
    } finally {
      directoryBusy = false;
      directorySearchButton.disabled = false;
    }
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
        !state.revision) return false;
    state.mutationInProgress = true;
    state.pendingMetadataNumber = route === "metadata" ?
      (state.stations.find(station => station.id === fields.id)?.number ?? null) : null;
    setStationFeedback("");
    if (state.pendingMetadataNumber !== null) renderStationList();
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
        return false;
      }
      if (!response.ok) throw { status: response.status, code: result && result.error };
      if (!result || result.ok !== true) throw { status: 500 };
      if (stationDialog.open) stationDialog.close();
      state.editorNumber = null;
      const refreshed = await loadStations();
      setStationFeedback(refreshed ? successMessage :
        "Zmiana została zapisana, ale nie udało się odświeżyć listy. Użyj przycisku Ponów.", !refreshed);
      return true;
    } catch (error) {
      console.warn("VoxOne: station mutation failed", error);
      setStationFeedback(mutationErrorMessage(error.status), true);
      return false;
    } finally {
      state.pendingMetadataNumber = null;
      state.mutationInProgress = false;
      if (state.stationsStatus === "ready") renderStationList();
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
    if (file.size > 192 * 1024) {
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
      body.append("file", file, file.name);
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
    for (const key of ["source", "station", "metadata", "codec", "bitrate", "rssi", "volume", "volume100", "maximumVolume", "startupMode", "startupFixedVolume", "brightness", "stationListTimeout", "btTransportTimeout", "bass", "middle", "treble", "balance", "playing", "current", "ip"]) {
      state[key] = null;
    }
    state.webStatus = null;
    renderBtModule();
    state.canBrightness = false;
    state.canBtTransport = false;
    state.flip = null;
    state.canFlip = false;
    state.vu = null;
    state.canVu = false;
    flipPending = false;
    clearTimeout(flipTimer);
    vuAwaiting = null;
    clearTimeout(vuTimer);
    clearTimeout(brightnessTimer);
    clearTimeout(brightnessAckTimer);
    brightnessDragging = false;
    brightnessPending = null;
    brightnessAwaiting = null;
    for (const key of Object.keys(volumeSettingEditing)) {
      volumeSettingEditing[key] = null;
      clearTimeout(volumeSettingTimers[key]);
    }
    volumeSettingDragging.maximumVolume = false;
    volumeSettingDragging.startupFixedVolume = false;
    draggingVolume = false;
    activeVolumeSlider = null;
    awaitingVolume = null;
    pendingVolume = null;
    clearTimeout(volumeTimer);
    clearTimeout(volumeAckTimer);
    renderStatus();
    renderRssi();
    showVolume(null);
    renderVolumeSettings();
    renderDisplaySettings();
    for (const control of Object.values(audioControls)) {
      control.dragging = false;
      clearTimeout(control.ackTimer);
    }
    renderAudio();
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
        break;
      }
      case "volume100": {
        const volume = Number(value);
        if (!Number.isInteger(volume) || volume < 0 || volume > 100) break;
        state.volume100 = volume;
        if (awaitingVolume === volume) {
          awaitingVolume = null;
          clearTimeout(volumeAckTimer);
        }
        renderVolume();
        break;
      }
      case "maximumVolume": {
        const maximum = Number(value);
        if (!Number.isInteger(maximum) || maximum < 1 || maximum > 100) break;
        confirmVolumeSetting("maximumVolume", maximum);
        break;
      }
      case "startupMode": {
        const mode = Number(value);
        if (!Number.isInteger(mode) || (mode !== 0 && mode !== 1)) break;
        confirmVolumeSetting("startupMode", mode);
        break;
      }
      case "startupFixedVolume": {
        const volume = Number(value);
        if (!Number.isInteger(volume) || volume < 0 || volume > 100) break;
        confirmVolumeSetting("startupFixedVolume", volume);
        break;
      }
      case "bass":
      case "middle":
      case "trebble":
      case "balance": {
        const setting = Number(value);
        if (!Number.isInteger(setting)) break;
        const key = id === "trebble" ? "treble" : id;
        state[key] = setting;
        clearTimeout(audioControls[key].ackTimer);
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
    const status = message.webStatus;
    if (status && (status.source === "WEB" || status.source === "BT") &&
        ["name", "metadata", "artist", "title", "codec", "playback"].every(key => typeof status[key] === "string") &&
        Number.isInteger(status.bitrate) && Number.isInteger(status.sampleRate) &&
        typeof status.btConnected === "boolean") {
      state.webStatus = status;
      renderStatus();
      renderBtModule();
    }
    if (Array.isArray(message.payload)) {
      let toneChanged = false;
      for (const item of message.payload) {
        updatePayload(item.id, item.value);
        if (["bass", "middle", "trebble", "balance"].includes(item.id)) toneChanged = true;
      }
      if (toneChanged) renderAudio();
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
    if (message.canFlip === 0 || message.canFlip === 1) {
      state.canFlip = message.canFlip === 1;
    }
    if (message.canBrightness === 0 || message.canBrightness === 1) {
      state.canBrightness = message.canBrightness === 1;
    }
    if (message.canBtTransport === 0 || message.canBtTransport === 1)
      state.canBtTransport = message.canBtTransport === 1;
    if (Number.isInteger(message.stationListTimeout) && message.stationListTimeout >= 0 && message.stationListTimeout <= 120)
      state.stationListTimeout = message.stationListTimeout;
    if (Number.isInteger(message.btTransportTimeout) && message.btTransportTimeout >= 0 && message.btTransportTimeout <= 120)
      state.btTransportTimeout = message.btTransportTimeout;
    if (Number.isInteger(message.br) && message.br >= 0 && message.br <= 100) {
      state.brightness = message.br;
      if (brightnessAwaiting === message.br) {
        brightnessAwaiting = null;
        clearTimeout(brightnessAckTimer);
      }
    }
    if (message.flip === 0 || message.flip === 1) {
      state.flip = message.flip === 1;
      flipPending = false;
      clearTimeout(flipTimer);
    }
    if (message.canVu === 0 || message.canVu === 1) state.canVu = message.canVu === 1;
    if (message.vu === 0 || message.vu === 1) {
      state.vu = message.vu === 1;
      if (vuAwaiting === state.vu) {
        vuAwaiting = null;
        clearTimeout(vuTimer);
      }
    }
    if (message.canFlip === 0 || message.canFlip === 1 ||
        message.flip === 0 || message.flip === 1 ||
        message.canBrightness === 0 || message.canBrightness === 1 ||
        Number.isInteger(message.br) ||
        message.canVu === 0 || message.canVu === 1 ||
        message.vu === 0 || message.vu === 1 ||
        Number.isInteger(message.stationListTimeout) ||
        Number.isInteger(message.btTransportTimeout) ||
        message.canBtTransport === 0 || message.canBtTransport === 1) renderDisplaySettings();
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
      send("getwebstatus", 1);
      send("getindex", 1);
      send("getscreen", 1);
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
      renderBtModule();
      for (const control of Object.values(audioControls)) {
        control.dragging = false;
        clearTimeout(control.ackTimer);
      }
      renderAudio();
      renderDisplaySettings();
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
    if (send("webtransport", "prev")) flashButton(buttons.prev);
  });
  buttons.play.addEventListener("click", () => {
    if (send("webtransport", "toggle")) flashButton(buttons.play);
  });
  buttons.next.addEventListener("click", () => {
    if (send("webtransport", "next")) flashButton(buttons.next);
  });

  myStationsTab.addEventListener("click", () => showStationView(false));
  directoryTab.addEventListener("click", () => showStationView(true));
  directoryForm.addEventListener("submit", event => {
    event.preventDefault();
    searchDirectory();
  });
  directoryResults.addEventListener("click", async event => {
    const button = event.target.closest("button[data-directory-index]");
    if (!button || button.disabled || state.mutationInProgress) return;
    const station = directoryStations[Number(button.dataset.directoryIndex)];
    if (!station) return;
    button.disabled = true;
    button.textContent = "Dodawanie...";
    if (state.stationsStatus !== "ready" && !await loadStations()) {
      directoryMessage("Nie udało się pobrać listy własnych stacji.", true);
      button.disabled = false;
      button.textContent = "Dodaj";
      return;
    }
    const added = await mutateStation("add",
      { name: station.name, url: station.url, ovol: "0" }, "Dodano stację.");
    if (added) {
      button.textContent = "Dodano";
      directoryMessage("Dodano „" + station.name + "” do Moje stacje.");
    } else {
      button.disabled = false;
      button.textContent = "Dodaj";
      directoryMessage("Nie udało się dodać stacji. Sprawdź komunikat w Moje stacje.", true);
    }
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
    if (file.size === 0) {
      stationImportFile.value = "";
      setStationFeedback("Plik VoxOne Stations jest pusty.", true);
      return;
    }
    if (file.size > 192 * 1024) {
      stationImportFile.value = "";
      setStationFeedback(importErrorMessage(413), true);
      return;
    }
    stationImportDetails.textContent = file.name + " · " + file.size + " B";
    stationImportWarning.textContent =
      "Import zastąpi obecną listę stacji. Aktualna lista zostanie zachowana bezpiecznie do czasu zakończenia zapisu.";
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
      mutateStation("edit", { id: state.stations[state.editorNumber - 1].id, ...fields }, "Zapisano stację.");
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
      case "metadata":
        mutateStation("metadata", { id: station.id, swapArtistTitle: station.swapArtistTitle ? "0" : "1" },
          station.swapArtistTitle ? "Wyłączono zamianę artysty i utworu." : "Włączono zamianę artysty i utworu.");
        break;
      case "edit":
        openStationForm(number);
        break;
      case "delete": {
        const activeNote = number === state.current ? "\nTo jest aktualnie odtwarzana stacja." : "";
        if (window.confirm("Usunąć stację „" + station.name + "”?" + activeNote)) {
          mutateStation("delete", { id: station.id }, "Usunięto stację.");
        }
        break;
      }
      case "up":
      case "down": {
        const target = number + (button.dataset.action === "up" ? -1 : 1);
        if (target >= 1 && target <= state.count) {
          mutateStation("reorder", { id: station.id, targetPosition: String(target) }, "Zmieniono kolejność stacji.");
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
    if (send("vol100", pendingVolume)) lastVolumeSentAt = performance.now();
    pendingVolume = null;
  }

  for (const slider of volumeSliders) {
    slider.addEventListener("input", () => {
      if (state.volume100 === null || state.connection !== "connected") return;
      draggingVolume = true;
      activeVolumeSlider = slider;
      pendingVolume = Number(slider.value);
      const wait = 120 - (performance.now() - lastVolumeSentAt);
      if (wait <= 0) sendPendingVolume();
      else {
        clearTimeout(volumeTimer);
        volumeTimer = setTimeout(sendPendingVolume, wait);
      }
    });

    slider.addEventListener("change", () => {
      if (!draggingVolume || activeVolumeSlider !== slider) return;
      const finalVolume = Number(slider.value);
      draggingVolume = false;
      activeVolumeSlider = null;
      pendingVolume = finalVolume;
      awaitingVolume = finalVolume;
      sendPendingVolume();
      clearTimeout(volumeAckTimer);
      volumeAckTimer = setTimeout(() => {
        awaitingVolume = null;
        renderVolume();
      }, 1500);
    });
  }

  maximumVolumeSlider.addEventListener("input", () => {
    if (maximumVolumeSlider.disabled) return;
    volumeSettingDragging.maximumVolume = true;
    maximumVolumeValue.textContent = maximumVolumeSlider.value;
  });
  maximumVolumeSlider.addEventListener("change", () => {
    if (maximumVolumeSlider.disabled) return;
    const value = Number(maximumVolumeSlider.value);
    volumeSettingDragging.maximumVolume = false;
    if (!Number.isInteger(value) || value < 1 || value > 100) {
      renderVolumeSettings();
      return;
    }
    sendVolumeSetting("maximumVolume", "maximumvolume", value);
  });

  flipScreenToggle.addEventListener("change", () => {
    if (!state.canFlip || state.flip === null ||
        !send("flipscreen", flipScreenToggle.checked ? 1 : 0)) {
      renderDisplaySettings();
      return;
    }
    flipPending = true;
    clearTimeout(flipTimer);
    flipTimer = setTimeout(() => {
      flipPending = false;
      renderDisplaySettings();
      send("getscreen", 1);
    }, 2000);
    renderDisplaySettings();
  });

  vuMeterToggle.addEventListener("change", () => {
    const requested = vuMeterToggle.checked;
    if (!state.canVu || state.vu === null || !send("vumeter", requested ? 1 : 0)) {
      renderDisplaySettings();
      return;
    }
    vuAwaiting = requested;
    clearTimeout(vuTimer);
    vuTimer = setTimeout(() => {
      vuAwaiting = null;
      renderDisplaySettings();
      send("getsystem", 1);
    }, 2000);
    renderDisplaySettings();
  });

  function sendPendingBrightness() {
    clearTimeout(brightnessTimer);
    if (brightnessPending === null) return false;
    const value = brightnessPending;
    brightnessPending = null;
    if (!send("dim", value)) return false;
    lastBrightnessSentAt = performance.now();
    return true;
  }

  brightnessSlider.addEventListener("input", () => {
    if (!state.canBrightness || state.connection !== "connected") return;
    brightnessDragging = true;
    brightnessPending = Number(brightnessSlider.value);
    brightnessValue.textContent = brightnessPending + "%";
    const wait = 120 - (performance.now() - lastBrightnessSentAt);
    if (wait <= 0) sendPendingBrightness();
    else {
      clearTimeout(brightnessTimer);
      brightnessTimer = setTimeout(sendPendingBrightness, wait);
    }
  });

  brightnessSlider.addEventListener("change", () => {
    if (!state.canBrightness || state.connection !== "connected") return;
    brightnessDragging = false;
    brightnessPending = Number(brightnessSlider.value);
    brightnessAwaiting = brightnessPending;
    if (!sendPendingBrightness()) {
      brightnessAwaiting = null;
      renderDisplaySettings();
      return;
    }
    clearTimeout(brightnessAckTimer);
    brightnessAckTimer = setTimeout(() => {
      brightnessAwaiting = null;
      renderDisplaySettings();
      send("getscreen", 1);
    }, 2000);
    renderDisplaySettings();
  });

  function saveIdleTimeout(input, key, command) {
    const value = Number(input.value);
    if (input.value.trim() === "" || !Number.isInteger(value) || value < 0 || value > 120 ||
        !send(command, value)) {
      input.value = state[key] === null ? "" : String(state[key]);
      return;
    }
    input.blur();
    setTimeout(() => send("getscreen", 1), 500);
  }

  stationListTimeoutInput.addEventListener("change", () =>
    saveIdleTimeout(stationListTimeoutInput, "stationListTimeout", "stationlisttimeout"));
  btTransportTimeoutInput.addEventListener("change", () =>
    saveIdleTimeout(btTransportTimeoutInput, "btTransportTimeout", "bttransporttimeout"));

  rtcNtpInterval.addEventListener("change", () => saveTimeInterval(rtcNtpInterval, "timeint", 1, 10080));
  rtcWriteInterval.addEventListener("change", () => saveTimeInterval(rtcWriteInterval, "timeintrtc", 1, 1000));
  rtcSyncNow.addEventListener("click", syncTimeNow);

  startupModeSelect.addEventListener("change", () => {
    if (startupModeSelect.disabled) return;
    const mode = Number(startupModeSelect.value);
    if (mode !== 0 && mode !== 1) {
      renderVolumeSettings();
      return;
    }
    sendVolumeSetting("startupMode", "startupmode", mode);
  });

  startupFixedSlider.addEventListener("input", () => {
    if (startupFixedSlider.disabled) return;
    volumeSettingDragging.startupFixedVolume = true;
    startupFixedValue.textContent = startupFixedSlider.value;
  });
  startupFixedSlider.addEventListener("change", () => {
    if (startupFixedSlider.disabled) return;
    const value = Number(startupFixedSlider.value);
    volumeSettingDragging.startupFixedVolume = false;
    if (!Number.isInteger(value) || value < 0 || value > 100) {
      renderVolumeSettings();
      return;
    }
    sendVolumeSetting("startupFixedVolume", "startupfixedvolume", value);
  });

  for (const control of Object.values(audioControls)) {
    control.dragging = false;
    control.slider.addEventListener("input", () => {
      if (!control.slider.disabled) control.dragging = true;
    });
    control.slider.addEventListener("change", () => {
      if (control.slider.disabled) return;
      const value = Number(control.slider.value);
      control.dragging = false;
      if (!Number.isInteger(value) || value < Number(control.slider.min) ||
          value > Number(control.slider.max) || !send(control.command, value)) {
        renderAudio();
      } else {
        clearTimeout(control.ackTimer);
        control.ackTimer = setTimeout(renderAudio, 1500);
      }
    });
  }

  for (const button of tonePresetButtons) {
    button.addEventListener("click", () => {
      if (button.disabled) return;
      const values = tonePresets[button.dataset.tonePreset];
      if (values) send("tone", values.join(","));
    });
  }

  window.addEventListener("hashchange", showTab);
  window.addEventListener("pageshow", event => {
    if (event.persisted) showStartupTab();
  });
  for (const [target, image] of Object.entries(updateImages)) {
    image.file.addEventListener("change", renderUpdateFiles);
    image.button.addEventListener("click", () => uploadUpdateImage(target));
  }
  updateBtFile.addEventListener("change", renderUpdateFiles);
  rebootButton.addEventListener("click", () => {
    if (!rebootStarted) rebootDialog.showModal();
  });
  rebootCancel.addEventListener("click", () => rebootDialog.close());
  rebootConfirm.addEventListener("click", rebootDevice);
  mqttForm.addEventListener("submit", saveMqttConfig);
  mqttEnabled.addEventListener("change", updateMqttFormState);
  mqttPassword.addEventListener("input", () => {
    if (mqttPassword.value) mqttClearPassword.checked = false;
  });
  mqttClearPassword.addEventListener("change", () => {
    if (mqttClearPassword.checked) mqttPassword.value = "";
  });
  mqttForm.querySelectorAll('input[name="mqtt-root-mode"]').forEach(input => input.addEventListener("change", renderMqttRoot));
  haName.addEventListener("input", renderHaYaml);
  haCopy.addEventListener("click", copyHaYaml);
  window.addEventListener("online", () => {
    if (!socket) connect();
  });
  window.addEventListener("beforeunload", () => {
    leaving = true;
    clearTimeout(reconnectTimer);
    if (socket) socket.close();
  });
  renderIdentity();
  renderUpdateFiles();
  renderConnection();
  showStartupTab();
  connect();
})();
