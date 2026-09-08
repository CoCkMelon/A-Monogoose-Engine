/* ame-next web loader — the ONLY hand-written JS on the site (~70 lines).
 * Each game is one Emscripten single-file bundle compiled from the SAME C
 * as desktop (web/build.sh); this file just probes WebGL2, lazy-loads a
 * bundle on Play, and boots its factory on the card's canvas. */
"use strict";

function ameProbeGL() {
  try {
    const c = document.createElement("canvas");
    if (!c.getContext("webgl2")) return "WebGL2 unavailable (GPU disabled or blocked)";
    return null;
  } catch (e) { return "WebGL2 probe threw: " + e; }
}

function ameLoadScript(src) {
  return new Promise((res, rej) => {
    const s = document.createElement("script");
    s.src = src;
    s.onload = () => res();
    s.onerror = () => rej(new Error("failed to load " + src));
    document.head.appendChild(s);
  });
}

async function amePlay(card) {
  const status = card.querySelector(".st");
  const canvas = card.querySelector("canvas");
  const btn = card.querySelector("button");
  const file = card.dataset.js, factory = card.dataset.factory;
  btn.disabled = true;
  try {
    const glProblem = ameProbeGL();
    if (glProblem) throw new Error(glProblem);
    status.textContent = "loading " + file + " …";
    if (!window[factory]) await ameLoadScript("games/" + file);
    const create = window[factory];
    if (!create) throw new Error("factory " + factory + " missing after load");
    status.textContent = "starting…";
    const mod = await create({ canvas, noExitRuntime: true });
    card._mod = mod;
    if (mod._ame_audio_resume) mod._ame_audio_resume();
    btn.style.display = "none";
    status.textContent = "";
    canvas.focus();
  } catch (e) {
    status.textContent = "error: " + (e && e.message ? e.message : e);
    btn.disabled = false;
  }
}

/* pause off-screen games so one rAF loop runs at a time */
function ameWatch(card) {
  const setRun = (r) => { if (card._mod && card._mod._ame_set_running) card._mod._ame_set_running(r); };
  new IntersectionObserver((es) => es.forEach((e) => setRun(e.isIntersecting ? 1 : 0)))
    .observe(card.querySelector("canvas"));
}

document.querySelectorAll(".game").forEach((card) => {
  card.querySelector("button").addEventListener("click", () => amePlay(card));
  ameWatch(card);
});

/* deep link (also the headless-test hook): ?play=AmeMemory|AmeRm|AmeLine */
(function () {
  const want = new URLSearchParams(location.search).get("play");
  if (!want) return;
  const card = [...document.querySelectorAll(".game")]
    .find((c) => c.dataset.factory.toLowerCase() === want.toLowerCase());
  if (card) card.querySelector("button").click();
})();
