const assert = require("node:assert/strict");
const childProcess = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

const chromeCandidates = [
  process.env.CHROME_PATH,
  "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
  "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
  "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
  "/usr/bin/google-chrome",
  "/usr/bin/chromium",
  "/usr/bin/chromium-browser"
].filter(Boolean);

function findChrome() {
  return chromeCandidates.find((candidate) => fs.existsSync(candidate));
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function waitForFile(filePath, timeoutMs = 6000) {
  const started = Date.now();
  while (Date.now() - started < timeoutMs) {
    if (fs.existsSync(filePath)) return fs.readFileSync(filePath, "utf8");
    await sleep(50);
  }
  throw new Error(`Timed out waiting for ${filePath}`);
}

function connect(wsUrl) {
  const socket = new WebSocket(wsUrl);
  let nextId = 1;
  const pending = new Map();
  const events = [];

  socket.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    if (message.id && pending.has(message.id)) {
      const { resolve, reject } = pending.get(message.id);
      pending.delete(message.id);
      if (message.error) reject(new Error(message.error.message));
      else resolve(message.result);
    } else if (message.method) {
      events.push(message);
    }
  });

  return new Promise((resolve, reject) => {
    socket.addEventListener("open", () => {
      resolve({
        send(method, params = {}) {
          const id = nextId;
          nextId += 1;
          socket.send(JSON.stringify({ id, method, params }));
          return new Promise((res, rej) => pending.set(id, { resolve: res, reject: rej }));
        },
        async waitForEvent(method, timeoutMs = 6000) {
          const started = Date.now();
          while (Date.now() - started < timeoutMs) {
            const index = events.findIndex((event) => event.method === method);
            if (index >= 0) return events.splice(index, 1)[0];
            await sleep(25);
          }
          throw new Error(`Timed out waiting for CDP event ${method}`);
        },
        close() {
          socket.close();
        }
      });
    });
    socket.addEventListener("error", reject);
  });
}

async function main() {
  const chrome = findChrome();
  if (!chrome) {
    console.warn("No Chrome or Edge executable found; skipping browser smoke test.");
    return;
  }

  const root = path.resolve(__dirname, "..");
  const url = pathToFileURL(path.join(root, "index.html")).href;
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), "xray-chrome-"));
  const chromeProcess = childProcess.spawn(chrome, [
    "--headless=new",
    "--disable-gpu",
    "--no-first-run",
    "--no-default-browser-check",
    "--remote-debugging-port=0",
    `--user-data-dir=${userDataDir}`,
    "about:blank"
  ], { stdio: "ignore" });

  try {
    const activePort = await waitForFile(path.join(userDataDir, "DevToolsActivePort"));
    const [port] = activePort.trim().split(/\r?\n/);
    const list = await fetch(`http://127.0.0.1:${port}/json/list`).then((response) => response.json());
    const pageInfo = list.find((target) => target.type === "page");
    assert.ok(pageInfo?.webSocketDebuggerUrl, "Chrome did not expose a page target");
    const cdp = await connect(pageInfo.webSocketDebuggerUrl);

    await cdp.send("Page.enable");
    await cdp.send("Runtime.enable");
    await cdp.send("Page.navigate", { url });
    await cdp.waitForEvent("Page.loadEventFired");
    await waitForExpression(cdp, "document.querySelector('#scan-status')?.textContent.includes('Scanned')");
    await cdp.send("Runtime.evaluate", {
      expression: "document.querySelector('[data-sample=\"phi3\"]').click(); document.querySelector('[data-view=\"table\"]').click();"
    });
    await waitForExpression(cdp, "document.querySelector('.mini-table') && document.querySelector('#scan-status')?.textContent.includes('Scanned')");
    const english = await evaluate(cdp, `({
      title: document.querySelector('h1')?.textContent,
      status: document.querySelector('#scan-status')?.textContent,
      credit: document.querySelector('.paper-credit')?.textContent,
      overflowX: document.documentElement.scrollWidth > document.documentElement.clientWidth
    })`);
    assert.equal(english.title, "Number X-Ray Cabinet");
    assert.match(english.status, /Scanned/);
    assert.match(english.credit, /Payam/);
    assert.equal(english.overflowX, false);

    await cdp.send("Page.navigate", { url: `${url}?lang=fa` });
    await cdp.waitForEvent("Page.loadEventFired");
    await waitForExpression(cdp, "document.documentElement.lang === 'fa-IR' && document.querySelector('#scan-status')?.textContent.includes('اسکن شد')");
    const persian = await evaluate(cdp, `({
      title: document.querySelector('h1')?.textContent,
      dir: document.documentElement.dir,
      credit: document.querySelector('.paper-credit')?.textContent,
      bridge: document.querySelector('.paper-bridge')?.textContent,
      overflowX: document.documentElement.scrollWidth > document.documentElement.clientWidth
    })`);
    assert.match(persian.title, /ایکس/);
    assert.equal(persian.dir, "rtl");
    assert.match(persian.credit, /پیام/);
    assert.match(persian.bridge, /مقالهٔ پیام/);
    assert.equal(persian.overflowX, false);
    cdp.close();
  } finally {
    chromeProcess.kill();
    await waitForExit(chromeProcess, 2500);
    await removeWithRetry(userDataDir);
  }
}

async function evaluate(cdp, expression) {
  const result = await cdp.send("Runtime.evaluate", {
    expression,
    returnByValue: true,
    awaitPromise: true
  });
  if (result.exceptionDetails) {
    throw new Error(result.exceptionDetails.text || "Runtime evaluation failed");
  }
  return result.result.value;
}

async function waitForExpression(cdp, expression, timeoutMs = 8000) {
  const started = Date.now();
  while (Date.now() - started < timeoutMs) {
    const value = await evaluate(cdp, expression);
    if (value) return;
    await sleep(80);
  }
  throw new Error(`Timed out waiting for expression: ${expression}`);
}

function waitForExit(processHandle, timeoutMs) {
  if (processHandle.exitCode !== null) return Promise.resolve();
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, timeoutMs);
    processHandle.once("exit", () => {
      clearTimeout(timer);
      resolve();
    });
  });
}

async function removeWithRetry(targetPath) {
  for (let attempt = 0; attempt < 6; attempt += 1) {
    try {
      fs.rmSync(targetPath, { recursive: true, force: true });
      return;
    } catch (error) {
      if (attempt === 5) throw error;
      await sleep(250);
    }
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
