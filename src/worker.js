importScripts("polynomial.js", "scanner.js");

self.onmessage = (event) => {
  const { id, input, config } = event.data;
  try {
    const report = self.XRayScanner.scanNumber(input, config, {
      onProgress(progress) {
        self.postMessage({ id, ok: true, kind: "progress", progress });
      }
    });
    self.postMessage({ id, ok: true, kind: "report", report });
  } catch (error) {
    self.postMessage({ id, ok: false, kind: "error", error: error.message || String(error) });
  }
};
