/* DSP-WEB-LIVE-1: browser view backed by the firmware DSP service. */
(() => {
  "use strict";

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
      <p id="adv-demo-status" class="adv-demo-status" role="status">Backend DSP niedostępny</p>
      <p id="adv-error-status" class="adv-state" role="alert" hidden></p>
      <p class="hint adv-intro">Nowe ustawienia DSP działają w pamięci urządzenia i wrócą do wartości domyślnych po restarcie. Głośność, ton i balans używają istniejących ustawień VoxOne.</p>
      <div class="grid adv-top-grid">
        <article class="card"><h3>Preset brzmienia</h3>
          ${select("adv-preset", "Wybierz preset", [])}
          <p id="adv-dirty" class="adv-state" role="status">—</p>
          <div class="adv-actions"><button id="adv-restore" type="button">Przywróć</button><button id="adv-save" type="button">Zapisz preset</button><button id="adv-rename" type="button">Zmień nazwę</button></div>
          <div id="adv-save-as" class="adv-save-as" hidden>${select("adv-user-slot", "Zapisz jako", [])}</div>
          <p class="hint">Presety fabryczne są chronione. Presety User są przechowywane w RAM do restartu urządzenia.</p>
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
          ${toggle("adv-mute", "Mute DSP", "data-runtime=mute")}
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
          <div id="adv-sub-routing-wrap">${select("adv-sub-routing", "Routing 2.2", [[0, "SUM / MONO — L+R na obu"], [1, "STEREO — L/R"]], "data-global=subRouting")}</div>
          <p id="adv-sub-group-note" class="hint">Sterowanie grupowe pokazuje ustawienia OUT3/OUT4.</p>
          ${range("adv-sub-trim", "Sub Trim", -24, 6, .5, 0, " dB", "data-group=trim")}
          ${range("adv-sub-delay", "Delay", 0, 5, .05, 0, " ms", "data-group=delay")}
          ${select("adv-sub-polarity", "Polarity", [[0, "0°"], [180, "180°"]], "data-group=polarity")}
          ${toggle("adv-sub-mute", "Mute SUB", "data-group=mute")}
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
        ${select("adv-dynamics", "Tryb", [["off", "OFF"], ["custom", "Custom"]])}
        <div id="adv-compressor-custom" hidden>
          ${range("adv-comp-threshold", "Threshold", -40, 0, .5, -18, " dBFS")}
          ${range("adv-comp-ratio", "Ratio", 1, 8, .1, 2, ":1")}
          ${range("adv-comp-attack", "Attack", 1, 100, 1, 10, " ms")}
          ${range("adv-comp-release", "Release", 20, 1000, 10, 200, " ms")}
          ${range("adv-comp-makeup", "Makeup Gain", 0, 12, .5, 0, " dB")}
        </div>
      </details>`;
  }

  window.advancedAudioMarkup = makeMarkup;
})();
