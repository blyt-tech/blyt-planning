// Spike E main-thread harness.
//
// Responsibilities:
//   * Populate the cart dropdown from elf_list.js (built into the page).
//   * Drive a requestAnimationFrame loop that records inter-frame deltas and
//     plots a rolling p50/p99/max histogram.  This is the jank meter — if
//     anything from the cart run leaks onto the main thread, the rAF
//     deltas blow out.
//   * Spawn a Web Worker that loads rv32emu.js and runs a single cart.
//     Receive FRAME / SUMMARY lines, compute p50/p95/p99, render histogram.
//
// All measurement is in milliseconds (the cart prints microseconds on the
// FRAME lines; we divide by 1000 on intake).

const FRAME_BUDGET_MS = 16.67;

// ── DOM refs ────────────────────────────────────────────────────────────────
const $cart      = document.getElementById("cart");
const $run       = document.getElementById("run");
const $status    = document.getElementById("status");
const $log       = document.getElementById("log");
const $cartStats = document.getElementById("cart-stats");
const $cartHist  = document.getElementById("cart-hist");
const $rafStats  = document.getElementById("raf-stats");
const $rafHist   = document.getElementById("raf-hist");
const $rafSpinner = document.getElementById("raf-spinner");

// Populate the cart dropdown.
for (const e of (typeof elfFiles !== "undefined" ? elfFiles : [])) {
  const opt = document.createElement("option");
  opt.value = e;
  opt.textContent = e;
  $cart.appendChild(opt);
}
$cart.addEventListener("change", () => {
  $run.disabled = !$cart.value;
});

function log(line) {
  $log.textContent += line + "\n";
  $log.scrollTop = $log.scrollHeight;
}

// ── stats helpers ───────────────────────────────────────────────────────────
function quantile(sorted, q) {
  if (sorted.length === 0) return NaN;
  const i = Math.min(sorted.length - 1, Math.floor(q * sorted.length));
  return sorted[i];
}
function mean(xs) {
  if (xs.length === 0) return NaN;
  return xs.reduce((a, b) => a + b, 0) / xs.length;
}
function fmtMs(v) {
  if (!Number.isFinite(v)) return "—";
  return v.toFixed(2) + " ms";
}

function summarize(values) {
  const sorted = values.slice().sort((a, b) => a - b);
  return {
    n:    values.length,
    min:  sorted[0],
    p50:  quantile(sorted, 0.50),
    p95:  quantile(sorted, 0.95),
    p99:  quantile(sorted, 0.99),
    max:  sorted[sorted.length - 1],
    mean: mean(values),
  };
}

function classifyMean(meanMs) {
  if (meanMs <= FRAME_BUDGET_MS * 0.6) return "pass";   // comfortable
  if (meanMs <= FRAME_BUDGET_MS)       return "warn";   // marginal
  return "fail";                                        // over budget
}

// ── histogram renderer ──────────────────────────────────────────────────────
// Linear bins from 0 to (1.5 × max) in 32 bins.  Bars scaled by sqrt(count)
// so we can see the long tail without collapsing the body.
function renderHist(canvas, values, opts = {}) {
  const ctx = canvas.getContext("2d");
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  if (values.length === 0) return;
  const sorted = values.slice().sort((a, b) => a - b);
  const maxV = Math.max(opts.maxBin || 0, sorted[sorted.length - 1] * 1.05, FRAME_BUDGET_MS * 1.2);
  const bins = 32;
  const counts = new Array(bins).fill(0);
  for (const v of values) {
    const i = Math.min(bins - 1, Math.floor((v / maxV) * bins));
    counts[i]++;
  }
  const maxC = Math.max(...counts);
  const binW = w / bins;
  // 16.67 ms guideline.
  const xBudget = (FRAME_BUDGET_MS / maxV) * w;
  ctx.fillStyle = "#fff3cd";
  ctx.fillRect(0, 0, xBudget, h);
  ctx.strokeStyle = "#b56b00";
  ctx.beginPath();
  ctx.moveTo(xBudget, 0); ctx.lineTo(xBudget, h);
  ctx.stroke();
  ctx.fillStyle = "#3a6";
  for (let i = 0; i < bins; i++) {
    const c = counts[i];
    if (c === 0) continue;
    const bh = (Math.sqrt(c) / Math.sqrt(maxC)) * (h - 4);
    ctx.fillRect(i * binW + 1, h - bh, binW - 2, bh);
  }
  // Axis labels.
  ctx.fillStyle = "#666";
  ctx.font = "10px ui-monospace";
  ctx.fillText("0", 1, h - 2);
  ctx.fillText(maxV.toFixed(1) + " ms", w - 50, h - 2);
  ctx.fillText("16.67", xBudget + 2, 10);
}

function renderSummary($el, label, s) {
  if (!s || !Number.isFinite(s.mean)) {
    $el.innerHTML = "<em>no data</em>";
    return;
  }
  const cls = classifyMean(s.mean);
  $el.innerHTML = `
    <table>
      <tr><td>n</td><td>${s.n}</td></tr>
      <tr><td>min</td><td>${fmtMs(s.min)}</td></tr>
      <tr><td>mean</td><td class="${cls}"><strong>${fmtMs(s.mean)}</strong></td></tr>
      <tr><td>p50</td><td>${fmtMs(s.p50)}</td></tr>
      <tr><td>p95</td><td>${fmtMs(s.p95)}</td></tr>
      <tr><td>p99</td><td>${fmtMs(s.p99)}</td></tr>
      <tr><td>max</td><td>${fmtMs(s.max)}</td></tr>
      <tr><td>budget</td><td>${FRAME_BUDGET_MS} ms (60 fps)</td></tr>
    </table>
  `;
}

