// Spike G WebKit (Safari JSC) measurement runner.
//
// Uses Playwright's WebKit browser (same JavaScriptCore engine as Safari).
// Mirrors run_chrome.cjs: for each bench loads
//   http://127.0.0.1:<port>/?bench=<name>&auto=1
// waits for window.__spikeFResult, and prints a JSON line.
//
// Usage:
//   node run_webkit.cjs --port 8767 [--bench LIST] [--out FILE] [--show]

const path = require("path");
const fs   = require("fs");

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf("--" + name);
  if (i === -1) return fallback;
  return args[i + 1];
}
function flag(name) { return args.includes("--" + name); }

const port     = parseInt(arg("port", "8767"), 10);
const benchArg = arg("bench", "doom_tick,entity_update,binarytrees");
const benches  = benchArg.split(",").map(s => s.trim()).filter(Boolean);
const outFile  = arg("out", null);
const headless = !flag("show");
const timeout  = parseInt(arg("timeout", "120000"), 10);

(async () => {
  const { webkit } = require("playwright");
  const browser = await webkit.launch({ headless });

  const allResults = [];
  try {
    for (const bench of benches) {
      const url = `http://127.0.0.1:${port}/?bench=${encodeURIComponent(bench)}&auto=1`;
      console.error(`[${new Date().toISOString()}] ${bench} ← ${url}`);
      const page = await browser.newPage();
      page.on("pageerror", (e) => console.error("[pageerror]", e.message));
      page.on("console", (msg) => console.error("[console]", msg.type(), msg.text()));
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
