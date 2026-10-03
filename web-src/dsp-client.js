/* DSP-WEB-LIVE-1: one firmware snapshot and the existing VoxOne WebSocket. */
(() => {
  "use strict";

  const PARAM = Object.freeze({
    masterTrim: 1, processing: 2, mode: 3, crossover: 4, hpf: 5, routing: 6,
    outTrim: 7, outDelay: 8, outPolarity: 9, outMute: 10,
    loudness: 11, loudnessIntensity: 12, limiter: 13, limiterThreshold: 14,
    limiterAttack: 15, limiterRelease: 16, mute: 17,
    peqEnabled: 18, peqFrequency: 19, peqGain: 20, peqQ: 21,
    dynamics: 22, threshold: 23, ratio: 24, attack: 25, release: 26,
    makeup: 27, subTrim: 28, subDelay: 29, subPolarity: 30, subMute: 31
  });
  const ROLES = ["LEFT", "RIGHT", "SUB", "SUB", "SUB-L", "SUB-R", "OFF"];
  const MODES = ["2.0", "2.1", "2.2"];
  const ERROR_TEXT = {
    1: "DSP nie jest gotowy.", 2: "Wartość poza zakresem.", 3: "Nieprawidłowy indeks.",
    4: "Nieprawidłowy preset.", 5: "Preset fabryczny jest tylko do odczytu.",
    6: "Brak aktywnego subwoofera.", 7: "Nieprawidłowa nazwa presetu.",
    8: "Stan presetu zmienił się. Odświeżono dane.", 9: "Backend DSP nie przyjął zmiany.",
    100: "Nieprawidłowe żądanie.", 101: "Żądanie jest za długie.",
    102: "Nieznany parametr DSP.", 103: "Nieprawidłowa nazwa presetu.",
    104: "DSP jest zajęty. Spróbuj ponownie.",
    105: "Nie udało się zapisać tonu presetu.", 106: "Backend DSP niedostępny."
  };
  const fmt = value => Number.isInteger(Number(value)) ? String(value) :
    Number(value).toFixed(2).replace(/0+$/, "").replace(/\.$/, "");
  const keyOf = (id, index) => `${id}:${index}`;
  const validState = state => state && typeof state === "object" &&
    state.capabilities && state.sharedAudio && state.global && state.working &&
    Array.isArray(state.global.outputs) && Array.isArray(state.working.peq) &&
    state.working.dynamics && Array.isArray(state.presets) && state.runtime &&
    state.computed && Array.isArray(state.computed.outputs) && state.computed.sub &&
    Number.isInteger(state.capabilities.peqBands) &&
    state.capabilities.peqBands <= state.working.peq.length &&
    Number.isInteger(state.capabilities.outputCount) &&
    state.capabilities.outputCount <= state.global.outputs.length &&
    state.capabilities.outputCount <= state.computed.outputs.length &&
    Number.isFinite(state.capabilities.sampleRateHz) && state.capabilities.sampleRateHz > 0 &&
    Number.isFinite(state.capabilities.maxDelayMs) &&
    Number.isInteger(state.runtime.revision) && Number.isInteger(state.runtime.appliedRevision);

  function patch(state, id, index, value) {
    const global = state.global, working = state.working;
    const output = global.outputs[index], band = working.peq[index];
    switch (id) {
      case 1: global.masterTrimDb = value; break;
      case 2: global.processingEnabled = Boolean(value); break;
      case 3: global.outputMode = value; break;
      case 4: global.crossoverHz = value; break;
      case 5: global.hpfMains = Boolean(value); break;
      case 6: global.subRouting = value; break;
      case 7: if (output) output.trimDb = value; break;
      case 8: if (output) output.delayMs = value; break;
      case 9: if (output) output.polarity = value; break;
      case 10: if (output) output.userMute = Boolean(value); break;
      case 11: global.loudness.enabled = Boolean(value); break;
      case 12: global.loudness.intensity = value; break;
      case 13: global.limiter.enabled = Boolean(value); break;
      case 14: global.limiter.thresholdDbfs = value; break;
      case 15: global.limiter.attackMs = value; break;
      case 16: global.limiter.releaseMs = value; break;
      case 17: state.runtime.masterMute = Boolean(value); break;
      case 18: if (band) band.enabled = Boolean(value); break;
      case 19: if (band) band.frequencyHz = value; break;
      case 20: if (band) band.gainDb = value; break;
      case 21: if (band) band.q = value; break;
      case 22: working.dynamics.enabled = Boolean(value); break;
      case 23: working.dynamics.thresholdDbfs = value; break;
      case 24: working.dynamics.ratio = value; break;
      case 25: working.dynamics.attackMs = value; break;
      case 26: working.dynamics.releaseMs = value; break;
      case 27: working.dynamics.makeupGainDb = value; break;
      case 28: case 29: case 30: case 31: {
        const field = {28: "trimDb", 29: "delayMs", 30: "polarity", 31: "userMute"}[id];
        const count = global.outputMode === 1 ? 3 : global.outputMode === 2 ? 4 : 2;
        for (let i = 2; i < count; i++) global.outputs[i][field] = id === 31 ? Boolean(value) : value;
        if (state.computed.sub) {
          const name = {28: "trim", 29: "delay", 30: "polarity", 31: "mute"}[id];
          state.computed.sub[name] = {active: count > 2, mixed: false, value: id === 31 ? Boolean(value) : value};
        }
        break;
      }
    }
  }

  class DspClient {
    constructor(bridge, changed) {
      this.bridge = bridge;
      this.changed = changed;
      this.state = null;
      this.optimistic = new Map();
      this.pending = new Map();
      this.timers = new Map();
      this.lastSent = new Map();
      this.requestId = 0;
      this.generation = 0;
      this.active = false;
      this.error = "";
      this.loading = false;
      this.resyncTimer = 0;
    }
    clearPending() {
      for (const timer of this.timers.values()) clearTimeout(timer);
      for (const item of this.pending.values()) clearTimeout(item.timeout);
      this.timers.clear(); this.pending.clear(); this.optimistic.clear(); this.lastSent.clear();
    }
    async loadState(preserveError = false) {
      const generation = ++this.generation;
      this.loading = true;
      this.changed();
      try {
        const response = await fetch("/api/dsp/state", {cache: "no-store", credentials: "same-origin"});
        if (!response.ok) throw new Error("HTTP " + response.status);
        const state = await response.json();
        if (!validState(state)) throw new Error("Invalid DSP state");
        if (generation !== this.generation) return;
        this.clearPending();
        this.state = state;
        if (!preserveError) this.error = "";
      } catch (_) {
        if (generation !== this.generation) return;
        this.clearPending();
        this.state = null;
        this.error = "Backend DSP niedostępny";
      } finally {
        if (generation === this.generation) { this.loading = false; this.changed(); }
      }
    }
    enter() { this.active = true; this.loadState(); }
    leave() { this.active = false; }
    connected() {
      this.clearPending();
      this.requestId = 0;
      if (this.active) this.loadState();
    }
    disconnected() {
      ++this.generation;
      clearTimeout(this.resyncTimer);
      this.clearPending();
      this.state = null;
      this.loading = false;
      this.error = "Backend DSP niedostępny";
      this.changed();
    }
    resync() {
      if (!this.active || this.resyncTimer) return;
      this.resyncTimer = setTimeout(() => { this.resyncTimer = 0; this.loadState(true); }, 80);
    }
    viewState() {
      if (!this.state) return null;
      const view = JSON.parse(JSON.stringify(this.state));
      for (const [key, item] of this.optimistic) patch(view, item.id, item.index, item.value);
      return view;
    }
    command(operation, fields, meta = {}) {
      if (!this.state || this.bridge.getShared().connection !== "connected") return false;
      this.requestId = this.requestId >= 2147483647 ? 1 : this.requestId + 1;
      const id = this.requestId;
      if (!this.bridge.send("dsp." + operation, [id, ...fields].join(","))) return false;
      const item = {...meta, operation, timeout: setTimeout(() => {
        if (this.pending.has(id)) { this.pending.delete(id); this.error = "Brak potwierdzenia DSP."; this.resync(); this.changed(); }
      }, 4000)};
      this.pending.set(id, item);
      return id;
    }
    set(id, index, value) {
      if (!Number.isFinite(value)) return false;
      const key = keyOf(id, index);
      const requestId = this.command("set", [id, index, value], {key, id, index, value});
      if (!requestId) { this.optimistic.delete(key); this.error = "Połączenie DSP jest niedostępne."; this.changed(); return false; }
      this.optimistic.set(key, {id, index, value, requestId});
      this.lastSent.set(key, {value, at: performance.now()});
      this.changed();
      return true;
    }
    edit(id, index, value, final = false) {
      if (!this.state || !Number.isFinite(value)) return;
      const key = keyOf(id, index);
      this.optimistic.set(key, {id, index, value, requestId: 0});
      this.changed();
      clearTimeout(this.timers.get(key));
      const last = this.lastSent.get(key);
      if (final) {
        this.timers.delete(key);
        if (!last || last.value !== value || !this.pending.size) this.set(id, index, value);
      } else {
        const delay = last ? Math.max(0, 120 - (performance.now() - last.at)) : 120;
        this.timers.set(key, setTimeout(() => {
          this.timers.delete(key);
          const current = this.optimistic.get(key);
          if (current) this.set(id, index, current.value);
        }, delay));
      }
    }
    activatePreset(id) { return this.command("activate", [id], {operation: "activate"}); }
    restorePreset() { return this.command("restore", [], {operation: "restore"}); }
    savePreset(id, name) { return this.command("save", name ? [id, name] : [id], {operation: "save"}); }
    renamePreset(id, name) { return this.command("rename", [id, name], {operation: "rename"}); }
    handleWsMessage(message) {
      if (!message || (message.event !== "dsp.changed" && message.event !== "dsp.error")) return false;
      const requestId = message.requestId;
      let pending = this.pending.get(requestId);
      if (message.event === "dsp.changed" && pending &&
          (pending.operation === "set" ?
            message.operation !== 0 || message.parameterId !== pending.id ||
            message.index !== pending.index || message.value !== pending.value :
            message.operation !== {activate: 1, restore: 2, save: 3, rename: 4}[pending.operation]))
        pending = null;
      if (message.event === "dsp.error") {
        if (pending) {
          clearTimeout(pending.timeout);
          this.pending.delete(requestId);
          if (pending.key && this.optimistic.get(pending.key)?.requestId === requestId)
            this.optimistic.delete(pending.key);
        }
        this.error = ERROR_TEXT[message.code] || "Nie udało się zmienić ustawienia DSP.";
        this.resync(); this.changed();
        return true;
      }
      if (!this.state) return true;
      if (!Number.isInteger(message.revision) || message.revision < this.state.runtime.revision) {
        if (pending) {
          clearTimeout(pending.timeout);
          this.pending.delete(requestId);
          if (pending.key && this.optimistic.get(pending.key)?.requestId === requestId)
            this.optimistic.delete(pending.key);
        }
        return true;
      }
      if (pending) { clearTimeout(pending.timeout); this.pending.delete(requestId); }
      this.state.runtime.revision = message.revision;
      if (Number.isInteger(message.appliedRevision)) this.state.runtime.appliedRevision = message.appliedRevision;
      if (typeof message.dirty === "boolean") this.state.runtime.dirty = message.dirty;
      if (message.parameterId === 0 || message.operation !== 0) {
        this.resync(); this.changed(); return true;
      }
      if (Number.isInteger(message.parameterId) && Number.isInteger(message.index) && Number.isFinite(message.value)) {
        const key = keyOf(message.parameterId, message.index);
        patch(this.state, message.parameterId, message.index, message.value);
        if (pending?.key === key && this.optimistic.get(key)?.requestId === requestId)
          this.optimistic.delete(key);
        if ([3, 6, 10, 17].includes(message.parameterId)) this.resync();
      } else this.resync();
      this.error = "";
      this.changed();
      return true;
    }
  }

  function createAdvancedAudioLive(root, bridge) {
    root.innerHTML = window.advancedAudioMarkup();
    const $ = id => root.querySelector("#" + id);
    let selectedBand = 0, model = null, chartFrame = 0, chartDrag = false;
    let outputSignature = "", presetSignature = "", wasActive = false;
    let volumeTimer = 0, lastVolumeSent = 0;
    const client = new DspClient(bridge, render);
    const sharedIds = {
      volume100: "adv-volume", balance: "adv-balance", maximumVolume: "adv-maximum",
      startupMode: "adv-startup-mode", startupFixedVolume: "adv-startup-fixed",
      bass: "adv-bass", middle: "adv-middle", treble: "adv-treble"
    };
    const sharedCommands = {
      volume100: "vol100", balance: "balance", maximumVolume: "maximumvolume",
      startupMode: "startupmode", startupFixedVolume: "startupfixedvolume",
      bass: "bass", middle: "middle", treble: "trebble"
    };
    function setRange(id, value, unit = "") {
      const control = $(id);
      if (control && document.activeElement !== control) control.value = String(value);
      const output = $(id + "-value");
      if (output) output.textContent = fmt(value) + unit;
    }
    function syncShared() {
      const live = bridge.getShared(), fallback = client.state?.sharedAudio || {};
      for (const [key, id] of Object.entries(sharedIds)) {
        const control = $(id);
        const value = Number.isFinite(live[key]) ? live[key] : fallback[key === "volume100" ? "volume" : key];
        const mode = Number.isFinite(live.startupMode) ? live.startupMode : fallback.startupMode;
        control.disabled = !client.state || live.connection !== "connected" || !Number.isFinite(value) ||
          (key === "startupFixedVolume" && mode !== 1);
        if (Number.isFinite(value) && document.activeElement !== control) control.value = String(value);
        if (key !== "startupMode") $(id + "-value").textContent = Number.isFinite(value) ?
          fmt(value) + (["bass", "middle", "treble"].includes(key) ? " dB" : "") : "—";
      }
      $("adv-startup-mode").value = String(Number.isFinite(live.startupMode) ? live.startupMode : fallback.startupMode ?? 0);
      $("adv-startup-fixed-wrap").hidden = $("adv-startup-mode").value !== "1";
    }
    function projection(state) {
      return {
        capabilities: state.capabilities, sharedAudio: state.sharedAudio, presets: state.presets,
        computed: state.computed, runtime: state.runtime,
        peq: state.working.peq.slice(0, state.capabilities.peqBands).map(b =>
          ({enabled: b.enabled, frequency: b.frequencyHz, gain: b.gainDb, q: b.q})),
        dynamics: {mode: state.working.dynamics.enabled ? "custom" : "off",
          threshold: state.working.dynamics.thresholdDbfs, ratio: state.working.dynamics.ratio,
          attack: state.working.dynamics.attackMs, release: state.working.dynamics.releaseMs,
          makeup: state.working.dynamics.makeupGainDb},
        global: {mode: MODES[state.global.outputMode], masterTrim: state.global.masterTrimDb,
          processingEnabled: state.global.processingEnabled, crossoverHz: state.global.crossoverHz,
          hpfMains: state.global.hpfMains, subRouting: state.global.subRouting,
          loudnessEnabled: state.global.loudness.enabled, loudnessIntensity: state.global.loudness.intensity,
          limiterEnabled: state.global.limiter.enabled, limiterThreshold: state.global.limiter.thresholdDbfs,
          limiterAttack: state.global.limiter.attackMs, limiterRelease: state.global.limiter.releaseMs,
          activePresetId: state.global.activePresetId,
          outputs: state.global.outputs.slice(0, state.capabilities.outputCount).map(o =>
            ({trim: o.trimDb, delay: o.delayMs, polarity: o.polarity, mute: o.userMute}))}
      };
    }
    const roleAt = index => ROLES[model.computed.outputs[index]?.role] || "OFF";
    function outputMarkup(index) {
      const role = roleAt(index), off = role === "OFF", value = model.global.outputs[index];
      const muted = model.computed.outputs[index]?.effectiveMute;
      return `<details class="adv-output" data-output="${index}"><summary><strong>OUT${index + 1}</strong><span>${role}</span><span class="adv-output-status">${off ? "Wymuszone MUTE" : muted ? "MUTE" : "Aktywne"}</span></summary>
        <div class="adv-output-fields">
          <div class="audio-control"><div class="audio-control-heading"><label for="adv-out${index}-trim">Trim</label><output id="adv-out${index}-trim-value"></output></div><input id="adv-out${index}-trim" class="volume-slider" type="range" min="-24" max="6" step=".5" data-out="${index}" data-out-field="trim" ${off ? "disabled" : ""}></div>
          <div class="audio-control"><div class="audio-control-heading"><label for="adv-out${index}-delay">Delay</label><output id="adv-out${index}-delay-value"></output></div><input id="adv-out${index}-delay" class="volume-slider" type="range" min="0" max="${model.capabilities.maxDelayMs}" step=".05" data-out="${index}" data-out-field="delay" ${off ? "disabled" : ""}></div>
          <div class="audio-control"><label class="setting-label" for="adv-out${index}-polarity">Polarity</label><select id="adv-out${index}-polarity" class="setting-select" data-out="${index}" data-out-field="polarity" ${off ? "disabled" : ""}><option value="0">0°</option><option value="180">180°</option></select></div>
          <label class="adv-toggle"><input id="adv-out${index}-mute" type="checkbox" data-out="${index}" data-out-field="mute" ${off ? "disabled" : ""}><span>Mute</span></label>
        </div></details>`;
    }
    function renderOutputs() {
      const signature = model.global.outputs.map((_, i) => roleAt(i)).join("|") + "|" + model.capabilities.maxDelayMs;
      if (signature !== outputSignature) {
        const open = [...root.querySelectorAll(".adv-output[open]")].map(item => Number(item.dataset.output));
        $("adv-outputs").innerHTML = model.global.outputs.map((_, i) => outputMarkup(i)).join("");
        open.forEach(index => { const item = root.querySelector(`.adv-output[data-output="${index}"]`); if (item) item.open = true; });
        outputSignature = signature;
      }
      model.global.outputs.forEach((value, index) => {
        const off = roleAt(index) === "OFF";
        setRange(`adv-out${index}-trim`, value.trim, " dB");
        setRange(`adv-out${index}-delay`, value.delay, " ms");
        $(`adv-out${index}-polarity`).value = String(value.polarity);
        $(`adv-out${index}-mute`).checked = value.mute;
        for (const field of ["trim", "delay", "polarity", "mute"]) $(`adv-out${index}-${field}`).disabled = off;
        const status = root.querySelector(`.adv-output[data-output="${index}"] .adv-output-status`);
        status.textContent = off ? "Wymuszone MUTE" : model.computed.outputs[index]?.effectiveMute ? "MUTE" : "Aktywne";
      });
    }
    function renderBands() {
      const short = f => f >= 1000 ? `${fmt(f / 1000)}k` : String(Math.round(f));
      $("adv-band-buttons").innerHTML = model.peq.map((band, index) =>
        `<button type="button" data-band="${index}" aria-pressed="${index === selectedBand}">P${index + 1}<small>${short(band.frequency)} Hz</small></button>`).join("");
      selectedBand = Math.min(selectedBand, Math.max(0, model.peq.length - 1));
      const band = model.peq[selectedBand];
      $("adv-band-title").textContent = band ? `Pasmo ${selectedBand + 1}` : "Brak pasm PEQ";
      if (!band) return;
      $("adv-band-enabled").checked = band.enabled;
      $("adv-band-frequency").value = String(Math.round(Math.log10(band.frequency / 20) / 3 * 1000));
      $("adv-band-frequency-value").textContent = Math.round(band.frequency) + " Hz";
      if (document.activeElement !== $("adv-band-frequency-number")) $("adv-band-frequency-number").value = String(Math.round(band.frequency));
      setRange("adv-band-gain", band.gain, " dB"); setRange("adv-band-q", band.q);
    }
    function render() {
      const connected = bridge.getShared().connection === "connected";
      const state = client.viewState();
      root.querySelector(".adv-demo-badge").textContent = state?.capabilities.demoMode ? "DEMO" : "DSP";
      $("adv-demo-status").textContent = !state ? "Backend DSP niedostępny" :
        state.capabilities.demoMode && !state.capabilities.hasDspHardware ?
          "Tryb demonstracyjny — sprzęt DSP nie jest podłączony" : "Backend DSP aktywny";
      const errorText = state && (client.error || (state.runtime.backendStatus === 2 ?
        "DSP zgłosił błąd zastosowania zmian." : ""));
      $("adv-error-status").textContent = errorText || "";
      $("adv-error-status").hidden = !errorText;
      if (!state) {
        root.querySelectorAll("input,select,button").forEach(control => { control.disabled = true; });
        $("adv-dirty").textContent = "—";
        syncShared();
        return;
      }
      model = projection(state);
      root.querySelectorAll("input,select,button").forEach(control => { control.disabled = !connected; });
      $("adv-band-frequency").disabled = !connected || !model.peq.length;
      const catalog = model.presets.filter(p => p.initialized);
      const signature = catalog.map(p => `${p.id}:${p.name}:${p.writable}`).join("|");
      if (signature !== presetSignature) {
        for (const id of ["adv-preset", "adv-user-slot"]) {
          const control = $(id); control.replaceChildren();
          for (const preset of catalog.filter(p => id === "adv-preset" || p.writable))
            control.add(new Option(preset.name, String(preset.id)));
        }
        presetSignature = signature;
      }
      $("adv-preset").value = String(model.global.activePresetId);
      $("adv-user-slot").value = String(catalog.find(p => p.writable)?.id ?? "");
      const active = catalog.find(p => p.id === model.global.activePresetId);
      $("adv-save").textContent = active?.writable ? "Zapisz preset" : "Zapisz jako User";
      $("adv-rename").disabled = !connected || !active?.writable;
      $("adv-save-as").hidden = !$("adv-save-as").dataset.open || Boolean(active?.writable);
      $("adv-dirty").textContent = model.runtime.dirty ? "Zmieniono" : "Zapisany preset";
      if (model.runtime.revision > model.runtime.appliedRevision) $("adv-dirty").textContent += " · Zmiany oczekują na DSP";
      const modes = $("adv-mode");
      [...modes.options].forEach((option, index) => { option.hidden = !(model.capabilities.supportedOutputModes & (1 << index)); });
      modes.value = String(model.global.mode);
      const roles = model.global.outputs.map((_, index) => `OUT${index + 1} ${roleAt(index)}`);
      $("adv-routing-summary").textContent = roles.join(" · ");
      $("adv-sub-sections").hidden = model.global.mode === "2.0";
      $("adv-sub-routing-wrap").hidden = model.global.mode !== "2.2";
      $("adv-sub-routing").value = String(model.global.subRouting);
      $("adv-sub-crossover").textContent = model.global.crossoverHz + " Hz";
      setRange("adv-crossover", model.global.crossoverHz, " Hz");
      $("adv-hpf").checked = model.global.hpfMains;
      setRange("adv-master-trim", model.global.masterTrim, " dB");
      $("adv-processing").checked = model.global.processingEnabled;
      $("adv-mute").checked = model.runtime.masterMute;
      $("adv-loudness").checked = model.global.loudnessEnabled;
      setRange("adv-loudness-intensity", model.global.loudnessIntensity, "%");
      $("adv-loudness-intensity").disabled = !connected || !model.global.loudnessEnabled;
      $("adv-limiter").checked = model.global.limiterEnabled;
      for (const [field, id, unit] of [["limiterThreshold", "adv-limiter-threshold", " dBFS"], ["limiterAttack", "adv-limiter-attack", " ms"], ["limiterRelease", "adv-limiter-release", " ms"]]) {
        setRange(id, model.global[field], unit); $(id).disabled = !connected || !model.global.limiterEnabled;
      }
      const sub = model.computed.sub;
      $("adv-sub-group-note").textContent = [sub.trim, sub.delay, sub.polarity, sub.mute].some(field => field.mixed) ?
        "OUT3/OUT4 mają różne wartości. Zmiana grupowa ustawi obie jednakowo." :
        "Sterowanie grupowe pokazuje ustawienia OUT3/OUT4.";
      setRange("adv-sub-trim", sub.trim.value ?? 0, " dB");
      setRange("adv-sub-delay", sub.delay.value ?? 0, " ms");
      $("adv-sub-polarity").value = String(sub.polarity.value ?? 0);
      $("adv-sub-mute").checked = Boolean(sub.mute.value);
      $("adv-sub-mute").indeterminate = Boolean(sub.mute.mixed);
      if (sub.trim.mixed) $("adv-sub-trim-value").textContent = "różne";
      if (sub.delay.mixed) $("adv-sub-delay-value").textContent = "różne";
      if (sub.polarity.mixed) $("adv-sub-polarity").selectedIndex = -1;
      $("adv-sub-delay").max = String(model.capabilities.maxDelayMs);
      $("adv-dynamics").value = model.dynamics.mode;
      $("adv-compressor-custom").hidden = model.dynamics.mode !== "custom";
      for (const [field, id, unit] of [["threshold", "adv-comp-threshold", " dBFS"], ["ratio", "adv-comp-ratio", ":1"], ["attack", "adv-comp-attack", " ms"], ["release", "adv-comp-release", " ms"], ["makeup", "adv-comp-makeup", " dB"]]) setRange(id, model.dynamics[field], unit);
      renderOutputs(); renderBands(); syncShared(); scheduleChart();
    }
    function peaking(band, omega) {
      if (!band.enabled || band.gain === 0) return 0;
      const a = Math.pow(10, band.gain / 40), w0 = 2 * Math.PI * band.frequency / model.capabilities.sampleRateHz;
      const alpha = Math.sin(w0) / (2 * band.q), c = Math.cos(w0);
      const b0 = 1 + alpha * a, b1 = -2 * c, b2 = 1 - alpha * a;
      const a0 = 1 + alpha / a, a1 = -2 * c, a2 = 1 - alpha / a;
      const co = Math.cos(omega), si = Math.sin(omega), co2 = Math.cos(2 * omega), si2 = Math.sin(2 * omega);
      const nr = b0 + b1 * co + b2 * co2, ni = -b1 * si - b2 * si2;
      const dr = a0 + a1 * co + a2 * co2, di = -a1 * si - a2 * si2;
      return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    function geometry() {
      const canvas = $("adv-peq-chart"), bounds = canvas.getBoundingClientRect();
      const width = Math.max(1, bounds.width), height = Math.max(1, bounds.height);
      return {canvas, width, height, left: 44, right: width - 12, top: 12, bottom: height - 26};
    }
    const xFor = (f, g) => g.left + Math.log10(f / 20) / 3 * (g.right - g.left);
    const yFor = (db, g) => g.top + (18 - db) / 36 * (g.bottom - g.top);
    function drawChart() {
      chartFrame = 0;
      if (!model || !model.peq.length) return;
      const g = geometry(), dpr = Math.min(window.devicePixelRatio || 1, 2);
      if (g.width < 80) return;
      g.canvas.width = Math.round(g.width * dpr); g.canvas.height = Math.round(g.height * dpr);
      const ctx = g.canvas.getContext("2d"); if (!ctx) return;
      ctx.scale(dpr, dpr); ctx.clearRect(0, 0, g.width, g.height);
      ctx.font = "11px system-ui, sans-serif"; ctx.lineWidth = 1;
      for (const db of [-12, -6, 0, 6, 12]) {
        const y = yFor(db, g); ctx.strokeStyle = db === 0 ? "#62798b" : "#314456";
        ctx.beginPath(); ctx.moveTo(g.left, y); ctx.lineTo(g.right, y); ctx.stroke();
        ctx.fillStyle = "#a2b5c5"; ctx.fillText(`${db > 0 ? "+" : ""}${db}`, 5, y + 4);
      }
      for (const [f, label] of [[20, "20"], [100, "100"], [1000, "1k"], [10000, "10k"], [20000, "20k"]]) {
        const x = xFor(f, g); ctx.strokeStyle = "#314456"; ctx.beginPath();
        ctx.moveTo(x, g.top); ctx.lineTo(x, g.bottom); ctx.stroke();
        ctx.fillStyle = "#a2b5c5"; ctx.fillText(label, Math.max(0, Math.min(x - 10, g.width - 22)), g.height - 5);
      }
      const response = f => model.peq.reduce((db, band) => db + peaking(band, 2 * Math.PI * f / model.capabilities.sampleRateHz), 0);
      ctx.strokeStyle = "#71d8e8"; ctx.lineWidth = 2; ctx.beginPath();
      for (let x = g.left; x <= g.right; x += 2) {
        const f = 20 * Math.pow(1000, (x - g.left) / (g.right - g.left));
        const y = yFor(Math.max(-18, Math.min(18, response(f))), g);
        if (x === g.left) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
      model.peq.forEach((band, index) => {
        const x = xFor(band.frequency, g), y = yFor(Math.max(-18, Math.min(18, response(band.frequency))), g);
        ctx.beginPath(); ctx.arc(x, y, index === selectedBand ? 6 : 4, 0, 2 * Math.PI);
        ctx.fillStyle = band.enabled ? "#71d8e8" : "#a2b5c5"; ctx.fill();
        ctx.strokeStyle = "#0b121b"; ctx.lineWidth = 2; ctx.stroke();
      });
    }
    function scheduleChart() { if (!chartFrame) chartFrame = requestAnimationFrame(drawChart); }
    function chartEdit(event, final = false) {
      if (!model) return;
      const g = geometry(), rect = g.canvas.getBoundingClientRect();
      const x = Math.max(g.left, Math.min(g.right, event.clientX - rect.left));
      const y = Math.max(g.top, Math.min(g.bottom, event.clientY - rect.top));
      const frequency = Math.max(20, Math.min(20000, Math.round(20 * Math.pow(1000, (x - g.left) / (g.right - g.left)))));
      const gain = Math.max(-12, Math.min(12, Math.round((18 - (y - g.top) / (g.bottom - g.top) * 36) * 2) / 2));
      client.edit(PARAM.peqFrequency, selectedBand, frequency, final);
      client.edit(PARAM.peqGain, selectedBand, gain, final);
    }
    const globalIds = {masterTrim: 1, processingEnabled: 2, crossoverHz: 4, hpfMains: 5,
      subRouting: 6, loudnessEnabled: 11, loudnessIntensity: 12,
      limiterEnabled: 13, limiterThreshold: 14, limiterAttack: 15, limiterRelease: 16};
    const outIds = {trim: 7, delay: 8, polarity: 9, mute: 10};
    const groupIds = {trim: 28, delay: 29, polarity: 30, mute: 31};
    const bandIds = {"adv-band-enabled": 18, "adv-band-frequency": 19, "adv-band-gain": 20, "adv-band-q": 21};
    const dynamicsIds = {"adv-comp-threshold": 23, "adv-comp-ratio": 24, "adv-comp-attack": 25,
      "adv-comp-release": 26, "adv-comp-makeup": 27};
    function dspControl(control, final) {
      let id, index = 0, value;
      if (control.dataset.global) id = globalIds[control.dataset.global];
      else if (control.dataset.runtime) id = PARAM.mute;
      else if (control.dataset.group) id = groupIds[control.dataset.group];
      else if (control.dataset.outField) { id = outIds[control.dataset.outField]; index = Number(control.dataset.out); }
      else if (bandIds[control.id]) { id = bandIds[control.id]; index = selectedBand; }
      else id = dynamicsIds[control.id];
      if (!id) return false;
      value = control.type === "checkbox" ? Number(control.checked) : Number(control.value);
      if (control.id === "adv-band-frequency") value = Math.max(20, Math.min(20000, Math.round(20 * Math.pow(1000, value / 1000))));
      if (control.type === "range" && !final) client.edit(id, index, value);
      else if (control.type === "range") client.edit(id, index, value, true);
      else client.set(id, index, value);
      return true;
    }
    root.addEventListener("input", event => {
      const control = event.target;
      if (!client.state || control.disabled) return;
      if (control.dataset.shared === "volume100") {
        $("adv-volume-value").textContent = control.value;
        clearTimeout(volumeTimer);
        const wait = Math.max(0, 120 - (performance.now() - lastVolumeSent));
        volumeTimer = setTimeout(() => { lastVolumeSent = performance.now(); bridge.send("vol100", control.value); }, wait);
      } else if (control.dataset.shared) {
        if (control.type === "range") $(control.id + "-value").textContent = control.value +
          (["bass", "middle", "treble"].includes(control.dataset.shared) ? " dB" : "");
      } else if (control.type === "range") dspControl(control, false);
    });
    root.addEventListener("change", event => {
      const control = event.target;
      if (!client.state || control.disabled) return;
      if (control.dataset.shared) {
        if (control.dataset.shared === "volume100") { clearTimeout(volumeTimer); bridge.send("vol100", control.value); }
        else if (!bridge.send(sharedCommands[control.dataset.shared], control.value)) syncShared();
        return;
      }
      if (control.id === "adv-preset") { client.activatePreset(Number(control.value)); return; }
      if (control.id === "adv-mode") { client.set(PARAM.mode, 0, MODES.indexOf(control.value)); return; }
      if (control.id === "adv-dynamics") { client.set(PARAM.dynamics, 0, control.value === "custom" ? 1 : 0); return; }
      if (control.id === "adv-band-frequency-number") {
        const f = Number(control.value);
        if (Number.isInteger(f) && f >= 20 && f <= 20000) client.set(PARAM.peqFrequency, selectedBand, f);
        else renderBands();
        return;
      }
      dspControl(control, true);
    });
    root.addEventListener("click", event => {
      if (!client.state) return;
      const bandButton = event.target.closest("[data-band]");
      if (bandButton) { selectedBand = Number(bandButton.dataset.band); renderBands(); scheduleChart(); return; }
      if (event.target.id === "adv-restore") { client.restorePreset(); return; }
      if (event.target.id === "adv-save") {
        const active = client.state.presets.find(p => p.id === client.state.global.activePresetId);
        if (!active?.writable && !$("adv-save-as").dataset.open) {
          $("adv-save-as").dataset.open = "1"; $("adv-save-as").hidden = false; return;
        }
        const id = active?.writable ? active.id : Number($("adv-user-slot").value);
        if (Number.isInteger(id)) client.savePreset(id);
        delete $("adv-save-as").dataset.open;
      }
      if (event.target.id === "adv-rename") {
        const active = client.state.presets.find(p => p.id === client.state.global.activePresetId);
        if (!active?.writable) return;
        const name = window.prompt("Nowa nazwa presetu User (1–24 bajty UTF-8)", active.name);
        if (name === null) return;
        const bytes = new TextEncoder().encode(name);
        if (!bytes.length || bytes.length > 24 || /[\u0000-\u001f\u007f]/.test(name)) {
          client.error = "Nazwa musi mieć 1–24 bajty UTF-8."; render(); return;
        }
        client.renamePreset(active.id, name);
      }
    });
    const canvas = $("adv-peq-chart");
    canvas.addEventListener("pointerdown", event => {
      if (!model?.peq.length) return;
      const g = geometry(), rect = canvas.getBoundingClientRect();
      const x = event.clientX - rect.left, y = event.clientY - rect.top;
      let nearest = 0, distance = Infinity;
      const response = f => model.peq.reduce((db, band) => db + peaking(band, 2 * Math.PI * f / model.capabilities.sampleRateHz), 0);
      model.peq.forEach((band, index) => {
        const dx = xFor(band.frequency, g) - x, dy = yFor(Math.max(-18, Math.min(18, response(band.frequency))), g) - y;
        const d = Math.hypot(dx, dy); if (d < distance) { distance = d; nearest = index; }
      });
      if (distance > 24) return;
      selectedBand = nearest; chartDrag = true; canvas.setPointerCapture(event.pointerId);
      renderBands(); scheduleChart();
    });
    canvas.addEventListener("pointermove", event => { if (chartDrag) chartEdit(event); });
    canvas.addEventListener("pointerup", event => { if (chartDrag) chartEdit(event, true); chartDrag = false; });
    canvas.addEventListener("pointercancel", () => { chartDrag = false; client.resync(); });
    window.addEventListener("resize", scheduleChart);
    render();
    return {
      enter: () => { wasActive = true; client.enter(); },
      leave: () => { wasActive = false; client.leave(); },
      connected: () => { if (wasActive) client.connected(); },
      disconnected: () => { clearTimeout(volumeTimer); client.disconnected(); },
      handleWsMessage: message => client.handleWsMessage(message),
      syncShared, render
    };
  }
  window.createAdvancedAudioLive = createAdvancedAudioLive;
  window.VoxOneDspClient = DspClient;
})();
