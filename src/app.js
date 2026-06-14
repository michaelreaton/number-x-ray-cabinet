(function startCabinet() {
  const scanner = window.XRayScanner;
  const math = window.XRayPolynomial;
  const i18n = window.XRayI18n;
  const state = {
    mode: "explore",
    view: "chamber",
    report: null,
    progress: null,
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
    rsaCopy: document.getElementById("rsa-copy"),
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
        if (button.dataset.sampleMode) setMode(button.dataset.sampleMode);
        applySampleDefaults(button.dataset.sample);
        updateDigitCount();
        runScan();
      });
    });

    document.querySelectorAll("[data-mode]").forEach((button) => {
      button.addEventListener("click", () => {
        setMode(button.dataset.mode);
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

  function setMode(mode) {
    state.mode = mode;
    const button = document.querySelector(`[data-mode="${mode}"]`);
    if (button) selectInGroup("[data-mode]", button);
    applyModeDefaults(state.mode);
  }

  function updateDigitCount() {
    const preview = scanner.previewIntegerInput(el.input.value);
    el.digitCount.classList.toggle("bad", !preview.parseable && preview.digits > 0);
    el.digitCount.textContent = preview.parseable
      ? i18n.t("digits", { count: preview.digits })
      : i18n.t("digitsFound", { count: preview.digits });
  }

  function readConfig() {
    return {
      nMin: document.getElementById("n-min").value,
      nMax: document.getElementById("n-max").value,
      baseWindow: document.getElementById("base-window").value,
      timeBudgetMs: document.getElementById("time-budget").value,
      verificationLimit: document.getElementById("verification-limit").value,
      k: document.getElementById("k-value").value || "1",
      mode: state.mode
    };
  }

  function applyModeDefaults(mode) {
    const nMax = document.getElementById("n-max");
    const baseWindow = document.getElementById("base-window");
    const timeBudget = document.getElementById("time-budget");
    const verificationLimit = document.getElementById("verification-limit");
    if (mode === "deep" || mode === "rsa") {
      if (Number(nMax.value) < 8192) nMax.value = "8192";
      if (Number(baseWindow.value) > 1) baseWindow.value = "1";
      timeBudget.value = "15000";
      verificationLimit.value = mode === "rsa" ? "40" : "48";
      return;
    }
    if (Number(timeBudget.value) >= 15000 && mode === "explore") timeBudget.value = "3000";
    if (Number(verificationLimit.value) > 24 && mode === "explore") verificationLimit.value = "24";
  }

  function applySampleDefaults(sample) {
    if (sample !== "semiprime") return;
    document.getElementById("n-max").value = "128";
    document.getElementById("base-window").value = "1";
    document.getElementById("time-budget").value = "1500";
    document.getElementById("verification-limit").value = "24";
  }

  function setBusy(isBusy) {
    el.runButton.disabled = isBusy;
    el.cancelButton.classList.toggle("hidden", !isBusy);
    if (isBusy) {
      state.lastStatus = "scanning";
      state.progress = null;
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
    if (window.Worker) {
      try {
        runWorkerScan(input, config);
        return;
      } catch (error) {
        state.activeWorker = null;
      }
    }
    window.setTimeout(() => runLocalScan(input, config), 25);
  }

  function runWorkerScan(input, config) {
    const worker = new Worker("src/worker.js?v=20260614-solver");
    const id = crypto.randomUUID ? crypto.randomUUID() : String(Date.now());
    state.activeWorker = worker;
    worker.onmessage = (event) => {
      if (event.data.id !== id) return;
      if (event.data.kind === "progress") {
        acceptProgress(event.data.progress);
        return;
      }
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
      const report = scanner.scanNumber(input, config, { onProgress: acceptProgress });
      setBusy(false);
      acceptReport(report);
    } catch (error) {
      setBusy(false);
      showError(error.message || String(error));
    }
  }

  function acceptReport(report) {
    state.report = report;
    state.progress = null;
    state.selectedN = report.bestCandidate?.n || null;
    state.lastStatus = "report";
    refreshStatusText();
    renderAll();
  }

  function acceptProgress(progress) {
    state.progress = progress;
    if (state.lastStatus === "scanning") {
      const total = Math.max(1, progress.total || 1);
      const completed = Math.min(total, progress.completed || 0);
      el.status.textContent = i18n.t("progressStatus", {
        stage: i18n.stage(progress.stage),
        count: completed,
        total,
        ms: progress.elapsedMs || 0
      });
    }
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
      if (state.progress) acceptProgress(state.progress);
      else el.status.textContent = i18n.t("scanning");
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
    const candidates = topCandidates(12);
    if (!state.report) {
      el.tableView.innerHTML = `<p class="empty-state">${i18n.t("stageEmpty")}</p>`;
      return;
    }
    const stageRows = (state.report.discoveryStages || []).map((stage) => `
      <span class="stage-pill ${stage.status}">
        <strong>${i18n.stage(stage.name)}</strong>
        ${i18n.stageStatus(stage.status)}
      </span>`).join("");
    el.tableView.innerHTML = `
      <div class="matrix-summary">
        <h3>${i18n.t("discoveryMatrix")}</h3>
        <div class="stage-pills">${stageRows}</div>
      </div>
      <table class="mini-table">
        <thead><tr><th>n</th><th>φ(n)</th><th>${i18n.t("bestB")}</th><th>${i18n.t("rootSignal")}</th><th>${i18n.t("residues")}</th><th>${i18n.t("verifyStatus")}</th><th>${i18n.t("label")}</th><th>${i18n.t("nextAction")}</th></tr></thead>
        <tbody>
          ${candidates.map((candidate) => `
            <tr data-n="${candidate.n}">
              <td>${candidate.n}</td>
              <td>${candidate.phi}</td>
              <td>${scanner.formatBigInt(candidate.bestBase, 14)}</td>
              <td>${Math.round((candidate.discoverySignals?.rootProximity || 0) * 100)}%</td>
              <td>${Math.round(candidate.residueRatio * 100)}%</td>
              <td>${i18n.verifyStatus(candidate.verificationStatus)}</td>
              <td class="${verdictClass(candidate)}">${displayEvidenceLabel(candidate)}</td>
              <td>${i18n.action(candidate.nextAction)}</td>
            </tr>`).join("")}
        </tbody>
      </table>`;
    el.tableView.querySelectorAll("[data-n]").forEach((row) => {
      row.addEventListener("click", () => {
        state.selectedN = Number(row.dataset.n);
        renderAll();
      });
    });
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
        <small>${displayEvidenceLabel(candidate)} · ${i18n.verifyStatus(candidate.verificationStatus)}</small>
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
    renderRsaScout();
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
    const stages = (state.report.discoveryStages || [])
      .map((stage) => `${i18n.stage(stage.name)}: ${i18n.stageStatus(stage.status)}`)
      .join(" · ");
    el.treasureCopy.textContent = top
      ? i18n.t("topRegion", {
          n: top.n,
          base: scanner.formatBigInt(top.bestBase, 12),
          verdict: displayVerdictForSentence(top)
        })
        + (stages ? ` ${stages}` : "")
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
        <dt>${i18n.t("testedBases")}</dt><dd>${(candidate.testedBases || []).map((base) => scanner.formatBigInt(base, 12)).join(", ")}</dd>
        <dt>${i18n.t("candidatePoly")}</dt><dd>${candidate.polynomialPreview}</dd>
        <dt>${i18n.t("phiMatch")}</dt><dd>${candidate.cyclotomicMatch ? i18n.t("exact") : i18n.t("notExact")}</dd>
        <dt>${i18n.t("difference")}</dt><dd>${scanner.formatBigInt(candidate.cyclotomicDifference || candidate.difference || "0", 18)}</dd>
        <dt>${i18n.t("verifyStatus")}</dt><dd>${i18n.verifyStatus(candidate.verificationStatus)}</dd>
        <dt>${i18n.t("rootSignal")}</dt><dd>${Math.round((candidate.discoverySignals?.rootProximity || 0) * 100)}%</dd>
        <dt>${i18n.t("residueConfidence")}</dt><dd>${Math.round(candidate.residueRatio * 100)}%</dd>
      </dl>
      <p>${i18n.action(candidate.nextAction)}</p>
      <div class="meter"><span style="width:${Math.round(candidate.score * 100)}%"></span></div>`;
  }

  function renderRsaScout() {
    const recon = state.report?.rsaRecon;
    if (!recon) {
      el.rsaCopy.innerHTML = `<p>${i18n.t("rsaInitial")}</p>`;
      return;
    }

    const checksumClass = recon.checksum.pass === true ? "good" : recon.checksum.pass === false ? "bad" : "warn";
    const checksumText = recon.checksum.pass === true
      ? `${recon.checksum.residue} (${i18n.t("rsaVerified")})`
      : recon.checksum.expected === null
        ? `${recon.checksum.residue} (${i18n.t("rsaUnknown")})`
        : `${recon.checksum.residue} / ${recon.checksum.expected}`;
    const primalityText = recon.primality.probablyPrime
      ? i18n.t("rsaProbablyPrimeLine", { rounds: recon.primality.rounds })
      : i18n.t("rsaCompositeWitnessLine", { witness: recon.primality.witness });
    const solver = recon.solver || {};
    const factorText = solver.factors?.length
      ? solver.factors.map((factor) => formatFactorRecord(factor)).join(" · ")
      : recon.factorPair
        ? `${scanner.formatBigInt(recon.factorPair.factor, 18)} × ${scanner.formatBigInt(recon.factorPair.cofactor, 18)}`
        : i18n.t("rsaNoFactor");
    const unresolvedText = solver.unresolved?.length
      ? solver.unresolved.map((item) => scanner.formatBigInt(item.value, 18)).join(" · ")
      : i18n.t("no");
    const factorClass = solver.status === "solved" ? "good" : recon.factorPair ? "warn" : "bad";
    const proofClass = solver.productVerified ? "good" : "bad";

    el.rsaCopy.innerHTML = `
      <p class="verdict-head ${recon.factorPair ? "good" : "warn"}">${displayRsaVerdict(recon)}</p>
      <dl class="recon-grid">
        <dt>${i18n.t("rsaTarget")}</dt><dd>${i18n.t("rsaTargetLine", { target: recon.targetLabel, digits: recon.digits, bits: recon.bitLength })}</dd>
        <dt>${i18n.t("rsaSolverStatus")}</dt><dd class="${factorClass}">${solver.status || i18n.t("rsaUnknown")} · ${solver.elapsedMs || 0} ms</dd>
        <dt>${i18n.t("rsaFactors")}</dt><dd class="${factorClass}">${factorText}</dd>
        <dt>${i18n.t("rsaUnresolved")}</dt><dd>${unresolvedText}</dd>
        <dt>${i18n.t("rsaProductProof")}</dt><dd class="${proofClass}">${solver.productVerified ? i18n.t("rsaProofPass") : i18n.t("rsaProofFail")}</dd>
        <dt>${i18n.t("rsaChecksum")}</dt><dd class="${checksumClass}">${checksumText}</dd>
        <dt>${i18n.t("rsaComposite")}</dt><dd>${primalityText}</dd>
        <dt>${i18n.t("rsaSmallSieve")}</dt><dd>${i18n.t("rsaSieveLine", { count: recon.smallPrimeSieve.tested, limit: recon.smallPrimeSieve.limit })}</dd>
        <dt>${i18n.t("rsaFermat")}</dt><dd>${i18n.t("rsaFermatLine", { count: recon.fermat.iterations, status: rsaStatusLabel(recon.fermat.status) })}</dd>
        <dt>${i18n.t("rsaRho")}</dt><dd>${i18n.t("rsaRhoLine", { count: recon.pollardRho.attempts.length, status: rsaStatusLabel(recon.pollardRho.status) })}</dd>
        <dt>${i18n.t("rsaEscalation")}</dt><dd>${solver.escalation?.required ? i18n.t("rsaCommandPack") : i18n.t("no")}</dd>
      </dl>
      <p>${recon.recognized ? i18n.t("rsaSourceLine") : i18n.t("rsaReconLine")}</p>`;
  }

  function formatFactorRecord(factor) {
    const power = factor.exponent > 1 ? `^${factor.exponent}` : "";
    return `${scanner.formatBigInt(factor.value, 18)}${power}`;
  }

  function rsaStatusLabel(status) {
    const key = {
      "no-factor": "rsaStatusNoFactor",
      "no-square-offset": "rsaStatusNoSquare",
      "factor-found": "rsaStatusFactor",
      disabled: "rsaStatusDisabled",
      skipped: "rsaStatusSkipped"
    }[status];
    return key ? i18n.t(key) : status || i18n.t("rsaUnknown");
  }

  function displayRsaVerdict(recon) {
    const solver = recon.solver || {};
    if (solver.status === "solved") return i18n.t("rsaSolved");
    if (solver.status === "partial" || recon.factorPair) return i18n.t("rsaPartial");
    if (solver.status === "timeout") return i18n.t("rsaTimeout");
    if (recon.recognized) return i18n.t("rsaUnsolved", { target: recon.targetLabel });
    if (recon.primality.probablyPrime) return i18n.t("rsaProbablyPrimeLine", { rounds: recon.primality.rounds });
    return i18n.t("rsaCompositeNoFactor");
  }

  function renderCounterexample() {
    const examples = state.report?.counterexamples || scanner.buildCounterexamples();
    const target = state.report?.fragileMatches?.[0] || null;
    const example = target
      ? {
          label: i18n.t("exactHit", { n: target.n, b: scanner.formatBigInt(target.bestBase, 22) }),
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
      <p>${i18n.t("evidenceLabelLine", { label: displayEvidenceLabel(candidate), status: i18n.verifyStatus(candidate.verificationStatus) })}</p>
      <p>${i18n.t("bestCandidateLine", { n: candidate.n, phi: candidate.phi, base: scanner.formatBigInt(candidate.bestBase, 16) })}</p>
      ${state.report.rsaRecon ? `<p>${displayRsaVerdict(state.report.rsaRecon)}</p>` : ""}
      <p>${i18n.t("confidenceLine", { score: candidate.score.toFixed(2), exact, fragile })}</p>
      <div class="meter ${tone}"><span style="width:${Math.round(candidate.score * 100)}%"></span></div>`;
  }

  function displayVerdictForSentence(candidate) {
    const label = displayEvidenceLabel(candidate);
    return i18n.currentLanguage() === "en" ? label.toLowerCase() : label;
  }

  function displayEvidenceLabel(candidate) {
    return i18n.evidenceLabel(candidate.evidenceLabel || candidate.verdict);
  }

  function verdictClass(candidate) {
    if (!candidate) return "";
    if (candidate.verdict === "Exact" || candidate.verdict === "Strong evidence") return "good";
    if (candidate.verdict === "Weak evidence" || candidate.evidenceLabel === "counterexample") return "warn";
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
