(function attachI18n(root) {
  const dictionaries = {
    en: {
      appTitle: "Number X-Ray Cabinet",
      subtitle: "Detecting hidden cyclotomic structure in large integers.",
      credit: "Credit: Payam · Paper",
      engine: "Engine: local BigInt",
      docs: "Docs",
      settings: "Settings",
      input: "Input",
      pasteInteger: "Paste integer",
      digits: "Digits: {count}",
      digitsFound: "Digits found: {count}",
      clear: "Clear",
      run: "Run X-Ray",
      cancel: "Cancel scan",
      scanControls: "Scan Controls",
      nMin: "n min",
      nMax: "n max",
      baseWindow: "Base window",
      timeBudget: "Time budget",
      verificationLimit: "Verify limit",
      explore: "Explore",
      deep: "Deep Scan",
      counterMode: "Hunt Counterexamples",
      verify: "Verify Claim",
      samples: "Samples",
      samplePhi21: "Φ21(3)",
      samplePhi3: "Counterexample 111",
      samplePhi3Large: "1k Φ3 Fixture",
      samplePhi5: "Prime 31",
      sampleCarmichael: "Carmichael 561",
      samplePrimePower: "Prime Power 3^7",
      sampleLarge: "150-digit Probe",
      sourceNote: "Source Note",
      sourceNoteHtml:
        'Built from Payam’s paper, <a href="assets/Payam_Idea.pdf" target="_blank" rel="noopener">Payam_Idea.pdf</a>. The export cuts off the promised Python script after <code>import sympy as sp</code>, so this cabinet reconstructs the idea and marks fragile assumptions directly.',
      bridgeTitle: "Payam’s Paper → This Lab",
      bridgeP1:
        "Payam’s paper proposes an “X-ray” for large integers: instead of asking only whether a number is prime, it asks whether the number hides a cyclotomic construction.",
      bridgeP2:
        "This app turns that idea into an interactive instrument. It searches candidate n, computes φ(n), estimates possible bases, evaluates Φₙ(b), and shows the evidence trail.",
      bridgeP3:
        "The skeptical parts are intentional: the PDF says to recover structure with root tests and symbolic algebra, but the visible script is incomplete. The cabinet therefore also shows counterexamples where a real cyclotomic value fails the shortcut.",
      candidates: "Cyclotomic Candidates",
      ready: "Ready. Load a sample or paste an integer.",
      chamber: "Chamber",
      table: "Matrix",
      evidenceScore: "Evidence score",
      signalsCombined: "Signals combined",
      signalExact: "Exact Φn(b) equality",
      signalRoot: "φ(n)-root proximity",
      signalResidue: "Residue agreement",
      signalGcd: "gcd(k,n) structure",
      evidence: "Evidence",
      treasureMap: "Treasure Map",
      treasureInitial: "Top regions appear after the first scan.",
      skeletonKey: "Skeleton Key",
      noCandidate: "No candidate selected.",
      counterexample: "Counterexample",
      counterInitial: "Counterexamples show when an exact cyclotomic value fails the PDF’s perfect-root shortcut.",
      verdict: "Verdict",
      awaitingScan: "Awaiting scan.",
      exportJson: "Export report (JSON)",
      auditInitial: "Run a scan to populate the audit log.",
      stageEmpty: "Run a scan to inspect candidate details.",
      timelineEmpty: "Evidence timeline is empty.",
      scanning: "Profiling, screening, and verifying candidates...",
      progressStatus: "{stage}: {count}/{total} in {ms} ms",
      cancelled: "Scan cancelled.",
      scannedStatus: "Scanned {count} candidates in {ms} ms.{timeout}",
      timeout: " Time budget reached; showing partial evidence.",
      topRegion: "Top region: n ≈ {n}; best base {base}; {verdict} evidence.",
      noSurvivors: "No candidate survived filtering.",
      score: "Score",
      scoreLower: "score",
      bestB: "Best b",
      testedBases: "Tested bases",
      difference: "Difference",
      residues: "Residues",
      discoveryMatrix: "Discovery Matrix",
      rootSignal: "Root signal",
      verifyStatus: "Verify status",
      label: "Label",
      nextAction: "Next action",
      rootTest: "Root test",
      notes: "Notes",
      match: "Match",
      exact: "Exact",
      notExact: "not exact",
      no: "No",
      pass: "Pass",
      fragile: "Fragile",
      exactSmall: "exact",
      estSmall: "est.",
      withB: "with b = {b} ({kind})",
      candidatePoly: "Candidate Φn(x)",
      phiMatch: "Φn(b) match",
      residueConfidence: "Residue confidence",
      exactHit: "Φ{n}({b}) exact hit",
      exactButFragile: "Exact candidate, but the perfect-root shortcut does not prove it.",
      counterPassExplanation: "This one also passes the perfect-root test.",
      counterFailExplanation: "Exact cyclotomic value, but not a perfect φ(n)-th power.",
      counterRootLine: "φ(n) = {phi}; root test = ",
      passes: "passes",
      fails: "fails",
      verdictExact: "Exact cyclotomic structure detected.",
      verdictStrong: "Plausible structure detected.",
      verdictWeak: "Weak structure signal.",
      verdictNone: "No convincing structure.",
      evidenceLabelLine: "Label: {label}. Verification: {status}.",
      bestCandidateLine: "Best candidate: n = {n} with φ(n) = {phi} and b = {base}.",
      confidenceLine: "Confidence {score}; exact matches {exact}; fragile exact hits {fragile}.",
      noDominant: "No dominant signal",
      chartN: "n",
      languageLabel: "Language",
      english: "EN",
      persian: "FA"
    },
    fa: {
      appTitle: "کابینت ایکس‌ری اعداد",
      subtitle: "جست‌وجوی ساختار پنهان سیکلوتومیک در عددهای بزرگ.",
      credit: "اعتبار: پیام · مقاله",
      engine: "موتور: BigInt محلی",
      docs: "یادداشت‌ها",
      settings: "تنظیمات",
      input: "ورودی",
      pasteInteger: "عدد صحیح را وارد کنید",
      digits: "تعداد رقم‌ها: {count}",
      digitsFound: "رقم‌های پیدا شده: {count}",
      clear: "پاک کردن",
      run: "اجرای ایکس‌ری",
      cancel: "لغو اسکن",
      scanControls: "کنترل‌های اسکن",
      nMin: "کمینه n",
      nMax: "بیشینه n",
      baseWindow: "بازه پایه",
      timeBudget: "بودجه زمان",
      verificationLimit: "حد راستی‌آزمایی",
      explore: "کاوش",
      deep: "اسکن عمیق",
      counterMode: "شکار نمونه نقض",
      verify: "راستی‌آزمایی ادعا",
      samples: "نمونه‌ها",
      samplePhi21: "Φ21(3)",
      samplePhi3: "نمونه نقض 111",
      samplePhi3Large: "نمونه Φ3 هزاررقمی",
      samplePhi5: "عدد اول 31",
      sampleCarmichael: "کارمایکل 561",
      samplePrimePower: "توان اول 3^7",
      sampleLarge: "آزمون ۱۵۰ رقمی",
      sourceNote: "یادداشت منبع",
      sourceNoteHtml:
        'بر پایهٔ مقالهٔ پیام، <a href="assets/Payam_Idea.pdf" target="_blank" rel="noopener">Payam_Idea.pdf</a>. خروجی PDF کد پایتون وعده‌داده‌شده را بعد از <code>import sympy as sp</code> قطع می‌کند؛ بنابراین این کابینت ایده را بازسازی می‌کند و فرض‌های شکننده را آشکار نشان می‌دهد.',
      bridgeTitle: "از مقالهٔ پیام تا این آزمایشگاه",
      bridgeP1:
        "مقالهٔ پیام یک «ایکس‌ری» برای عددهای بزرگ پیشنهاد می‌کند: به‌جای این‌که فقط بپرسیم عدد اول است یا نه، می‌پرسیم آیا عدد یک ساختار سیکلوتومیک پنهان دارد.",
      bridgeP2:
        "این برنامه همان ایده را به یک ابزار تعاملی تبدیل می‌کند. نامزدهای n را می‌گردد، φ(n) را حساب می‌کند، پایه‌های ممکن را تخمین می‌زند، Φₙ(b) را ارزیابی می‌کند و ردپای شواهد را نشان می‌دهد.",
      bridgeP3:
        "بخش‌های بدبینانه عمدی‌اند: PDF می‌گوید ساختار با آزمون ریشه و جبر نمادین بازیابی می‌شود، اما اسکریپت قابل‌مشاهده کامل نیست. بنابراین کابینت نمونه‌های نقض را هم نشان می‌دهد؛ جاهایی که یک مقدار سیکلوتومیک واقعی از میان‌بُر ریشه عبور نمی‌کند.",
      candidates: "نامزدهای سیکلوتومیک",
      ready: "آماده است. یک نمونه را بارگذاری کنید یا عددی وارد کنید.",
      chamber: "محفظه",
      table: "ماتریس",
      evidenceScore: "امتیاز شواهد",
      signalsCombined: "سیگنال‌های ترکیب‌شده",
      signalExact: "برابری دقیق Φn(b)",
      signalRoot: "نزدیکی ریشه φ(n)",
      signalResidue: "هم‌خوانی باقیمانده‌ها",
      signalGcd: "ساختار gcd(k,n)",
      evidence: "شواهد",
      treasureMap: "نقشه گنج",
      treasureInitial: "ناحیه‌های برتر بعد از اولین اسکن ظاهر می‌شوند.",
      skeletonKey: "کلید استخوانی",
      noCandidate: "هیچ نامزدی انتخاب نشده است.",
      counterexample: "نمونه نقض",
      counterInitial: "نمونه‌های نقض نشان می‌دهند چه زمانی یک مقدار سیکلوتومیک دقیق از آزمون ریشهٔ کاملِ PDF عبور نمی‌کند.",
      verdict: "حکم",
      awaitingScan: "در انتظار اسکن.",
      exportJson: "خروجی گزارش (JSON)",
      auditInitial: "برای پر شدن گزارش حسابرسی، یک اسکن اجرا کنید.",
      stageEmpty: "برای دیدن جزئیات نامزدها یک اسکن اجرا کنید.",
      timelineEmpty: "خط زمانی شواهد خالی است.",
      scanning: "در حال نمایه‌سازی، غربال و راستی‌آزمایی نامزدها...",
      progressStatus: "{stage}: {count}/{total} در {ms} میلی‌ثانیه",
      cancelled: "اسکن لغو شد.",
      scannedStatus: "{count} نامزد در {ms} میلی‌ثانیه اسکن شد.{timeout}",
      timeout: " بودجه زمان تمام شد؛ شواهد جزئی نمایش داده می‌شود.",
      topRegion: "ناحیه برتر: n ≈ {n}؛ بهترین پایه {base}؛ شواهد {verdict}.",
      noSurvivors: "هیچ نامزدی از فیلتر عبور نکرد.",
      score: "امتیاز",
      scoreLower: "امتیاز",
      bestB: "بهترین b",
      testedBases: "پایه‌های آزموده",
      difference: "اختلاف",
      residues: "باقیمانده‌ها",
      discoveryMatrix: "ماتریس کشف",
      rootSignal: "سیگنال ریشه",
      verifyStatus: "وضعیت راستی‌آزمایی",
      label: "برچسب",
      nextAction: "گام بعدی",
      rootTest: "آزمون ریشه",
      notes: "یادداشت‌ها",
      match: "تطابق",
      exact: "دقیق",
      notExact: "دقیق نیست",
      no: "خیر",
      pass: "قبول",
      fragile: "شکننده",
      exactSmall: "دقیق",
      estSmall: "تخمینی",
      withB: "با b = {b} ({kind})",
      candidatePoly: "نامزد Φn(x)",
      phiMatch: "تطابق Φn(b)",
      residueConfidence: "اطمینان باقیمانده‌ها",
      exactHit: "برخورد دقیق Φ{n}({b})",
      exactButFragile: "نامزد دقیق است، اما میان‌بُر ریشهٔ کامل آن را ثابت نمی‌کند.",
      counterPassExplanation: "این مورد از آزمون ریشهٔ کامل هم عبور می‌کند.",
      counterFailExplanation: "مقدار سیکلوتومیک دقیق است، اما توان کامل φ(n) نیست.",
      counterRootLine: "φ(n) = {phi}؛ آزمون ریشه = ",
      passes: "قبول",
      fails: "رد",
      verdictExact: "ساختار سیکلوتومیک دقیق پیدا شد.",
      verdictStrong: "ساختار محتمل پیدا شد.",
      verdictWeak: "سیگنال ساختار ضعیف است.",
      verdictNone: "ساختار قانع‌کننده‌ای دیده نشد.",
      evidenceLabelLine: "برچسب: {label}. راستی‌آزمایی: {status}.",
      bestCandidateLine: "بهترین نامزد: n = {n} با φ(n) = {phi} و b = {base}.",
      confidenceLine: "اطمینان {score}؛ تطابق‌های دقیق {exact}؛ برخوردهای دقیق شکننده {fragile}.",
      noDominant: "سیگنال غالبی نیست",
      chartN: "n",
      languageLabel: "زبان",
      english: "EN",
      persian: "FA"
    }
  };

  const verdictLabels = {
    en: {
      Exact: "Exact",
      "Strong evidence": "Strong evidence",
      "Weak evidence": "Weak evidence",
      "No match": "No match",
      Strong: "Strong",
      Weak: "Weak",
      Filtered: "Filtered",
      Invalid: "Invalid",
      Error: "Error",
      "Timed out": "Timed out",
      "Exact but invalid k": "Exact but invalid k"
    },
    fa: {
      Exact: "دقیق",
      "Strong evidence": "شواهد قوی",
      "Weak evidence": "شواهد ضعیف",
      "No match": "بدون تطابق",
      Strong: "قوی",
      Weak: "ضعیف",
      Filtered: "فیلتر شده",
      Invalid: "نامعتبر",
      Error: "خطا",
      "Timed out": "زمان تمام شد",
      "Exact but invalid k": "دقیق اما k نامعتبر است"
    }
  };

  const noteLabels = {
    en: {},
    fa: {
      "Exact Φn(b) equality": "برابری دقیق Φn(b)",
      "Fails perfect-root shortcut": "از میان‌بُر ریشهٔ کامل عبور نمی‌کند",
      "Exact Φn(b) verification missed": "راستی‌آزمایی دقیق Φn(b) به تطابق نرسید",
      "Screened before exact Φn(b) verification": "پیش از راستی‌آزمایی دقیق Φn(b) غربال شده است",
      "Perfect-root shortcut passes": "میان‌بُر ریشهٔ کامل قبول می‌شود",
      "Root proximity is high": "نزدیکی ریشه بالاست",
      "gcd(k,n) > 1": "gcd(k,n) بزرگ‌تر از ۱ است",
      "Residues mostly agree": "باقیمانده‌ها بیشتر هم‌خوان‌اند",
      "Low evidence score": "امتیاز شواهد پایین است"
    }
  };

  const evidenceLabels = {
    en: {
      exact: "exact",
      "strong evidence": "strong evidence",
      "weak evidence": "weak evidence",
      counterexample: "counterexample",
      "no match": "no match"
    },
    fa: {
      exact: "دقیق",
      "strong evidence": "شواهد قوی",
      "weak evidence": "شواهد ضعیف",
      counterexample: "نمونه نقض",
      "no match": "بدون تطابق"
    }
  };

  const stageLabels = {
    en: {
      profile: "Profile",
      screen: "Screen",
      hypothesize: "Hypothesize",
      verify: "Verify"
    },
    fa: {
      profile: "نمایه",
      screen: "غربال",
      hypothesize: "فرضیه‌سازی",
      verify: "راستی‌آزمایی"
    }
  };

  const stageStatusLabels = {
    en: {
      complete: "complete",
      partial: "partial",
      cancelled: "cancelled"
    },
    fa: {
      complete: "کامل",
      partial: "جزئی",
      cancelled: "لغو شده"
    }
  };

  const verifyStatusLabels = {
    en: {
      screened: "screened",
      "verified-exact": "verified exact",
      "verified-miss": "verified miss",
      error: "error"
    },
    fa: {
      screened: "غربال شده",
      "verified-exact": "دقیق راستی‌آزمایی شد",
      "verified-miss": "راستی‌آزمایی بی‌تطابق",
      error: "خطا"
    }
  };

  const actionLabels = {
    en: {},
    fa: {
      "Verify exact Φn(b) for the strongest tested bases.": "برای قوی‌ترین پایه‌های آزموده‌شده، Φn(b) را دقیق راستی‌آزمایی کنید.",
      "Expand n range or change the base window only if this region matters.": "فقط اگر این ناحیه مهم است، بازه n یا بازه پایه را تغییر دهید.",
      "Treat as exact cyclotomic equality; inspect whether the root shortcut was fragile.": "آن را برابری دقیق سیکلوتومیک بدانید؛ سپس بررسی کنید آیا میان‌بُر ریشه شکننده بوده است.",
      "Keep as evidence only; exact Φn(b) equality was not found for tested bases.": "فقط به‌عنوان شواهد نگه دارید؛ برای پایه‌های آزموده‌شده برابری دقیق Φn(b) پیدا نشد."
    }
  };

  let activeLanguage = normalizeLanguage(
    new URL(window.location.href).searchParams.get("lang") || localStorage.getItem("xray-language") || "en"
  );

  function normalizeLanguage(language) {
    return language === "fa" ? "fa" : "en";
  }

  function currentLanguage() {
    return activeLanguage;
  }

  function t(key, params = {}) {
    const language = currentLanguage();
    const template = dictionaries[language][key] || dictionaries.en[key] || key;
    return template.replace(/\{(\w+)\}/g, (_, name) => params[name] ?? "");
  }

  function verdict(value) {
    const language = currentLanguage();
    return verdictLabels[language][value] || value;
  }

  function evidenceLabel(value) {
    const language = currentLanguage();
    return evidenceLabels[language][value] || verdict(value);
  }

  function stage(value) {
    const language = currentLanguage();
    return stageLabels[language][value] || value;
  }

  function stageStatus(value) {
    const language = currentLanguage();
    return stageStatusLabels[language][value] || value;
  }

  function verifyStatus(value) {
    const language = currentLanguage();
    return verifyStatusLabels[language][value] || value || "n/a";
  }

  function action(value) {
    const language = currentLanguage();
    return actionLabels[language][value] || value || "";
  }

  function note(value) {
    const language = currentLanguage();
    return noteLabels[language][value] || value;
  }

  function setText(selector, key) {
    const node = document.querySelector(selector);
    if (node) node.textContent = t(key);
  }

  function setHtml(selector, key) {
    const node = document.querySelector(selector);
    if (node) node.innerHTML = t(key);
  }

  function setAttr(selector, attr, key) {
    const node = document.querySelector(selector);
    if (node) node.setAttribute(attr, t(key));
  }

  function setIconButton(selector, key, icon) {
    const node = document.querySelector(selector);
    if (node) node.innerHTML = `<span aria-hidden="true">${icon}</span>${t(key)}`;
  }

  function setLanguage(language) {
    const next = normalizeLanguage(language);
    activeLanguage = next;
    localStorage.setItem("xray-language", next);
    applyStatic(next);
  }

  function applyStatic(language = currentLanguage()) {
    const next = normalizeLanguage(language);
    activeLanguage = next;
    document.documentElement.lang = next === "fa" ? "fa-IR" : "en";
    document.documentElement.dir = next === "fa" ? "rtl" : "ltr";
    document.body.classList.toggle("lang-fa", next === "fa");
    document.title = t("appTitle");

    setAttr(".cabinet-shell", "aria-label", "appTitle");
    setAttr(".language-switch", "aria-label", "languageLabel");
    setAttr(".input-rail", "aria-label", "scanControls");
    setAttr(".main-stage", "aria-label", "candidates");
    setAttr(".inspector", "aria-label", "skeletonKey");
    setAttr(".legend-panel", "aria-label", "evidenceScore");

    setText("h1", "appTitle");
    setText(".brand-block p", "subtitle");
    setText(".paper-credit", "credit");
    const engineNode = document.querySelector(".engine-status");
    if (engineNode) engineNode.innerHTML = `<span class="status-dot"></span>${t("engine")}`;
    setIconButton('[data-action="show-docs"]', "docs", "?");
    setIconButton('[data-action="toggle-density"]', "settings", "⚙");
    setText(".input-rail .rail-section:nth-child(1) h2", "input");
    setText('label[for="integer-input"]', "pasteInteger");
    setText('[data-action="clear-input"]', "clear");
    setText("#run-button span:first-child", "run");
    setText("#cancel-button", "cancel");
    setText(".input-rail .rail-section:nth-child(2) h2", "scanControls");
    setText('label[for="n-min"]', "nMin");
    setText('label[for="n-max"]', "nMax");
    setText('label[for="base-window"]', "baseWindow");
    setText('label[for="time-budget"]', "timeBudget");
    setText('label[for="verification-limit"]', "verificationLimit");
    setText('[data-mode="explore"]', "explore");
    setText('[data-mode="deep"]', "deep");
    setText('[data-mode="counterexample"]', "counterMode");
    setText('[data-mode="verify"]', "verify");
    setText(".input-rail .rail-section:nth-child(3) h2", "samples");
    setText('[data-sample="phi21"]', "samplePhi21");
    setText('[data-sample="phi3"]', "samplePhi3");
    setText('[data-sample="phi3large"]', "samplePhi3Large");
    setText('[data-sample="phi5"]', "samplePhi5");
    setText('[data-sample="carmichael"]', "sampleCarmichael");
    setText('[data-sample="primepower"]', "samplePrimePower");
    setText('[data-sample="large"]', "sampleLarge");
    setText(".source-note h2", "sourceNote");
    setHtml(".source-note p", "sourceNoteHtml");
    setText(".paper-bridge h2", "bridgeTitle");
    setText(".paper-bridge p:nth-of-type(1)", "bridgeP1");
    setText(".paper-bridge p:nth-of-type(2)", "bridgeP2");
    setText(".paper-bridge p:nth-of-type(3)", "bridgeP3");
    setText(".stage-header h2", "candidates");
    setText('[data-view="chamber"]', "chamber");
    setText('[data-view="table"]', "table");
    setText(".legend-panel h3:nth-of-type(1)", "evidenceScore");
    setText(".legend-panel h3:nth-of-type(2)", "signalsCombined");
    setText(".legend-panel li:nth-child(1)", "signalExact");
    setText(".legend-panel li:nth-child(2)", "signalRoot");
    setText(".legend-panel li:nth-child(3)", "signalResidue");
    setText(".legend-panel li:nth-child(4)", "signalGcd");
    setText(".strip-title", "evidence");
    setText(".treasure h2", "treasureMap");
    setText(".inspector-panel:nth-child(2) h2", "skeletonKey");
    setText(".counter h2", "counterexample");
    setText(".verdict h2", "verdict");
    setText(".export-button", "exportJson");

    const auditHeaders = [
      "#",
      "n",
      "φ(n)",
      "Base (b est.)",
      "Score",
      "Φn(b) match",
      "gcd(k,n)",
      "Root test",
      "Verdict",
      "Notes"
    ];
    const auditFa = ["#", "n", "φ(n)", "پایه (b تخمینی)", "امتیاز", "تطابق Φn(b)", "gcd(k,n)", "آزمون ریشه", "حکم", "یادداشت‌ها"];
    document.querySelectorAll(".audit-table thead th").forEach((th, index) => {
      th.textContent = next === "fa" ? auditFa[index] : auditHeaders[index];
    });

    document.querySelectorAll("[data-lang]").forEach((button) => {
      const selected = button.dataset.lang === next;
      button.classList.toggle("selected", selected);
      button.setAttribute("aria-pressed", String(selected));
    });
  }

  root.XRayI18n = {
    currentLanguage,
    setLanguage,
    applyStatic,
    t,
    verdict,
    evidenceLabel,
    stage,
    stageStatus,
    verifyStatus,
    action,
    note
  };
})(typeof globalThis !== "undefined" ? globalThis : window);
