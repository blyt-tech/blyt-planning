// Spike E desktop measurement runner.
//
// Drives the spike_e.html harness via headless Chrome (puppeteer-core,
// using the system Chrome binary).  For each requested cart it loads
// http://127.0.0.1:<port>/?cart=<name>&auto=1, waits for window.__spikeEResult
// to be populated, prints the JSON summary line, and moves to the next cart.
//
// Usage:
//   node run_chrome.cjs --port 8765 --headless [--cart LIST] [--out FILE]
// Default carts: doom_tick, entity_update, doom_tick_c, binarytrees.

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
const cartArg   = arg("cart", "lua_cart_doom_tick.elf,lua_cart_entity_update.elf,doom_tick_c.elf,lua_cart_binarytrees.elf");
const carts     = cartArg.split(",").map(s => s.trim()).filter(Boolean);
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
    for (const cart of carts) {
      const url = `http://127.0.0.1:${port}/?cart=${encodeURIComponent(cart)}&auto=1`;
      console.error(`[${new Date().toISOString()}] ${cart} ← ${url}`);
      const page = await browser.newPage();
      page.on("pageerror", (e) => console.error("[pageerror]", e.message));
      page.on("console", (msg) => console.error("[console]", msg.type(), msg.text()));
      page.on("requestfailed", (req) => console.error("[reqfail]", req.url(), req.failure().errorText));
      // Mirror harness stdout (FRAME / SUMMARY / [worker] lines).
      await page.exposeFunction("__spikeELog", (line) => console.error("[harness]", line));
      await page.evaluateOnNewDocument(() => {
        const $log = () => document.getElementById("log");
        const obs = new MutationObserver(() => {
          const t = $log()?.textContent ?? "";
          const lines = t.split("\n");
          const last = lines[lines.length - 2];
          if (last) window.__spikeELog(last);
        });
        // Wait until #log exists.
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
          "window.__spikeEResult && window.__spikeEResult.cart",
          { timeout }
        );
        const result = await page.evaluate(() => window.__spikeEResult);
        result.userAgent = await page.evaluate(() => navigator.userAgent);
        console.log(JSON.stringify({
          cart: result.cart, n: result.n,
          min: result.min, mean: result.mean,
          p50: result.p50, p95: result.p95, p99: result.p99, max: result.max,
          raf_mean: result.raf.mean, raf_p99: result.raf.p99,
        }));
        allResults.push(result);
      } catch (e) {
        console.error(`[${cart}] ERROR:`, e.message);
        allResults.push({ cart, error: String(e.message || e) });
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
