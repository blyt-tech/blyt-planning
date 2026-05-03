// Spike G.2 main-thread harness.

const FRAME_BUDGET_MS = 16.67;

const $bench      = document.getElementById("bench");
const $run        = document.getElementById("run");
const $status     = document.getElementById("status");
const $log        = document.getElementById("log");
const $benchStats = document.getElementById("bench-stats");
const $benchHist  = document.getElementById("bench-hist");
const $rafStats   = document.getElementById("raf-stats");
const $rafHist    = document.getElementById("raf-hist");
const $rafSpinner = document.getElementById("raf-spinner");

for (const e of (typeof benchFiles !== "undefined" ? benchFiles : [])) {
  const opt = document.createElement("option");
  opt.value = e;
  opt.textContent = e;
  $bench.appendChild(opt);
}
$bench.addEventListener("change", () => {
  $run.disabled = !$bench.value;
});

function log(line) {
  $log.textContent += line + "\n";
  $log.scrollTop = $log.scrollHeight;
}

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
  if (meanMs <= FRAME_BUDGET_MS * 0.6) return "pass";
  if (meanMs <= FRAME_BUDGET_MS)       return "warn";
  return "fail";
}

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

const RAF_BUFFER_SIZE = 600;
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
  if (rafFrame % 30 === 0) {
    const s = summarize(rafDeltas);
    renderSummary($rafStats, "rAF", s);
    renderHist($rafHist, rafDeltas, { maxBin: 50 });
  }
  requestAnimationFrame(rafTick);
}
requestAnimationFrame((t) => { lastRaf = t; rafTick(t); });

let worker = null;
let runFrames = [];
let runStart = 0;

function publishResult() {
  const elapsed = performance.now() - runStart;
  const s = summarize(runFrames);
  renderSummary($benchStats, $bench.value, s);
  renderHist($benchHist, runFrames);
  $status.textContent = `done in ${(elapsed / 1000).toFixed(2)} s — ${runFrames.length} ticks`;
  window.__spikeFResult = {
    bench: $bench.value,
    n: s.n, min: s.min, mean: s.mean, p50: s.p50, p95: s.p95, p99: s.p99, max: s.max,
    elapsed_ms: elapsed,
    raf: summarize(rafDeltas),
    frames_ms: runFrames.slice(),
  };
  $run.disabled = false;
}

$run.addEventListener("click", () => {
  if (!$bench.value) return;
  $run.disabled = true;
  $status.textContent = "running " + $bench.value + " …";
  runFrames = [];
  $benchStats.innerHTML = "<em>running…</em>";
  $benchHist.getContext("2d").clearRect(0, 0, $benchHist.width, $benchHist.height);
  $log.textContent = "";

  if (worker) { worker.terminate(); worker = null; }
  worker = new Worker("spike_g2_worker.js");
  worker.onmessage = (ev) => {
    const m = ev.data;
    if (m.type === "stdout") {
      log(m.line);
      const parts = m.line.split(" ");
      if (parts[0] === "FRAME" && parts.length >= 4) {
        const us = parseInt(parts[3], 10);
        if (Number.isFinite(us)) runFrames.push(us / 1000);
      } else if (parts[0] === "SUMMARY") {
        publishResult();
      }
    } else if (m.type === "ready") {
      $status.textContent = "worker initialised; running " + $bench.value + " …";
    } else if (m.type === "done") {
      // Emscripten flushes stdout post-task; trust SUMMARY line not "done".
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
  worker.postMessage({ type: "run", bench: $bench.value });
});

$status.textContent = "ready — pick a benchmark and press Run.";

const params = new URLSearchParams(location.search);
window.__spikeFResult = null;
if (params.get("auto") === "1" && params.get("bench")) {
  const wantBench = params.get("bench");
  const tryStart = () => {
    if ([...$bench.options].some(o => o.value === wantBench)) {
      $bench.value = wantBench;
      $run.disabled = false;
      $run.click();
    } else {
      setTimeout(tryStart, 50);
    }
  };
  tryStart();
}
