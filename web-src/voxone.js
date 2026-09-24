(() => {
  "use strict";

  const tabs = ["status", "stations", "settings", "system"];
  const text = (id, value) => {
    const node = document.getElementById(id);
    if (node) node.textContent = value;
  };

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

  // Existing /variables.js is the sole firmware version/profile source.
  const version = typeof voxOneVersion === "string" ? voxOneVersion : "—";
  const profile = typeof voxOneProfile === "string" ? voxOneProfile : "—";
  const base = typeof yoRadioVersion === "string" ? yoRadioVersion : "—";
  text("header-version", "VoxOne " + version);
  text("header-profile", "Profil " + profile.toUpperCase());
  text("system-version", "VoxOne " + version);
  text("system-profile", profile.toUpperCase());
  text("system-base", "yoRadio " + base);

  window.addEventListener("hashchange", showTab);
  showTab();

  // WEB-1B: connect an adapter to the existing /ws protocol here.
})();