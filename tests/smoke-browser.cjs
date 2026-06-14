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
    if (fs.existsSync(filePath)) {
      try {
        return fs.readFileSync(filePath, "utf8");
      } catch (error) {
        if (error.code !== "EBUSY") throw error;
      }
    }
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
      matrix: document.querySelector('.matrix-summary')?.textContent,
      deep: document.querySelector('[data-mode="deep"]')?.textContent,
      rsaMode: document.querySelector('[data-mode="rsa"]')?.textContent,
      semiprimeSample: document.querySelector('[data-sample="semiprime"]')?.textContent,
      rsaSample: document.querySelector('[data-sample="rsa260"]')?.textContent,
      largeSample: document.querySelector('[data-sample="phi3large"]')?.textContent,
      fermatSample: document.querySelector('[data-sample="fermat12"]')?.textContent,
      stageCount: document.querySelectorAll('.stage-pill').length,
      overflowX: document.documentElement.scrollWidth > document.documentElement.clientWidth
    })`);
    assert.equal(english.title, "Number X-Ray Cabinet");
    assert.match(english.status, /Scanned/);
    assert.match(english.credit, /Payam/);
    assert.match(english.matrix, /Discovery Matrix/);
    assert.match(english.deep, /Deep Scan/);
    assert.match(english.rsaMode, /RSA Solver/);
    assert.match(english.semiprimeSample, /10403/);
    assert.match(english.rsaSample, /RSA-260/);
    assert.match(english.largeSample, /1k/);
    assert.match(english.fermatSample, /F12/);
    assert.equal(english.stageCount, 4);
    assert.equal(english.overflowX, false);

    const exportName = await evaluate(cdp, `new Promise((resolve) => {
      const original = HTMLAnchorElement.prototype.click;
      HTMLAnchorElement.prototype.click = function () {
        const name = this.download;
        HTMLAnchorElement.prototype.click = original;
        resolve(name);
      };
      document.querySelector('[data-action="export-json"]').click();
      setTimeout(() => resolve(null), 250);
    })`);
    assert.equal(exportName, "number-x-ray-report.json");

    await cdp.send("Runtime.evaluate", {
      expression: `
        document.querySelector('[data-sample="semiprime"]').click();
        document.querySelector('#cancel-button').click();
        document.querySelector('#n-min').value = '3';
        document.querySelector('#n-max').value = '16';
        document.querySelector('#base-window').value = '0';
        document.querySelector('#time-budget').value = '2000';
        document.querySelector('#verification-limit').value = '2';
        document.querySelector('#run-button').click();
      `
    });
    await waitForExpression(cdp, "document.querySelector('#scan-status')?.textContent.includes('Scanned') && document.querySelector('#rsa-copy')?.textContent.includes('Solved')", 12000);
    const toySolve = await evaluate(cdp, `({
      panel: document.querySelector('#rsa-copy')?.textContent,
      stageCount: document.querySelectorAll('.stage-pill').length,
      overflowX: document.documentElement.scrollWidth > document.documentElement.clientWidth
    })`);
    assert.match(toySolve.panel, /101/);
    assert.match(toySolve.panel, /103/);
    assert.match(toySolve.panel, /verified/i);
    assert.equal(toySolve.stageCount, 6);
    assert.equal(toySolve.overflowX, false);

    await cdp.send("Runtime.evaluate", {
      expression: `
        document.querySelector('[data-mode="rsa"]').click();
        document.querySelector('#cancel-button').click();
        document.querySelector('#n-min').value = '3';
        document.querySelector('#n-max').value = '32';
        document.querySelector('#base-window').value = '0';
        document.querySelector('#time-budget').value = '2000';
        document.querySelector('#verification-limit').value = '2';
        const rsaInput = document.querySelector('#integer-input');
        rsaInput.value = window.XRayScanner.sampleValue('rsa260');
        rsaInput.dispatchEvent(new Event('input', { bubbles: true }));
        document.querySelector('#run-button').click();
      `
    });
    await waitForExpression(cdp, "document.querySelector('#scan-status')?.textContent.includes('Scanned') && document.querySelector('#rsa-copy')?.textContent.includes('RSA-260')", 12000);
    const rsa = await evaluate(cdp, `({
      status: document.querySelector('#scan-status')?.textContent,
      panel: document.querySelector('#rsa-copy')?.textContent,
      stageCount: document.querySelectorAll('.stage-pill').length,
      overflowX: document.documentElement.scrollWidth > document.documentElement.clientWidth
    })`);
    assert.match(rsa.status, /Scanned/);
    assert.match(rsa.panel, /RSA-260/);
    assert.match(rsa.panel, /327430/);
    assert.match(rsa.panel, /unsolved locally|GNFS/i);
    assert.equal(rsa.stageCount, 6);
    assert.equal(rsa.overflowX, false);

    await cdp.send("Runtime.evaluate", {
      expression: `
        document.querySelector('[data-mode="explore"]').click();
        document.querySelector('#cancel-button').click();
        document.querySelector('#n-min').value = '3';
        document.querySelector('#n-max').value = '128';
        document.querySelector('#base-window').value = '2';
        document.querySelector('#time-budget').value = '3000';
        document.querySelector('#verification-limit').value = '24';
      `
    });

    await cdp.send("Runtime.evaluate", {
      expression: `
        const input = document.querySelector('#integer-input');
        input.value = 'Payam note: Φ3(10) = 111';
        input.dispatchEvent(new Event('input', { bubbles: true }));
        document.querySelector('#run-button').click();
      `
    });
    await waitForExpression(cdp, "document.querySelector('#scan-status')?.textContent.includes('Scanned')");
    const messy = await evaluate(cdp, `({
      digits: document.querySelector('#digit-count')?.textContent,
      verdict: document.querySelector('#verdict-copy')?.textContent,
      overflowX: document.documentElement.scrollWidth > document.documentElement.clientWidth
    })`);
    assert.match(messy.digits, /3/);
    assert.match(messy.verdict, /Exact|counterexample|cyclotomic/i);
    assert.equal(messy.overflowX, false);

    await cdp.send("Page.navigate", { url: `${url}?lang=fa` });
    await cdp.waitForEvent("Page.loadEventFired");
    await waitForExpression(cdp, "document.documentElement.lang === 'fa-IR' && document.querySelector('#scan-status')?.textContent.includes('اسکن شد')");
    const persian = await evaluate(cdp, `({
      title: document.querySelector('h1')?.textContent,
      dir: document.documentElement.dir,
      credit: document.querySelector('.paper-credit')?.textContent,
      bridge: document.querySelector('.paper-bridge')?.textContent,
      matrixButton: document.querySelector('[data-view="table"]')?.textContent,
      deep: document.querySelector('[data-mode="deep"]')?.textContent,
      rsaMode: document.querySelector('[data-mode="rsa"]')?.textContent,
      overflowX: document.documentElement.scrollWidth > document.documentElement.clientWidth
    })`);
    assert.match(persian.title, /ایکس/);
    assert.equal(persian.dir, "rtl");
    assert.match(persian.credit, /پیام/);
    assert.match(persian.bridge, /مقالهٔ پیام/);
    assert.match(persian.matrixButton, /ماتریس/);
    assert.match(persian.deep, /عمیق/);
    assert.match(persian.rsaMode, /RSA|حل/);
    assert.equal(persian.overflowX, false);

    await cdp.send("Page.navigate", { url: pathToFileURL(path.join(root, "fa", "index.html")).href });
    await cdp.waitForEvent("Page.loadEventFired");
    await waitForExpression(cdp, "document.documentElement.lang === 'fa-IR'");
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