// ── rAF jank meter ──────────────────────────────────────────────────────────
const RAF_BUFFER_SIZE = 600;   // ~10 s of frames at 60 fps
const rafDeltas = [];
let lastRaf = performance.now();
let rafFrame = 0;
const spinnerCtx = $rafSpinner.getContext("2d");

function rafTick(t) {
  const dt = t - lastRaf;
  lastRaf = t;
  rafDeltas.push(dt);
  if (rafDeltas.length > RAF_BUFFER_SIZE) rafDeltas.shift();
  rafFrame++;
  // Spinner — visible animation so we can eyeball whether the page is alive.
  const w = $rafSpinner.width, h = $rafSpinner.height;
  spinnerCtx.clearRect(0, 0, w, h);
  spinnerCtx.fillStyle = "#eee";
  spinnerCtx.fillRect(0, 0, w, h);
  spinnerCtx.fillStyle = "#3a6";
  const x = (rafFrame * 4) % w;
  spinnerCtx.fillRect(x, h / 2 - 8, 16, 16);
  spinnerCtx.fillStyle = "#333";
  spinnerCtx.font = "12px ui-monospace";
  spinnerCtx.fillText("rAF #" + rafFrame, 6, 14);
  // Update stats every 30 frames (~½ s).
  if (rafFrame % 30 === 0) {
    const s = summarize(rafDeltas);
    renderSummary($rafStats, "rAF", s);
    renderHist($rafHist, rafDeltas, { maxBin: 50 });
  }
  requestAnimationFrame(rafTick);
}
requestAnimationFrame((t) => { lastRaf = t; rafTick(t); });

// ── Worker plumbing ─────────────────────────────────────────────────────────
let worker = null;
let runFrames = [];   // collected per-tick durations in ms
let runStart = 0;

function publishResult() {
  const elapsed = performance.now() - runStart;
  const s = summarize(runFrames);
  renderSummary($cartStats, $cart.value, s);
  renderHist($cartHist, runFrames);
  $status.textContent = `done in ${(elapsed / 1000).toFixed(2)} s — ${runFrames.length} ticks`;
  window.__spikeEResult = {
    cart: $cart.value,
    n: s.n, min: s.min, mean: s.mean, p50: s.p50, p95: s.p95, p99: s.p99, max: s.max,
    elapsed_ms: elapsed,
    raf: summarize(rafDeltas),
    frames_ms: runFrames.slice(),
  };
  $run.disabled = false;
}

$run.addEventListener("click", () => {
  if (!$cart.value) return;
  $run.disabled = true;
  $status.textContent = "running " + $cart.value + " …";
  runFrames = [];
  $cartStats.innerHTML = "<em>running…</em>";
  $cartHist.getContext("2d").clearRect(0, 0, $cartHist.width, $cartHist.height);
  $log.textContent = "";

  if (worker) { worker.terminate(); worker = null; }
  worker = new Worker("spike_e_worker.js");
  worker.onmessage = (ev) => {
    const m = ev.data;
    if (m.type === "stdout") {
      log(m.line);
      const parts = m.line.split(" ");
      if (parts[0] === "FRAME" && parts.length >= 4) {
        // FRAME <name> <i> <us>
        const us = parseInt(parts[3], 10);
        if (Number.isFinite(us)) runFrames.push(us / 1000);
      } else if (parts[0] === "SUMMARY") {
        // SUMMARY <name> frames=N min=us max=us mean=us
        // The cart prints SUMMARY after the last FRAME — this is the
        // "cart done" signal we trust.  Note: worker's "done" message
        // arrives *before* the FRAME messages on Chrome 147 (Emscripten
        // appears to flush stdout post-task), so we publish results from
        // SUMMARY rather than from worker "done".
        publishResult();
      }
    } else if (m.type === "ready") {
      $status.textContent = "worker initialised; running " + $cart.value + " …";
    } else if (m.type === "done") {
      // "Done" fires when run_user returns inside the worker.  On Chrome
      // 147 we've observed this arrive *before* the FRAME messages —
      // Emscripten appears to flush stdout post-task, so the cart's
      // printf output for the last batch of frames trails the
      // run_user-returned signal.  We rely on the cart-emitted SUMMARY
      // line as the canonical "cart done" trigger and ignore worker
      // "done" entirely for result publication.
    } else if (m.type === "error") {
      log("WORKER ERROR: " + m.msg);
      $status.textContent = "worker error — see log";
      $run.disabled = false;
    }
  };
  worker.onerror = (ev) => {
    log("WORKER onerror: " + ev.message);
    $status.textContent = "worker errored — see log";
    $run.disabled = false;
  };
  runStart = performance.now();
  worker.postMessage({ type: "run", cart: $cart.value });
});

$status.textContent = "ready — pick a cart and press Run.";

// ── auto-run mode (driven by puppeteer / headless chrome) ───────────────────
// URL params: ?cart=lua_cart_doom_tick.elf&auto=1
// Once the run completes, the SUMMARY plus per-tick data is written to
// window.__spikeEResult so an automation script can read it back.
const params = new URLSearchParams(location.search);
window.__spikeEResult = null;
if (params.get("auto") === "1" && params.get("cart")) {
  const wantCart = params.get("cart");
  const tryStart = () => {
    if ([...$cart.options].some(o => o.value === wantCart)) {
      $cart.value = wantCart;
      $run.disabled = false;
      $run.click();
    } else {
      setTimeout(tryStart, 50);
    }
  };
  tryStart();
}

