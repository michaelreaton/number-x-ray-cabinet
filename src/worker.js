importScripts("polynomial.js", "scanner.js");

self.onmessage = (event) => {
  const { id, input, config } = event.data;
  try {
    const report = self.XRayScanner.scanNumber(input, config);
    self.postMessage({ id, ok: true, report });
  } catch (error) {
    self.postMessage({ id, ok: false, error: error.message || String(error) });
  }
};
