// Spike F desktop measurement runner.
//
// Drives spike_f.html via headless Chrome (puppeteer-core, system Chrome).
// For each requested benchmark, loads
//   http://127.0.0.1:<port>/?bench=<name>&auto=1
// waits for window.__spikeFResult to be populated, prints a JSON line,
// and moves to the next bench.
//
// Usage:
//   node run_chrome.cjs --port 8765 [--bench LIST] [--out FILE] [--show]
// Default benches: doom_tick, entity_update, binarytrees.

const path = require("path");
const fs   = require("fs");

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf("--" + name);
  if (i === -1) return fallback;
  return args[i + 1];
}
function flag(name) { return args.includes("--" + name); }

const port      = parseInt(arg("port", "8765"), 10);
const benchArg  = arg("bench", "doom_tick,entity_update,binarytrees");
const benches   = benchArg.split(",").map(s => s.trim()).filter(Boolean);
const outFile   = arg("out", null);
const headless  = !flag("show");
const chromeBin = arg("chrome", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome");
const timeout   = parseInt(arg("timeout", "120000"), 10);

(async () => {
  const puppeteer = require("puppeteer-core");
  const browser = await puppeteer.launch({
    executablePath: chromeBin,
    headless,
    args: [
      "--no-sandbox",
      "--disable-dev-shm-usage",
    ],
  });

  const allResults = [];
  try {
    for (const bench of benches) {
      const url = `http://127.0.0.1:${port}/?bench=${encodeURIComponent(bench)}&auto=1`;
      console.error(`[${new Date().toISOString()}] ${bench} ← ${url}`);
      const page = await browser.newPage();
      page.on("pageerror", (e) => console.error("[pageerror]", e.message));
      page.on("console", (msg) => console.error("[console]", msg.type(), msg.text()));
      page.on("requestfailed", (req) => console.error("[reqfail]", req.url(), req.failure().errorText));
      await page.exposeFunction("__spikeFLog", (line) => console.error("[harness]", line));
      await page.evaluateOnNewDocument(() => {
        const $log = () => document.getElementById("log");
        const obs = new MutationObserver(() => {
          const t = $log()?.textContent ?? "";
          const lines = t.split("\n");
          const last = lines[lines.length - 2];
          if (last) window.__spikeFLog(last);
        });
        const wait = setInterval(() => {
          if ($log()) {
            clearInterval(wait);
            obs.observe($log(), { childList: true, characterData: true, subtree: true });
          }
        }, 100);
      });
      try {
        await page.goto(url, { waitUntil: "load" });
        await page.waitForFunction(
          "window.__spikeFResult && window.__spikeFResult.bench",
          { timeout }
        );
        const result = await page.evaluate(() => window.__spikeFResult);
        result.userAgent = await page.evaluate(() => navigator.userAgent);
        console.log(JSON.stringify({
          bench: result.bench, n: result.n,
          min: result.min, mean: result.mean,
          p50: result.p50, p95: result.p95, p99: result.p99, max: result.max,
          raf_mean: result.raf.mean, raf_p99: result.raf.p99,
        }));
        allResults.push(result);
      } catch (e) {
        console.error(`[${bench}] ERROR:`, e.message);
        allResults.push({ bench, error: String(e.message || e) });
      } finally {
        await page.close();
      }
    }
  } finally {
    await browser.close();
  }

  if (outFile) {
    fs.writeFileSync(outFile, JSON.stringify(allResults, null, 2));
    console.error(`wrote ${allResults.length} results → ${outFile}`);
  }
})().catch((e) => {
  console.error("fatal:", e);
  process.exit(1);
});
