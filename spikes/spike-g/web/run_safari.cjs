// Spike G Safari measurement runner (actual Safari via safaridriver).
//
// Prerequisites:
//   safaridriver --enable   (once, needs admin password)
//   Safari must be open.
//
// Uses selenium-webdriver to drive safaridriver.
// Mirrors run_chrome.cjs; waits for window.__spikeFResult.
//
// Usage:
//   node run_safari.cjs --port 8769 [--bench LIST] [--out FILE]

const path = require("path");
const fs   = require("fs");

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf("--" + name);
  if (i === -1) return fallback;
  return args[i + 1];
}

const port     = parseInt(arg("port", "8769"), 10);
const benchArg = arg("bench", "doom_tick,entity_update,binarytrees");
const benches  = benchArg.split(",").map(s => s.trim()).filter(Boolean);
const outFile  = arg("out", null);
const timeout  = parseInt(arg("timeout", "120000"), 10);

(async () => {
  const { Builder, By, until } = require("selenium-webdriver");
  const safari = require("selenium-webdriver/safari");

  const options = new safari.Options();
  const driver = await new Builder()
    .forBrowser("safari")
    .setSafariOptions(options)
    .build();

  const allResults = [];
  try {
    for (const bench of benches) {
      const url = `http://127.0.0.1:${port}/?bench=${encodeURIComponent(bench)}&auto=1`;
      console.error(`[${new Date().toISOString()}] ${bench} ← ${url}`);
      try {
        await driver.get(url);
        await driver.wait(async () => {
          const result = await driver.executeScript(
            "return window.__spikeFResult && window.__spikeFResult.bench ? window.__spikeFResult : null"
          );
          return result != null;
        }, timeout);
        const result = await driver.executeScript("return window.__spikeFResult");
        const ua = await driver.executeScript("return navigator.userAgent");
        result.userAgent = ua;
        const s = result;
        console.log(JSON.stringify({
          bench: s.bench, n: s.n,
          min: s.min, mean: s.mean,
          p50: s.p50, p95: s.p95, p99: s.p99, max: s.max,
          raf_mean: s.raf.mean, raf_p99: s.raf.p99,
        }));
        allResults.push(result);
      } catch (e) {
        console.error(`[${bench}] ERROR:`, e.message);
        allResults.push({ bench, error: String(e.message || e) });
      }
    }
  } finally {
    await driver.quit();
  }

  if (outFile) {
    fs.writeFileSync(outFile, JSON.stringify(allResults, null, 2));
    console.error(`wrote ${allResults.length} results → ${outFile}`);
  }
})().catch((e) => {
  console.error("fatal:", e);
  process.exit(1);
});
