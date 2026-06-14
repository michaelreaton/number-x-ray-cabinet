(function startCabinet() {
  const scanner = window.XRayScanner;
  const math = window.XRayPolynomial;
  const i18n = window.XRayI18n;
  const state = {
    mode: "explore",
    view: "chamber",
    report: null,
    selectedN: null,
    activeWorker: null,
    compact: false,
    lastStatus: "ready"
  };

  const el = {
    input: document.getElementById("integer-input"),
    digitCount: document.getElementById("digit-count"),
    runButton: document.getElementById("run-button"),
    cancelButton: document.getElementById("cancel-button"),
    status: document.getElementById("scan-status"),
    chamber: document.getElementById("chamber-svg"),
    chamberView: document.getElementById("chamber-view"),
    tableView: document.getElementById("stage-table-view"),
    timeline: document.getElementById("evidence-timeline"),
    auditBody: document.getElementById("audit-body"),
    scoreChart: document.getElementById("score-chart"),
    treasureCopy: document.getElementById("treasure-copy"),
    skeletonCopy: document.getElementById("skeleton-copy"),
    counterCopy: document.getElementById("counterexample-copy"),
    verdictCopy: document.getElementById("verdict-copy")
  };

  function init() {
    i18n.applyStatic();
    el.input.value = scanner.sampleValue("large");
    updateDigitCount();
    bindEvents();
    runScan();
  }

  function bindEvents() {
    el.input.addEventListener("input", updateDigitCount);
    el.runButton.addEventListener("click", runScan);
    el.cancelButton.addEventListener("click", cancelScan);

    document.querySelectorAll("[data-sample]").forEach((button) => {
      button.addEventListener("click", () => {
        el.input.value = scanner.sampleValue(button.dataset.sample);
        updateDigitCount();
        runScan();
      });
    });

    document.querySelectorAll("[data-mode]").forEach((button) => {
      button.addEventListener("click", () => {
        state.mode = button.dataset.mode;
        selectInGroup("[data-mode]", button);
        runScan();
      });
    });

    document.querySelectorAll("[data-view]").forEach((button) => {
      button.addEventListener("click", () => {
        state.view = button.dataset.view;
        selectInGroup("[data-view]", button);
        renderViews();
      });
    });

    document.querySelectorAll("[data-lang]").forEach((button) => {
      button.addEventListener("click", () => {
        i18n.setLanguage(button.dataset.lang);
        updateDigitCount();
        refreshStatusText();
        renderAll();
      });
    });

    document.addEventListener("click", (event) => {
      const action = event.target.closest("[data-action]")?.dataset.action;
      if (!action) return;
      if (action === "clear-input") {
        el.input.value = "";
        updateDigitCount();
      }
      if (action === "export-json") exportJson();
      if (action === "show-docs") {
        document.getElementById("source-note").scrollIntoView({ behavior: "smooth", block: "center" });
      }
      if (action === "toggle-density") {
        state.compact = !state.compact;
        document.body.classList.toggle("compact", state.compact);
      }
    });
  }

  function selectInGroup(selector, selected) {
    document.querySelectorAll(selector).forEach((button) => button.classList.remove("selected"));
    selected.classList.add("selected");
  }

  function updateDigitCount() {
    const cleaned = el.input.value.replace(/[\s,_+-]/g, "");
    el.digitCount.textContent = i18n.t("digits", { count: cleaned.length });
  }

  function readConfig() {
    return {
      nMin: document.getElementById("n-min").value,
      nMax: document.getElementById("n-max").value,
      baseWindow: document.getElementById("base-window").value,
      timeBudgetMs: document.getElementById("time-budget").value,
      k: document.getElementById("k-value").value || "1",
      mode: state.mode
    };
  }

  function setBusy(isBusy) {
    el.runButton.disabled = isBusy;
    el.cancelButton.classList.toggle("hidden", !isBusy);
    if (isBusy) {
      state.lastStatus = "scanning";
      el.status.textContent = i18n.t("scanning");
    }
  }

  function cancelScan() {
    if (state.activeWorker) {
      state.activeWorker.terminate();
      state.activeWorker = null;
    }
    setBusy(false);
    el.runButton.disabled = false;
    state.lastStatus = "cancelled";
    el.status.textContent = i18n.t("cancelled");
  }

  function runScan() {
    cancelScan();
    setBusy(true);
    const input = el.input.value;
    const config = readConfig();
    if (window.Worker && location.protocol !== "file:") {
      runWorkerScan(input, config);
    } else {
      window.setTimeout(() => runLocalScan(input, config), 25);
    }
  }

  function runWorkerScan(input, config) {
    const worker = new Worker("src/worker.js");
    const id = crypto.randomUUID ? crypto.randomUUID() : String(Date.now());
    state.activeWorker = worker;
    worker.onmessage = (event) => {
      if (event.data.id !== id) return;
      state.activeWorker = null;
      setBusy(false);
      if (!event.data.ok) {
        showError(event.data.error);
        return;
      }
      acceptReport(event.data.report);
    };
    worker.onerror = () => {
      state.activeWorker = null;
      worker.terminate();
      runLocalScan(input, config);
    };
    worker.postMessage({ id, input, config });
  }

  function runLocalScan(input, config) {
    try {
      const report = scanner.scanNumber(input, config);
      setBusy(false);
      acceptReport(report);
    } catch (error) {
      setBusy(false);
      showError(error.message || String(error));
    }
  }

  function acceptReport(report) {
    state.report = report;
    state.selectedN = report.bestCandidate?.n || null;
    state.lastStatus = "report";
    refreshStatusText();
    renderAll();
  }

  function showError(message) {
    state.lastStatus = "error";
    el.status.textContent = message;
    state.report = null;
    renderAll();
  }

  function refreshStatusText() {
    if (state.lastStatus === "ready") {
      el.status.textContent = i18n.t("ready");
      return;
    }
    if (state.lastStatus === "scanning") {
      el.status.textContent = i18n.t("scanning");
      return;
    }
    if (state.lastStatus === "cancelled") {
      el.status.textContent = i18n.t("cancelled");
      return;
    }
    if (state.lastStatus === "report" && state.report) {
      el.status.textContent = i18n.t("scannedStatus", {
        count: state.report.candidates.length,
        ms: state.report.elapsedMs,
        timeout: state.report.timedOut ? i18n.t("timeout") : ""
      });
    }
  }

  function renderAll() {
    renderViews();
    renderTimeline();
    renderAudit();
    renderInspectors();
  }

  function renderViews() {
    el.chamberView.classList.toggle("hidden", state.view !== "chamber");
    el.tableView.classList.toggle("hidden", state.view !== "table");
    renderChamber();
    renderStageTable();
  }

  function topCandidates(limit = 9) {
    return state.report?.candidates?.slice(0, limit) || [];
  }

  function candidateColor(candidate) {
    if (!candidate) return "#7d8883";
    if (candidate.verdict.includes("Invalid") || candidate.verdict === "Error") return "#e36a57";
    if (candidate.score >= 0.75 || candidate.verdict === "Exact") return "#59d7dc";
    if (candidate.score >= 0.4) return "#e4b64d";
    return "#8b9591";
  }

  function renderChamber() {
    const candidates = topCandidates(8);
    if (!state.report) {
      el.chamber.innerHTML = emptyChamberSvg();
      return;
    }
    const rows = candidates.map((candidate, index) => {
      const y = 72 + index * 42;
      const width = 260 + Math.min(160, candidate.score * 160);
      const color = candidateColor(candidate);
      const dash = candidate.score < 0.4 ? "6 8" : "";
      return `
        <g class="ring-row" data-n="${candidate.n}">
          <text x="88" y="${y + 5}" class="ring-label">n = ${candidate.n}</text>
          <ellipse cx="340" cy="${y}" rx="${width / 2}" ry="18" fill="none" stroke="${color}" stroke-width="${1.2 + candidate.score * 2}" stroke-dasharray="${dash}" opacity="0.95"/>
          <ellipse cx="340" cy="${y}" rx="${width / 4}" ry="8" fill="none" stroke="${color}" opacity="0.35"/>
          <circle cx="340" cy="${y}" r="${4 + candidate.score * 5}" fill="${color}" opacity="0.92"/>
          <text x="560" y="${y + 5}" class="phi-label">Φ<tspan baseline-shift="sub">${candidate.n}</tspan>(x)</text>
        </g>`;
    }).join("");

    el.chamber.innerHTML = `
      <svg viewBox="0 0 760 430" role="img" aria-label="Layered cyclotomic candidate chamber">
        <defs>
          <linearGradient id="beam" x1="0" x2="0" y1="0" y2="1">
            <stop offset="0%" stop-color="#66f2ff" stop-opacity="0"/>
            <stop offset="40%" stop-color="#66f2ff" stop-opacity="0.48"/>
            <stop offset="100%" stop-color="#66f2ff" stop-opacity="0.04"/>
          </linearGradient>
          <radialGradient id="floor" cx="50%" cy="50%" r="50%">
            <stop offset="0%" stop-color="#66f2ff" stop-opacity="0.22"/>
            <stop offset="80%" stop-color="#66f2ff" stop-opacity="0"/>
          </radialGradient>
        </defs>
        <rect width="760" height="430" fill="transparent"/>
        <polygon points="340,24 286,398 394,398" fill="url(#beam)"/>
        <ellipse cx="340" cy="398" rx="210" ry="34" fill="url(#floor)"/>
        <text x="340" y="34" text-anchor="middle" class="equation">x = b · y²</text>
        ${rows}
        <path d="M120 392 C230 340 456 340 568 392" fill="none" stroke="#2d4644" stroke-width="1"/>
        <path d="M168 406 H512" stroke="#2d4644"/>
      </svg>`;
  }

  function emptyChamberSvg() {
    return `
      <svg viewBox="0 0 760 430" role="img" aria-label="Empty candidate chamber">
        <text x="380" y="210" text-anchor="middle" class="empty-text">${i18n.t("awaitingScan")}</text>
      </svg>`;
  }

  function renderStageTable() {
    const candidates = topCandidates(10);
    if (!state.report) {
      el.tableView.innerHTML = `<p class="empty-state">${i18n.t("stageEmpty")}</p>`;
      return;
    }
    el.tableView.innerHTML = `
      <table class="mini-table">
        <thead><tr><th>n</th><th>φ(n)</th><th>${i18n.t("bestB")}</th><th>${i18n.t("residues")}</th><th>${i18n.t("score")}</th><th>${i18n.t("verdict")}</th></tr></thead>
        <tbody>
          ${candidates.map((candidate) => `
            <tr data-n="${candidate.n}">
              <td>${candidate.n}</td>
              <td>${candidate.phi}</td>
              <td>${scanner.formatBigInt(candidate.bestBase, 14)}</td>
              <td>${Math.round(candidate.residueRatio * 100)}%</td>
              <td>${candidate.score.toFixed(2)}</td>
              <td class="${verdictClass(candidate)}">${i18n.verdict(candidate.verdict)}</td>
            </tr>`).join("")}
        </tbody>
      </table>`;
  }

  function renderTimeline() {
    const candidates = topCandidates(7);
    if (!state.report) {
      el.timeline.innerHTML = `<p class="empty-state">${i18n.t("timelineEmpty")}</p>`;
      return;
    }
    el.timeline.innerHTML = candidates.map((candidate) => `
      <button type="button" class="timeline-node ${candidate.n === state.selectedN ? "selected" : ""}" data-select-n="${candidate.n}">
        <span class="node-ring" style="--node-color:${candidateColor(candidate)}">${candidate.n}</span>
        <strong>${i18n.t("score")} ${candidate.score.toFixed(2)}</strong>
        <small>${i18n.note(candidate.notes[0]) || i18n.verdict(candidate.verdict)}</small>
      </button>`).join("");
    el.timeline.querySelectorAll("[data-select-n]").forEach((button) => {
      button.addEventListener("click", () => {
        state.selectedN = Number(button.dataset.selectN);
        renderAll();
      });
    });
  }

  function renderAudit() {
    const candidates = state.report?.candidates || [];
    if (!candidates.length) {
      el.auditBody.innerHTML = `<tr><td colspan="10">${i18n.t("auditInitial")}</td></tr>`;
      return;
    }
    el.auditBody.innerHTML = candidates.slice(0, 14).map((candidate, index) => `
      <tr class="${candidate.n === state.selectedN ? "selected-row" : ""}" data-select-n="${candidate.n}">
        <td>${index + 1}</td>
        <td>${candidate.n}</td>
        <td>${candidate.phi ?? "n/a"}</td>
        <td>${scanner.formatBigInt(candidate.bestBase || candidate.baseEstimate || "0", 18)}</td>
        <td>${Number(candidate.score || 0).toFixed(2)}</td>
        <td>${candidate.cyclotomicMatch ? i18n.t("exact") : i18n.t("no")}</td>
        <td class="${BigInt(candidate.gcdKN || 1) > 1n ? "bad" : ""}">${candidate.gcdKN || "n/a"}</td>
        <td>${candidate.exactPower ? i18n.t("pass") : i18n.t("fragile")}</td>
        <td class="${verdictClass(candidate)}">${i18n.verdict(candidate.verdict)}</td>
        <td>${(candidate.notes || []).map((note) => i18n.note(note)).join("; ") || i18n.t("noDominant")}</td>
      </tr>`).join("");
    el.auditBody.querySelectorAll("[data-select-n]").forEach((row) => {
      row.addEventListener("click", () => {
        state.selectedN = Number(row.dataset.selectN);
        renderAll();
      });
    });
  }

  function renderInspectors() {
    renderScoreChart();
    const selected = selectedCandidate();
    renderSkeleton(selected);
    renderCounterexample();
    renderVerdict(selected);
  }

  function selectedCandidate() {
    if (!state.report) return null;
    return state.report.candidates.find((candidate) => candidate.n === state.selectedN) || state.report.bestCandidate;
  }

  function renderScoreChart() {
    const candidates = topCandidates(18).sort((a, b) => a.n - b.n);
    if (!candidates.length) {
      el.scoreChart.innerHTML = "";
      el.treasureCopy.textContent = i18n.t("treasureInitial");
      return;
    }
    const maxScore = Math.max(...candidates.map((candidate) => candidate.score), 0.01);
    const bars = candidates.map((candidate, index) => {
      const width = 240 / Math.max(1, candidates.length);
      const x = 24 + index * width;
      const h = 96 * (candidate.score / maxScore);
      const y = 116 - h;
      return `<rect x="${x.toFixed(1)}" y="${y.toFixed(1)}" width="${Math.max(4, width - 3).toFixed(1)}" height="${h.toFixed(1)}" fill="${candidateColor(candidate)}" opacity="0.9"/>`;
    }).join("");
    el.scoreChart.innerHTML = `
      <svg viewBox="0 0 300 150" role="img" aria-label="Candidate score chart">
        <line x1="22" x2="282" y1="116" y2="116" stroke="#49605c"/>
        <line x1="22" x2="22" y1="18" y2="116" stroke="#49605c"/>
        ${bars}
        <text x="20" y="137" class="chart-label">${i18n.t("chartN")}</text>
        <text x="32" y="26" class="chart-label">${i18n.t("scoreLower")}</text>
      </svg>`;
    const top = state.report.bestCandidate;
    el.treasureCopy.textContent = top
      ? i18n.t("topRegion", {
          n: top.n,
          base: scanner.formatBigInt(top.bestBase, 12),
          verdict: displayVerdictForSentence(top)
        })
      : i18n.t("noSurvivors");
  }

  function renderSkeleton(candidate) {
    if (!candidate) {
      el.skeletonCopy.innerHTML = `<p>${i18n.t("noCandidate")}</p>`;
      return;
    }
    el.skeletonCopy.innerHTML = `
      <p class="math-line">x = b · y² <span>${i18n.t("withB", { b: scanner.formatBigInt(candidate.squareShape.b, 16), kind: candidate.squareShape.exact ? i18n.t("exactSmall") : i18n.t("estSmall") })}</span></p>
      <dl>
        <dt>φ(n)</dt><dd>${candidate.phi}</dd>
        <dt>n</dt><dd>${candidate.n}</dd>
        <dt>${i18n.t("candidatePoly")}</dt><dd>${candidate.polynomialPreview}</dd>
        <dt>${i18n.t("phiMatch")}</dt><dd>${candidate.cyclotomicMatch ? i18n.t("exact") : i18n.t("notExact")}</dd>
        <dt>${i18n.t("residueConfidence")}</dt><dd>${Math.round(candidate.residueRatio * 100)}%</dd>
      </dl>
      <div class="meter"><span style="width:${Math.round(candidate.score * 100)}%"></span></div>`;
  }

  function renderCounterexample() {
    const examples = state.report?.counterexamples || scanner.buildCounterexamples();
    const target = state.report?.fragileMatches?.[0] || null;
    const example = target
      ? {
          label: i18n.t("exactHit", { n: target.n, b: target.bestBase }),
          value: target.cyclotomicValue,
          phi: target.phi,
          perfectRoot: target.exactPower,
          root: target.powerRoot,
          explanation: i18n.t("exactButFragile")
        }
      : examples.find((item) => !item.perfectRoot) || examples[0];
    if (!target) {
      example.explanation = example.perfectRoot ? i18n.t("counterPassExplanation") : i18n.t("counterFailExplanation");
    }
    el.counterCopy.innerHTML = `
      <p><strong>${example.label}</strong></p>
      <p><code>N = ${scanner.formatBigInt(example.value, 30)}</code></p>
      <p>${i18n.t("counterRootLine", { phi: example.phi })}<span class="${example.perfectRoot ? "good" : "warn"}">${example.perfectRoot ? i18n.t("passes") : i18n.t("fails")}</span></p>
      <p>${example.explanation}</p>`;
  }

  function renderVerdict(candidate) {
    if (!state.report || !candidate) {
      el.verdictCopy.innerHTML = `<p>${i18n.t("awaitingScan")}</p>`;
      return;
    }
    const exact = state.report.exactMatches.length;
    const fragile = state.report.fragileMatches.length;
    const tone = candidate.verdict === "Exact" ? "good" : candidate.score >= 0.75 ? "good" : candidate.score >= 0.4 ? "warn" : "bad";
    const headline = candidate.verdict === "Exact"
      ? i18n.t("verdictExact")
      : candidate.score >= 0.75
        ? i18n.t("verdictStrong")
        : candidate.score >= 0.4
          ? i18n.t("verdictWeak")
          : i18n.t("verdictNone");
    el.verdictCopy.innerHTML = `
      <p class="verdict-head ${tone}">${headline}</p>
      <p>${i18n.t("bestCandidateLine", { n: candidate.n, phi: candidate.phi, base: scanner.formatBigInt(candidate.bestBase, 16) })}</p>
      <p>${i18n.t("confidenceLine", { score: candidate.score.toFixed(2), exact, fragile })}</p>
      <div class="meter ${tone}"><span style="width:${Math.round(candidate.score * 100)}%"></span></div>`;
  }

  function displayVerdictForSentence(candidate) {
    const label = i18n.verdict(candidate.verdict);
    return i18n.currentLanguage() === "en" ? label.toLowerCase() : label;
  }

  function verdictClass(candidate) {
    if (!candidate) return "";
    if (candidate.verdict === "Exact" || candidate.verdict === "Strong") return "good";
    if (candidate.verdict === "Weak") return "warn";
    if (candidate.verdict.includes("Invalid") || candidate.verdict === "Error") return "bad";
    return "";
  }

  function exportJson() {
    if (!state.report) return;
    const blob = new Blob([JSON.stringify(state.report, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = "number-x-ray-report.json";
    anchor.click();
    URL.revokeObjectURL(url);
  }

  init();
})();
