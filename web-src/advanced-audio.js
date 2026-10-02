/* DSP-WEB-DEMO-1: browser-only model. The view can later mount inside #audio. */
(() => {
  "use strict";

  const capability = Object.freeze({ peqBands: 5, sampleRate: 48000 });
  const starts = [80, 250, 1000, 4000, 10000];
  const factory = [
    { id: "flat", name: "Flat", tone: [0, 0, 0], dynamics: "off" },
    { id: "bass", name: "Bass", tone: [6, -2, 0], dynamics: "off" },
    { id: "rock", name: "Rock", tone: [5, -2, 4], dynamics: "off" },
    { id: "pop", name: "Pop", tone: [3, 2, 3], dynamics: "off" },
    { id: "mowa", name: "Mowa", tone: [-4, 4, 2], dynamics: "gentle" },
    { id: "noc", name: "Noc", tone: [-2, 1, -1], dynamics: "gentle" }
  ];
  const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
  const copy = value => JSON.parse(JSON.stringify(value));
  const fmt = value => Number.isInteger(Number(value)) ? String(value) : Number(value).toFixed(2).replace(/0+$/, "").replace(/\.$/, "");

  function range(id, label, min, max, step, value, unit = "", extra = "") {
    return `<div class="audio-control"><div class="audio-control-heading"><label for="${id}">${label}</label><output id="${id}-value">${value}${unit}</output></div><input id="${id}" class="volume-slider" type="range" min="${min}" max="${max}" step="${step}" value="${value}" ${extra}></div>`;
  }
  function select(id, label, options, extra = "") {
    return `<div class="audio-control"><label class="setting-label" for="${id}">${label}</label><select id="${id}" class="setting-select" ${extra}>${options.map(([value, name]) => `<option value="${value}">${name}</option>`).join("")}</select></div>`;
  }
  function toggle(id, label, extra = "") {
    return `<label class="adv-toggle"><input id="${id}" type="checkbox" ${extra}><span>${label}</span></label>`;
  }

  function makeMarkup() {
    return `
      <div class="section-heading"><div><p class="eyebrow">DŹWIĘK</p><h2 id="dsp-title">DSP / Audio Advanced</h2></div><span class="badge adv-demo-badge">DEMO</span></div>
      <p class="adv-demo-status" role="status">Tryb demonstracyjny — sprzęt DSP nie jest podłączony</p>
      <p class="hint adv-intro">Nowe ustawienia działają tylko w tej stronie i znikną po jej odświeżeniu. Głośność, ton i balans używają istniejących ustawień VoxOne.</p>
      <div class="grid adv-top-grid">
        <article class="card"><h3>Preset brzmienia</h3>
          ${select("adv-preset", "Wybierz preset", [...factory.map(item => [item.id, item.name]), ["user1", "User 1"], ["user2", "User 2"]])}
          <p id="adv-dirty" class="adv-state" role="status">—</p>
          <div class="adv-actions"><button id="adv-restore" type="button">Przywróć</button><button id="adv-save" type="button">Zapisz preset</button></div>
          <div id="adv-save-as" class="adv-save-as" hidden>${select("adv-user-slot", "Zapisz jako", [["user1", "User 1"], ["user2", "User 2"]])}</div>
          <p class="hint">Presety fabryczne są chronione. Zapis User dotyczy wyłącznie pamięci tej strony.</p>
        </article>
        <article class="card"><h3>Konfiguracja głośników</h3>
          ${select("adv-mode", "Tryb wyjść", [["2.0", "2.0 — stereo"], ["2.1", "2.1 — stereo + sub"], ["2.2", "2.2 — stereo + dwa sub"]])}
          <p id="adv-routing-summary" class="hint"></p>
        </article>
      </div>
      <div class="grid adv-main-grid">
        <article class="card"><h3>Master</h3>
          ${range("adv-volume", "Volume", 0, 100, 1, 0, "", "data-shared=volume100 disabled")}
          ${range("adv-balance", "Balance", -16, 16, 1, 0, "", "data-shared=balance disabled")}
          ${range("adv-master-trim", "Master Trim", -24, 6, .5, 0, " dB", "data-global=masterTrim")}
          ${toggle("adv-processing", "Korekcja DSP", "data-global=processingEnabled checked")}
          ${toggle("adv-mute", "Mute — tylko demo", "data-runtime=mute")}
          <p class="hint">Korekcja OFF omija Tone, PEQ, Auto Loudness i Dynamics. Routing, crossover, delay, trim, limiter i mute pozostają aktywne.</p>
        </article>
        <article class="card"><h3>Ustawienia głośności</h3>
          ${range("adv-maximum", "Maksymalna głośność", 1, 100, 1, 100, "", "data-shared=maximumVolume disabled")}
          ${select("adv-startup-mode", "Głośność po uruchomieniu", [[0, "Ostatnia"], [1, "Ustawiona"]], "data-shared=startupMode disabled")}
          <div id="adv-startup-fixed-wrap">${range("adv-startup-fixed", "Głośność startowa", 0, 100, 1, 20, "", "data-shared=startupFixedVolume disabled")}</div>
          <p class="hint">Te same wartości i komendy co w obecnym AUDIO; brak osobnej konfiguracji DSP.</p>
        </article>
      </div>
      <article class="card"><h3>Brzmienie</h3><div class="adv-tone-grid">
        ${range("adv-bass", "Bass", -6, 6, 1, 0, " dB", "data-shared=bass disabled")}
        ${range("adv-middle", "Middle", -6, 6, 1, 0, " dB", "data-shared=middle disabled")}
        ${range("adv-treble", "Treble", -6, 6, 1, 0, " dB", "data-shared=treble disabled")}
      </div><p class="hint">Zmiana tonu oznacza aktywny preset jako „Zmieniono”. Presety wybiera się na górze strony.</p></article>
      <article class="card"><h3>Parametric EQ</h3>
        <figure class="adv-chart"><canvas id="adv-peq-chart" aria-label="Symulowana odpowiedź PEQ od 20 Hz do 20 kHz"></canvas><figcaption>Symulowana odpowiedź PEQ</figcaption></figure>
        <div id="adv-band-buttons" class="adv-band-buttons" role="group" aria-label="Wybór pasma PEQ"></div>
        <div class="adv-band-editor"><h4 id="adv-band-title">Pasmo 1</h4>
          ${toggle("adv-band-enabled", "Pasmo włączone")}
          ${range("adv-band-frequency", "Częstotliwość", 0, 1000, 1, 201, " Hz")}
          <label class="setting-label" for="adv-band-frequency-number">Dokładna częstotliwość (Hz)</label>
          <input id="adv-band-frequency-number" class="adv-frequency-number" type="number" min="20" max="20000" step="1" value="80" inputmode="numeric">
          ${range("adv-band-gain", "Gain", -12, 12, .5, 0, " dB")}
          ${range("adv-band-q", "Q", .3, 10, .1, 1)}
        </div>
      </article>
      <div id="adv-sub-sections" class="grid adv-main-grid">
        <article class="card"><h3>Crossover</h3>
          ${range("adv-crossover", "Częstotliwość podziału", 40, 200, 1, 80, " Hz", "data-global=crossoverHz")}
          ${toggle("adv-hpf", "HPF dla L/R", "data-global=hpfMains checked")}
          <p class="hint">Linkwitz–Riley 24 dB/okt. · jedna wspólna częstotliwość dla podziału.</p>
        </article>
        <article class="card"><h3>Subwoofer</h3>
          <p class="hint">Crossover: <strong id="adv-sub-crossover">80 Hz</strong> — ta sama wartość co wyżej.</p>
          <div id="adv-sub-routing-wrap">${select("adv-sub-routing", "Routing 2.2", [["sum", "SUM / MONO — L+R na obu"], ["stereo", "STEREO — L/R"]], "data-global=subRouting")}</div>
          <p id="adv-sub-group-note" class="hint">Sterowanie grupowe pokazuje ustawienia OUT3/OUT4.</p>
          ${range("adv-sub-trim", "Sub Trim", -24, 6, .5, 0, " dB", "data-group=trim")}
          ${range("adv-sub-delay", "Delay", 0, 5, .05, 0, " ms", "data-group=delay")}
          ${select("adv-sub-polarity", "Polarity", [[0, "0°"], [180, "180°"]], "data-group=polarity")}
        </article>
      </div>
      <article class="card"><h3>Auto Loudness</h3>
        ${toggle("adv-loudness", "Auto Loudness", "data-global=loudnessEnabled")}
        ${range("adv-loudness-intensity", "Intensity", 0, 100, 1, 0, "%", "data-global=loudnessIntensity")}
        <p class="hint">Adaptacyjna korekcja — silniejsza przy cichym odsłuchu, zanika wraz ze wzrostem Volume. W demo nie zmienia dźwięku.</p>
      </article>
      <article class="card"><h3>Wyjścia</h3><div id="adv-outputs" class="adv-output-list"></div></article>
      <details class="card adv-details"><summary>Zaawansowane / ochrona</summary>
        <h3>Limiter ochronny</h3>
        ${toggle("adv-limiter", "Limiter włączony", "data-global=limiterEnabled checked")}
        ${range("adv-limiter-threshold", "Threshold", -24, 0, .5, -3, " dBFS", "data-global=limiterThreshold")}
        ${range("adv-limiter-attack", "Attack", .5, 20, .5, 5, " ms", "data-global=limiterAttack")}
        ${range("adv-limiter-release", "Release", 20, 1000, 10, 200, " ms", "data-global=limiterRelease")}
        <h3 class="adv-subheading">Dynamics / Compressor</h3>
        ${select("adv-dynamics", "Tryb", [["off", "OFF"], ["gentle", "Gentle"], ["medium", "Medium"], ["custom", "Custom"]])}
        <div id="adv-compressor-custom" hidden>
          ${range("adv-comp-threshold", "Threshold", -40, 0, .5, -18, " dBFS")}
          ${range("adv-comp-ratio", "Ratio", 1, 8, .1, 2, ":1")}
          ${range("adv-comp-attack", "Attack", 1, 100, 1, 10, " ms")}
          ${range("adv-comp-release", "Release", 20, 1000, 10, 200, " ms")}
          ${range("adv-comp-makeup", "Makeup Gain", 0, 12, .5, 0, " dB")}
        </div>
      </details>`;
  }

  function createAdvancedAudioDemo(root, bridge) {
    root.innerHTML = makeMarkup();
    const $ = id => root.querySelector("#" + id);
    const presets = {};
    const newPreset = item => ({
      tone: copy(item.tone),
      peq: starts.slice(0, capability.peqBands).map(frequency => ({ enabled: true, frequency, gain: 0, q: 1 })),
      dynamics: { mode: item.dynamics, threshold: -18, ratio: 2, attack: 10, release: 200, makeup: 0 }
    });
    factory.forEach(item => { presets[item.id] = newPreset(item); });
    presets.user1 = copy(presets.flat);
    presets.user2 = copy(presets.flat);
    const model = {
      activePreset: "flat", selectedBand: 0, peq: copy(presets.flat.peq),
      dynamics: copy(presets.flat.dynamics),
      global: {
        mode: "2.0", masterTrim: 0, processingEnabled: true, crossoverHz: 80,
        hpfMains: true, subRouting: "sum", loudnessEnabled: false, loudnessIntensity: 0,
        limiterEnabled: true, limiterThreshold: -3, limiterAttack: 5, limiterRelease: 200,
        outputs: Array.from({ length: 4 }, () => ({ trim: 0, delay: 0, polarity: 0, mute: false }))
      },
      runtime: { mute: false }
    };
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
    let pendingTone = null;
    let volumeTimer = 0;
    let lastVolumeSent = 0;
    let chartFrame = 0;
    let chartDrag = false;
    const frequencyToSlider = frequency => Math.round(Math.log10(frequency / 20) / 3 * 1000);
    const sliderToFrequency = position => clamp(Math.round(20 * Math.pow(1000, position / 1000)), 20, 20000);
    const outputRoles = () => model.global.mode === "2.0" ? ["LEFT", "RIGHT", "OFF", "OFF"] :
      model.global.mode === "2.1" ? ["LEFT", "RIGHT", "SUB", "OFF"] : ["LEFT", "RIGHT", "SUB-L", "SUB-R"];
    const subIndices = () => model.global.mode === "2.1" ? [2] : [2, 3];
    const snapshot = () => {
      const shared = bridge.getShared();
      return {
        tone: pendingTone || ([shared.bass, shared.middle, shared.treble].every(Number.isFinite) ?
          [shared.bass, shared.middle, shared.treble] : [0, 0, 0]),
        peq: copy(model.peq), dynamics: copy(model.dynamics)
      };
    };
    const dirty = () => JSON.stringify(snapshot()) !== JSON.stringify(presets[model.activePreset]);
    function setRange(id, value, unit = "") {
      const control = $(id);
      if (document.activeElement !== control || !control.matches(":active")) control.value = String(value);
      $(id + "-value").textContent = fmt(value) + unit;
    }
    function markDirty() {
      $("adv-dirty").textContent = dirty() ? "Zmieniono" : "Zapisany preset";
    }
    function sendTone(tone) {
      pendingTone = tone;
      if (!bridge.send("tone", tone.join(","))) pendingTone = null;
    }
    function loadPreset(id) {
      model.activePreset = id;
      model.peq = copy(presets[id].peq);
      model.dynamics = copy(presets[id].dynamics);
      sendTone(presets[id].tone);
      render();
    }
    function outputMarkup(index, role) {
      const off = role === "OFF";
      const value = model.global.outputs[index];
      return `<details class="adv-output" data-output="${index}"><summary><strong>OUT${index + 1}</strong><span>${role}</span><span class="adv-output-status">${off ? "Wymuszone MUTE" : value.mute ? "MUTE" : "Aktywne"}</span></summary>
        <div class="adv-output-fields">
          ${range(`adv-out${index}-trim`, "Trim", -24, 6, .5, value.trim, " dB", `data-out="${index}" data-out-field=trim ${off ? "disabled" : ""}`)}
          ${range(`adv-out${index}-delay`, "Delay", 0, 5, .05, value.delay, " ms", `data-out="${index}" data-out-field=delay ${off ? "disabled" : ""}`)}
          ${select(`adv-out${index}-polarity`, "Polarity", [[0, "0°"], [180, "180°"]], `data-out="${index}" data-out-field=polarity ${off ? "disabled" : ""}`)}
          ${toggle(`adv-out${index}-mute`, "Mute", `data-out="${index}" data-out-field=mute ${off ? "checked disabled" : value.mute ? "checked" : ""}`)}
        </div></details>`;
    }
    function renderOutputs() {
      const open = [...root.querySelectorAll(".adv-output[open]")].map(item => Number(item.dataset.output));
      $("adv-outputs").innerHTML = outputRoles().map((role, index) => outputMarkup(index, role)).join("");
      open.forEach(index => { const item = root.querySelector(`.adv-output[data-output="${index}"]`); if (item) item.open = true; });
    }
    function renderBands() {
      const shortFrequency = frequency => frequency >= 1000 ? `${fmt(frequency / 1000)}k` : String(Math.round(frequency));
      $("adv-band-buttons").innerHTML = model.peq.map((band, index) =>
        `<button type="button" data-band="${index}" aria-pressed="${index === model.selectedBand}">P${index + 1}<small>${shortFrequency(band.frequency)} Hz</small></button>`).join("");
      const band = model.peq[model.selectedBand];
      $("adv-band-title").textContent = `Pasmo ${model.selectedBand + 1}`;
      $("adv-band-enabled").checked = band.enabled;
      $("adv-band-frequency").value = String(frequencyToSlider(band.frequency));
      $("adv-band-frequency-value").textContent = Math.round(band.frequency) + " Hz";
      if (document.activeElement !== $("adv-band-frequency-number")) $("adv-band-frequency-number").value = String(Math.round(band.frequency));
      setRange("adv-band-gain", band.gain, " dB");
      setRange("adv-band-q", band.q);
    }
    function render() {
      $("adv-preset").value = model.activePreset;
      $("adv-mode").value = model.global.mode;
      $("adv-routing-summary").textContent = outputRoles().map((role, index) => `OUT${index + 1} ${role}`).join(" · ");
      $("adv-sub-sections").hidden = model.global.mode === "2.0";
      $("adv-sub-routing-wrap").hidden = model.global.mode !== "2.2";
      $("adv-sub-routing").value = model.global.subRouting;
      $("adv-sub-crossover").textContent = model.global.crossoverHz + " Hz";
      setRange("adv-crossover", model.global.crossoverHz, " Hz");
      $("adv-hpf").checked = model.global.hpfMains;
      setRange("adv-master-trim", model.global.masterTrim, " dB");
      $("adv-processing").checked = model.global.processingEnabled;
      $("adv-mute").checked = model.runtime.mute;
      $("adv-loudness").checked = model.global.loudnessEnabled;
      setRange("adv-loudness-intensity", model.global.loudnessIntensity, "%");
      $("adv-loudness-intensity").disabled = !model.global.loudnessEnabled;
      $("adv-limiter").checked = model.global.limiterEnabled;
      for (const [field, id, unit] of [["limiterThreshold", "adv-limiter-threshold", " dBFS"], ["limiterAttack", "adv-limiter-attack", " ms"], ["limiterRelease", "adv-limiter-release", " ms"]]) {
        setRange(id, model.global[field], unit);
        $(id).disabled = !model.global.limiterEnabled;
      }
      const group = subIndices().map(index => model.global.outputs[index]);
      const groupField = field => group.every(item => item[field] === group[0][field]) ? group[0][field] : null;
      const mixed = ["trim", "delay", "polarity"].some(field => groupField(field) === null);
      $("adv-sub-group-note").textContent = mixed ? "OUT3/OUT4 mają różne wartości. Zmiana grupowa ustawi obie jednakowo." : "Sterowanie grupowe pokazuje ustawienia OUT3/OUT4.";
      setRange("adv-sub-trim", groupField("trim") ?? 0, " dB");
      setRange("adv-sub-delay", groupField("delay") ?? 0, " ms");
      $("adv-sub-polarity").value = String(groupField("polarity") ?? 0);
      if (groupField("trim") === null) $("adv-sub-trim-value").textContent = "różne";
      if (groupField("delay") === null) $("adv-sub-delay-value").textContent = "różne";
      if (groupField("polarity") === null) $("adv-sub-polarity").selectedIndex = -1;
      $("adv-dynamics").value = model.dynamics.mode;
      $("adv-compressor-custom").hidden = model.dynamics.mode !== "custom";
      for (const [field, id, unit] of [["threshold", "adv-comp-threshold", " dBFS"], ["ratio", "adv-comp-ratio", ":1"], ["attack", "adv-comp-attack", " ms"], ["release", "adv-comp-release", " ms"], ["makeup", "adv-comp-makeup", " dB"]]) setRange(id, model.dynamics[field], unit);
      $("adv-save").textContent = model.activePreset.startsWith("user") ? "Zapisz preset" : "Zapisz jako User";
      $("adv-save-as").hidden = !$("adv-save-as").dataset.open || model.activePreset.startsWith("user");
      renderOutputs();
      renderBands();
      markDirty();
      scheduleChart();
      syncShared();
    }
    function syncShared() {
      const shared = bridge.getShared();
      const online = shared.connection === "connected";
      for (const [key, id] of Object.entries(sharedIds)) {
        const control = $(id);
        const value = shared[key];
        control.disabled = !online || !Number.isFinite(value) || (key === "startupFixedVolume" && shared.startupMode !== 1);
        if (Number.isFinite(value) && document.activeElement !== control) control.value = String(value);
        if (key !== "startupMode") $(id + "-value").textContent = Number.isFinite(value) ? fmt(value) + (["bass", "middle", "treble"].includes(key) ? " dB" : "") : "—";
      }
      $("adv-startup-fixed-wrap").hidden = shared.startupMode !== 1;
      if (pendingTone && [shared.bass, shared.middle, shared.treble].every(Number.isFinite) &&
          [shared.bass, shared.middle, shared.treble].every((value, index) => value === pendingTone[index])) pendingTone = null;
      markDirty();
    }
    function peaking(band, omega) {
      if (!band.enabled || band.gain === 0) return 0;
      const a = Math.pow(10, band.gain / 40);
      const w0 = 2 * Math.PI * band.frequency / capability.sampleRate;
      const alpha = Math.sin(w0) / (2 * band.q);
      const c = Math.cos(w0);
      const b0 = 1 + alpha * a, b1 = -2 * c, b2 = 1 - alpha * a;
      const a0 = 1 + alpha / a, a1 = -2 * c, a2 = 1 - alpha / a;
      const co = Math.cos(omega), si = Math.sin(omega);
      const co2 = Math.cos(2 * omega), si2 = Math.sin(2 * omega);
      const nr = b0 + b1 * co + b2 * co2, ni = -b1 * si - b2 * si2;
      const dr = a0 + a1 * co + a2 * co2, di = -a1 * si - a2 * si2;
      return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    function chartGeometry() {
      const canvas = $("adv-peq-chart"), bounds = canvas.getBoundingClientRect();
      const width = Math.max(1, bounds.width), height = Math.max(1, bounds.height);
      return { canvas, width, height, left: 44, right: width - 12, top: 12, bottom: height - 26 };
    }
    const xFor = (frequency, g) => g.left + Math.log10(frequency / 20) / 3 * (g.right - g.left);
    const yFor = (db, g) => g.top + (18 - db) / 36 * (g.bottom - g.top);
    function drawChart() {
      chartFrame = 0;
      const g = chartGeometry(), dpr = Math.min(window.devicePixelRatio || 1, 2);
      if (g.width < 80) return;
      g.canvas.width = Math.round(g.width * dpr);
      g.canvas.height = Math.round(g.height * dpr);
      const ctx = g.canvas.getContext("2d");
      if (!ctx) return;
      ctx.scale(dpr, dpr);
      ctx.clearRect(0, 0, g.width, g.height);
      ctx.font = "11px system-ui, sans-serif";
      ctx.lineWidth = 1;
      for (const db of [-12, -6, 0, 6, 12]) {
        const y = yFor(db, g);
        ctx.strokeStyle = db === 0 ? "#62798b" : "#314456";
        ctx.beginPath(); ctx.moveTo(g.left, y); ctx.lineTo(g.right, y); ctx.stroke();
        ctx.fillStyle = "#a2b5c5"; ctx.fillText(`${db > 0 ? "+" : ""}${db}`, 5, y + 4);
      }
      for (const [frequency, label] of [[20, "20"], [100, "100"], [1000, "1k"], [10000, "10k"], [20000, "20k"]]) {
        const x = xFor(frequency, g);
        ctx.strokeStyle = "#314456"; ctx.beginPath(); ctx.moveTo(x, g.top); ctx.lineTo(x, g.bottom); ctx.stroke();
        ctx.fillStyle = "#a2b5c5"; ctx.fillText(label, clamp(x - 10, 0, g.width - 22), g.height - 5);
      }
      const response = frequency => model.peq.reduce((db, band) => db + peaking(band, 2 * Math.PI * frequency / capability.sampleRate), 0);
      ctx.strokeStyle = "#71d8e8"; ctx.lineWidth = 2; ctx.beginPath();
      for (let x = g.left; x <= g.right; x += 2) {
        const frequency = 20 * Math.pow(1000, (x - g.left) / (g.right - g.left));
        const y = yFor(clamp(response(frequency), -18, 18), g);
        if (x === g.left) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
      model.peq.forEach((band, index) => {
        const x = xFor(band.frequency, g), y = yFor(clamp(response(band.frequency), -18, 18), g);
        ctx.beginPath(); ctx.arc(x, y, index === model.selectedBand ? 6 : 4, 0, 2 * Math.PI);
        ctx.fillStyle = band.enabled ? "#71d8e8" : "#a2b5c5"; ctx.fill();
        ctx.strokeStyle = "#0b121b"; ctx.lineWidth = 2; ctx.stroke();
      });
    }
    function scheduleChart() { if (!chartFrame) chartFrame = requestAnimationFrame(drawChart); }
    function chartEdit(event) {
      const g = chartGeometry(), rect = g.canvas.getBoundingClientRect();
      const x = clamp(event.clientX - rect.left, g.left, g.right), y = clamp(event.clientY - rect.top, g.top, g.bottom);
      const band = model.peq[model.selectedBand];
      band.frequency = clamp(Math.round(20 * Math.pow(1000, (x - g.left) / (g.right - g.left))), 20, 20000);
      band.gain = clamp(Math.round((18 - (y - g.top) / (g.bottom - g.top) * 36) * 2) / 2, -12, 12);
      renderBands(); markDirty(); scheduleChart();
    }
    root.addEventListener("input", event => {
      const control = event.target;
      if (control.dataset.shared === "volume100") {
        $("adv-volume-value").textContent = control.value;
        clearTimeout(volumeTimer);
        const sendVolume = () => { lastVolumeSent = performance.now(); bridge.send("vol100", control.value); };
        const wait = 120 - (performance.now() - lastVolumeSent);
        volumeTimer = setTimeout(sendVolume, Math.max(0, wait));
        return;
      }
      if (control.dataset.shared) {
        if (control.type === "range") $(control.id + "-value").textContent = control.value + (["bass", "middle", "treble"].includes(control.dataset.shared) ? " dB" : "");
        return;
      }
      if (control.dataset.global) {
        model.global[control.dataset.global] = control.type === "checkbox" ? control.checked : control.type === "range" ? Number(control.value) : control.value;
        render(); return;
      }
      if (control.dataset.runtime) { model.runtime[control.dataset.runtime] = control.checked; render(); return; }
      if (control.dataset.group) {
        const value = control.type === "range" ? Number(control.value) : Number(control.value);
        subIndices().forEach(index => { model.global.outputs[index][control.dataset.group] = value; });
        render(); return;
      }
      if (control.dataset.outField) {
        model.global.outputs[Number(control.dataset.out)][control.dataset.outField] = control.type === "checkbox" ? control.checked : Number(control.value);
        if (control.type === "range") setRange(control.id, Number(control.value), control.dataset.outField === "delay" ? " ms" : " dB");
        if (control.dataset.outField === "mute") control.closest(".adv-output").querySelector(".adv-output-status").textContent = control.checked ? "MUTE" : "Aktywne";
        return;
      }
      if (control.id.startsWith("adv-band-") && control.id !== "adv-band-title") {
        const band = model.peq[model.selectedBand];
        const field = { "adv-band-enabled": "enabled", "adv-band-frequency": "frequency", "adv-band-gain": "gain", "adv-band-q": "q" }[control.id];
        if (field) { band[field] = control.type === "checkbox" ? control.checked : field === "frequency" ? sliderToFrequency(Number(control.value)) : Number(control.value); renderBands(); markDirty(); scheduleChart(); }
        return;
      }
      const dynamicsField = { "adv-comp-threshold": "threshold", "adv-comp-ratio": "ratio", "adv-comp-attack": "attack", "adv-comp-release": "release", "adv-comp-makeup": "makeup" }[control.id];
      if (dynamicsField) { model.dynamics[dynamicsField] = Number(control.value); render(); }
    });
    root.addEventListener("change", event => {
      const control = event.target;
      const shared = control.dataset.shared;
      if (shared) {
        if (shared === "volume100") { clearTimeout(volumeTimer); bridge.send("vol100", control.value); }
        else if (!bridge.send(sharedCommands[shared], control.value)) syncShared();
        return;
      }
      if (control.id === "adv-preset") { loadPreset(control.value); return; }
      if (control.id === "adv-mode") { model.global.mode = control.value; render(); return; }
      if (control.id === "adv-dynamics") { model.dynamics.mode = control.value; render(); }
      if (control.id === "adv-band-frequency-number") {
        const frequency = Number(control.value);
        if (Number.isInteger(frequency) && frequency >= 20 && frequency <= 20000) model.peq[model.selectedBand].frequency = frequency;
        else control.value = String(model.peq[model.selectedBand].frequency);
        renderBands(); markDirty(); scheduleChart();
      }
      if (control.dataset.outField) render();
    });
    root.addEventListener("click", event => {
      const bandButton = event.target.closest("[data-band]");
      if (bandButton) { model.selectedBand = Number(bandButton.dataset.band); renderBands(); scheduleChart(); return; }
      if (event.target.id === "adv-restore") { loadPreset(model.activePreset); return; }
      if (event.target.id === "adv-save") {
        if (!model.activePreset.startsWith("user") && !$("adv-save-as").dataset.open) {
          $("adv-save-as").dataset.open = "1"; $("adv-save-as").hidden = false;
          $("adv-save").textContent = "Zapisz do User";
          return;
        }
        const id = model.activePreset.startsWith("user") ? model.activePreset : $("adv-user-slot").value;
        presets[id] = snapshot(); model.activePreset = id;
        delete $("adv-save-as").dataset.open; render();
      }
    });
    const canvas = $("adv-peq-chart");
    canvas.addEventListener("pointerdown", event => {
      const g = chartGeometry(), rect = canvas.getBoundingClientRect();
      const x = event.clientX - rect.left, y = event.clientY - rect.top;
      let nearest = 0, distance = Infinity;
      const response = frequency => model.peq.reduce((db, band) => db + peaking(band, 2 * Math.PI * frequency / capability.sampleRate), 0);
      model.peq.forEach((band, index) => {
        const dx = xFor(band.frequency, g) - x, dy = yFor(clamp(response(band.frequency), -18, 18), g) - y;
        const d = Math.hypot(dx, dy);
        if (d < distance) { distance = d; nearest = index; }
      });
      if (distance > 24) return;
      model.selectedBand = nearest; chartDrag = true; canvas.setPointerCapture(event.pointerId);
      renderBands(); scheduleChart();
    });
    canvas.addEventListener("pointermove", event => { if (chartDrag) chartEdit(event); });
    canvas.addEventListener("pointerup", () => { chartDrag = false; });
    canvas.addEventListener("pointercancel", () => { chartDrag = false; });
    window.addEventListener("resize", scheduleChart);
    render();
    return { syncShared, render };
  }
  window.createAdvancedAudioDemo = createAdvancedAudioDemo;
})();
