(function () {
  const I18N = {
    ja: {
      menu_file: "ファイル", menu_help: "ヘルプ",
      act_open: "イメージを開く", act_quit: "終了", act_help: "ヘルプ", act_about: "バージョン情報",
      readonly: "ソース読取専用",
      btn_diagnose: "診断", btn_deep: "消失走査", btn_image: "イメージ保存",
      btn_copy: "フォルダ救出", btn_carve: "カービング", btn_repair_gpt: "GPT修復", btn_cancel: "停止",
      confirm_gpt: "GPTヘッダだけを書き込みます。ファイルは変えません。先にイメージ保存を推奨。続行しますか？",
      lbl_elapsed: "経過", col_name: "名前", col_size: "サイズ", col_flag: "状態",
      col_attr: "名前", col_cur: "現在", col_worst: "最悪", col_thr: "閾値",
      health_good: "正常", health_caution: "注意", health_bad: "異常",
      about_title: "QuickDiskRescue", about_ver: "Ver. 1.0.0",
      about_env: "[開発環境]", about_author: "[制作者]",
      about_env_body: "C++17 (MinGW-w64 / g++) + WebView2 / Win32 生ディスク読取",
      about_author_body: "maktak-105",
      ready: "準備完了",
      bl_title: "BitLocker",
      bl_body: "この区画は暗号化されています。回復パスワード、パスフレーズ、または .bek を入力します。鍵は保存しません。",
      bl_rp: "回復パスワード（48桁）",
      bl_pw: "パスフレーズ",
      bl_bek: ".bek を選ぶ",
      bl_go: "解除",
      bl_cancel: "キャンセル",
    },
    en: {
      menu_file: "File", menu_help: "Help",
      act_open: "Open image", act_quit: "Exit", act_help: "Help", act_about: "About",
      readonly: "Source read-only",
      btn_diagnose: "Diagnose", btn_deep: "Lost-partition scan", btn_image: "Save image",
      btn_copy: "Rescue folder", btn_carve: "Carve", btn_repair_gpt: "Repair GPT", btn_cancel: "Stop",
      confirm_gpt: "This writes GPT headers only, not file contents. Imaging first is recommended. Continue?",
      lbl_elapsed: "Elapsed", col_name: "Name", col_size: "Size", col_flag: "Flags",
      col_attr: "Attribute", col_cur: "Current", col_worst: "Worst", col_thr: "Thresh",
      health_good: "Good", health_caution: "Caution", health_bad: "Bad",
      about_title: "QuickDiskRescue", about_ver: "Ver. 1.0.0",
      about_env: "[Environment]", about_author: "[Author]",
      about_env_body: "C++17 (MinGW-w64 / g++) + WebView2 / Win32 raw disk read",
      about_author_body: "maktak-105",
      ready: "Ready",
      bl_title: "BitLocker",
      bl_body: "This partition is encrypted. Enter the 48-digit recovery password, a passphrase, or a .bek file. The key is not saved.",
      bl_rp: "Recovery password (48 digits)",
      bl_pw: "Passphrase",
      bl_bek: "Choose .bek",
      bl_go: "Unlock",
      bl_cancel: "Cancel",
    }
  };

  let lang = "ja";
  let source = "";
  let partition = 0;
  let selectedPath = "/";
  let t0 = 0;
  let timer = null;
  let awaitingTree = false;
  let skipAutoTree = false;
  let volumeSource = "";
  let blVolume = "";
  let blBek = "";
  let lastParts = [];

  function t(k) { return (I18N[lang] && I18N[lang][k]) || k; }

  function applyI18n() {
    document.querySelectorAll("[data-i18n]").forEach((el) => {
      el.textContent = t(el.getAttribute("data-i18n"));
    });
    document.getElementById("lang-text").textContent = lang === "ja" ? "🌐 English" : "🌐 日本語";
    document.getElementById("status").textContent = t("ready");
  }

  function post(obj) {
    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(obj);
    }
  }

  function send(type, extra) {
    const msg = Object.assign({ type: type }, extra || {});
    post(msg);
  }

  function fmtSize(n) {
    if (n == null) return "—";
    const u = ["B", "KB", "MB", "GB", "TB"];
    let i = 0, x = Number(n);
    while (x >= 1024 && i < u.length - 1) { x /= 1024; i++; }
    return x.toFixed(i ? 1 : 0) + " " + u[i];
  }

  function setStatus(s) { document.getElementById("status").textContent = s; }

  function tickClock() {
    if (!t0) return;
    document.getElementById("time-val").textContent =
      ((performance.now() - t0) / 1000).toFixed(2) + "s";
  }

  function startClock() {
    t0 = performance.now();
    awaitingTree = false;
    if (timer) clearInterval(timer);
    tickClock();
    timer = setInterval(tickClock, 200);
  }

  function stopClock() {
    if (timer) {
      clearInterval(timer);
      timer = null;
    }
    tickClock();
  }

  function renderDisks(payload) {
    const ul = document.getElementById("disk-list");
    ul.innerHTML = "";
    (payload.items || []).forEach((d) => {
      const li = document.createElement("li");
      li.className = "disk-item";
      li.textContent = "Drive" + d.index + (d.letters ? " (" + d.letters + ")" : "") +
        "  " + fmtSize(d.size_bytes) + (d.model ? "  " + d.model : "");
      li.onclick = () => {
        document.querySelectorAll(".disk-item").forEach((x) => x.classList.remove("active"));
        li.classList.add("active");
        source = d.path;
        send("diagnose", { source: source });
        startClock();
      };
      ul.appendChild(li);
    });
  }

  const ATTR_JA = {
    1: "読み取りエラー率", 5: "代替処理済セクタ数", 9: "使用時間", 12: "電源投入回数",
    184: "エンドツーエンドエラー", 187: "報告済回復不能エラー", 188: "コマンドタイムアウト",
    190: "気流温度", 194: "温度", 196: "再配置イベント数", 197: "代替処理待セクタ数",
    198: "回復不可能セクタ数", 199: "UDMA CRCエラー", 7: "シークエラー率", 10: "スピンリトライ"
  };
  const ATTR_EN = {
    1: "Read Error Rate", 5: "Reallocated Sectors", 9: "Power-On Hours", 12: "Power Cycle Count",
    184: "End-to-End Error", 187: "Uncorrectable Errors", 188: "Command Timeout",
    190: "Airflow Temperature", 194: "Temperature", 196: "Reallocation Events", 197: "Pending Sectors",
    198: "Uncorrectable Sectors", 199: "UDMA CRC Error", 7: "Seek Error Rate", 10: "Spin Retry Count"
  };

  function attrName(id) {
    const t = lang === "ja" ? ATTR_JA : ATTR_EN;
    return t[id] || ("ID " + id);
  }

  function renderSmart(s) {
    const head = document.getElementById("smart-head");
    const body = document.getElementById("smart-body");
    body.innerHTML = "";
    if (!s || !s.available) {
      head.textContent = s && s.reason ? ("SMART n/a (" + s.reason + ")") : "SMART n/a";
      return;
    }
    const h = s.health || (s.failing ? "bad" : "good");
    const label = t("health_" + h) || h;
    let extra = "";
    if (s.temp_c != null && s.temp_c >= 0) extra += "<span class='smart-temp'>" + s.temp_c + " °C</span>";
    if (s.power_on_hours != null) extra += "POH " + s.power_on_hours + "h  ";
    if (s.power_on_count != null) extra += "Count " + s.power_on_count + "  ";
    if (s.serial) extra += "SN " + s.serial + "  ";
    if (s.firmware) extra += "FW " + s.firmware + "  ";
    if (s.protocol === "NVMe") {
      extra += "spare " + s.available_spare + "% used " + s.percent_used + "% ";
    }
    if (s.rotation === 1 || s.rotation === 0) extra += "SSD ";
    else if (s.rotation > 1) extra += s.rotation + " RPM ";
    head.innerHTML = "<span class='smart-health " + h + "'>" + label + "</span>" + extra +
      "<div style='margin-top:4px;color:#9ca3af'>" + (s.protocol || "") + " " + (s.identify_model || "") + "</div>";
    (s.attributes || []).forEach((a) => {
      const tr = document.createElement("tr");
      if (a.status === 2) tr.className = "st-bad";
      else if (a.status === 1) tr.className = "st-caution";
      const idh = a.id.toString(16).toUpperCase().padStart(2, "0");
      tr.innerHTML = "<td>" + idh + "</td><td>" + attrName(a.id) + "</td><td>" + a.current +
        "</td><td>" + a.worst + "</td><td>" + (a.threshold != null ? a.threshold : "—") +
        "</td><td>" + a.raw + "</td>";
      body.appendChild(tr);
    });
    if (s.protocol === "NVMe" && !(s.attributes || []).length) {
      const rows = [
        ["Spare", s.available_spare, s.spare_threshold],
        ["Percent Used", s.percent_used, 100],
        ["Media Errors", s.media_errors, 0]
      ];
      rows.forEach((r) => {
        const tr = document.createElement("tr");
        tr.innerHTML = "<td>—</td><td>" + r[0] + "</td><td>" + r[1] + "</td><td>—</td><td>" + r[2] + "</td><td>" + r[1] + "</td>";
        body.appendChild(tr);
      });
    }
  }

  function renderDiag(p) {
    const hw = [];
    hw.push((p.letters ? p.letters + "  " : "") + "size: " + fmtSize(p.size_bytes) + "  geom: " + fmtSize(p.geometry_size_bytes));
    hw.push("sector: " + p.sector_size + " / phys " + p.phys_sector_size + "  bus: " + (p.bus || ""));
    hw.push("model: " + (p.model || "—"));
    renderSmart(p.smart);
    hw.push("GPT primary:" + p.gpt_primary + " crc:" + p.gpt_primary_crc_ok +
            " backup:" + p.gpt_backup + " mismatch:" + p.size_mismatch);
    document.getElementById("hw-pane").textContent = hw.join("\n");
    const pane = document.getElementById("diag-pane");
    pane.textContent = "";
    (p.partitions || []).forEach((x) => {
      const row = document.createElement("div");
      row.className = "part-row";
      const gb = ((x.last_lba - x.first_lba + 1) * 512 / (1024 * 1024 * 1024)).toFixed(2);
      row.textContent = "#" + x.index + " " + (x.role || "") + " " + (x.fs || "") + " " + gb + "GB " + (x.name || "");
      row.onclick = () => {
        partition = x.index;
        pane.querySelectorAll(".part-row").forEach((r) => r.classList.remove("active"));
        row.classList.add("active");
        volumeSource = "";
        if (!source) {
          setStatus("先に左のディスクを選んでください");
          return;
        }
        awaitingTree = true;
        startClock();
        setStatus((x.fs === "BitLocker" ? "BitLocker " : "") + "ツリーを読み込み中…");
        send("tree", { source: source, partition: partition, path: "/" });
      };
      pane.appendChild(row);
    });
    if (p.lost_candidates) {
      const lost = document.createElement("div");
      lost.textContent = "lost: " + p.lost_candidates.length;
      pane.appendChild(lost);
    }
    const parts = p.partitions || [];
    lastParts = parts;
    const dataNtfs = parts.filter((x) => x.fs === "NTFS" && x.role !== "Recovery");
    const recNtfs = parts.filter((x) => x.fs === "NTFS" && x.role === "Recovery");
    const bitlocker = parts.filter((x) => x.fs === "BitLocker");
    const bySize = (arr) => arr.slice().sort((a, b) => (b.last_lba - a.last_lba) - (a.last_lba - a.first_lba))[0];
    skipAutoTree = false;
    let pick = bySize(dataNtfs);
    if (!pick && bitlocker.length) {
      skipAutoTree = true;
      pick = bitlocker[0];
    }
    if (!pick) pick = bySize(recNtfs);
    if (pick) {
      partition = pick.index;
      pane.querySelectorAll(".part-row").forEach((r, i) => {
        if (parts[i] && parts[i].index === partition) r.classList.add("active");
      });
    }
  }

  function setCaret(tr, open) {
    const el = tr.querySelector(".tw-caret");
    if (el) el.textContent = open ? "▼" : "▶";
    tr.dataset.open = open ? "1" : "0";
  }

  function makeTreeRow(node, depth, open) {
    const tr = document.createElement("tr");
    const isDir = !!node.is_dir;
    const path = node.path || "/";
    tr.dataset.path = path;
    tr.dataset.depth = String(depth);
    tr.dataset.dir = isDir ? "1" : "0";
    tr.dataset.open = open ? "1" : "0";
    tr.dataset.loaded = (isDir && !(node.children && node.children.length) && node.has_children !== false) ? "0" : "1";
    if (isDir && node.has_children === false) tr.dataset.loaded = "1";
    const flag = (node.deleted ? "DEL " : "") + (node.in_use === false ? "unused " : "") + (isDir ? "dir" : "file");
    const caret = isDir ? "<span class='tw-caret'>" + (open ? "▼" : "▶") + "</span>" : "<span class='tw-pad'></span>";
    tr.innerHTML = "<td style='padding-left:" + (8 + depth * 16) + "px'>" +
      caret + (isDir ? " 📁 " : " 📄 ") + (node.name || "") + "</td>" +
      "<td>" + fmtSize(node.size) + "</td><td>" + flag + "</td>";
    tr.onclick = (ev) => {
      document.querySelectorAll("#tree-body tr").forEach((x) => x.classList.remove("selected"));
      tr.classList.add("selected");
      selectedPath = path;
      if (isDir && (ev.target.classList.contains("tw-caret") || ev.detail === 2)) {
        toggleFolder(tr);
      }
    };
    return tr;
  }

  function toggleFolder(tr) {
    if (tr.dataset.dir !== "1") return;
    if (tr.dataset.open === "1") {
      const d = Number(tr.dataset.depth);
      let n = tr.nextElementSibling;
      while (n) {
        if (Number(n.dataset.depth) <= d) break;
        const nx = n.nextElementSibling;
        n.style.display = "none";
        n = nx;
      }
      setCaret(tr, false);
      return;
    }
    if (tr.dataset.loaded === "1") {
      const d = Number(tr.dataset.depth);
      let n = tr.nextElementSibling;
      while (n) {
        const nd = Number(n.dataset.depth);
        if (nd <= d) break;
        if (nd === d + 1) n.style.display = "";
        n = n.nextElementSibling;
      }
      setCaret(tr, true);
      return;
    }
    setStatus("フォルダを読み込み中…");
    awaitingTree = true;
    startClock();
    send("tree", { source: copySource(), partition: copyPart(), path: tr.dataset.path });
  }

  function insertChildren(parentPath, children) {
    const tb = document.getElementById("tree-body");
    const parent = Array.from(tb.querySelectorAll("tr")).find((r) => r.dataset.path === parentPath);
    if (!parent) return;
    const depth = Number(parent.dataset.depth) + 1;
    let after = parent;
    (children || []).forEach((c) => {
      const row = makeTreeRow(c, depth, false);
      after.after(row);
      after = row;
    });
    parent.dataset.loaded = "1";
    setCaret(parent, true);
  }

  function renderTree(node, depth) {
    const tb = document.getElementById("tree-body");
    if (depth === 0) tb.innerHTML = "";
    if (!node) return;
    const kids = node.children || [];
    const tr = makeTreeRow(node, depth, true);
    tr.dataset.loaded = "1";
    tb.appendChild(tr);
    kids.forEach((c) => {
      const row = makeTreeRow(c, depth + 1, false);
      tb.appendChild(row);
    });
  }

  function showAbout() {
    const el = document.getElementById("about-content");
    el.innerHTML = "<h2>" + t("about_title") + "</h2><div class='ver'>" + t("about_ver") +
      "</div><div class='sec'>" + t("about_env") + "</div><p>" + t("about_env_body") +
      "</p><div class='sec'>" + t("about_author") + "</div><p>" + t("about_author_body") + "</p>";
    document.getElementById("about-overlay").classList.remove("hidden");
  }

  function md(s) {
    const esc = (t) => t.replace(/&/g, "&amp;").replace(/</g, "&lt;");
    const inline = (t) => esc(t)
      .replace(/`([^`]+)`/g, "<code>$1</code>")
      .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
    const lines = (s || "").replace(/\r\n/g, "\n").split("\n");
    let html = "";
    let i = 0;
    const isRow = (ln) => /^\s*\|.*\|\s*$/.test(ln);
    const isSep = (ln) => /^\s*\|?\s*:?-{2,}/.test(ln);
    const cells = (ln) => ln.replace(/^\s*\|/, "").replace(/\|\s*$/, "").split("|").map((c) => c.trim());
    while (i < lines.length) {
      const ln = lines[i];
      if (isRow(ln) && i + 1 < lines.length && isSep(lines[i + 1])) {
        html += "<table><thead><tr>" + cells(ln).map((c) => "<th>" + inline(c) + "</th>").join("") + "</tr></thead><tbody>";
        i += 2;
        while (i < lines.length && isRow(lines[i])) {
          html += "<tr>" + cells(lines[i]).map((c) => "<td>" + inline(c) + "</td>").join("") + "</tr>";
          i++;
        }
        html += "</tbody></table>";
        continue;
      }
      if (/^[-*] /.test(ln)) {
        html += "<ul>";
        while (i < lines.length && /^[-*] /.test(lines[i])) {
          html += "<li>" + inline(lines[i].slice(2)) + "</li>";
          i++;
        }
        html += "</ul>";
        continue;
      }
      if (/^\d+\. /.test(ln)) {
        html += "<ol>";
        while (i < lines.length && /^\d+\. /.test(lines[i])) {
          html += "<li>" + inline(lines[i].replace(/^\d+\. /, "")) + "</li>";
          i++;
        }
        html += "</ol>";
        continue;
      }
      if (/^#### /.test(ln)) html += "<h4>" + inline(ln.slice(5)) + "</h4>";
      else if (/^### /.test(ln)) html += "<h3>" + inline(ln.slice(4)) + "</h3>";
      else if (/^## /.test(ln)) html += "<h2>" + inline(ln.slice(3)) + "</h2>";
      else if (/^# /.test(ln)) html += "<h1>" + inline(ln.slice(2)) + "</h1>";
      else if (ln.trim() === "") html += "<br>";
      else html += "<p>" + inline(ln) + "</p>";
      i++;
    }
    return html;
  }

  function showBlDialog(volume) {
    blVolume = volume || "";
    blBek = "";
    document.getElementById("bl-bek-path").textContent = "";
    document.getElementById("bl-rp").value = "";
    document.getElementById("bl-pw").value = "";
    document.getElementById("bl-overlay").classList.remove("hidden");
  }

  function hideBlDialog() {
    document.getElementById("bl-overlay").classList.add("hidden");
  }

  function copySource() {
    return volumeSource || source;
  }
  function copyPart() {
    return volumeSource ? 0 : partition;
  }

  function showHelp() {
    const pack = window.HELP_MD || { ja: "", en: "" };
    document.getElementById("help-content").innerHTML = md(lang === "ja" ? pack.ja : pack.en);
    document.getElementById("help-overlay").classList.remove("hidden");
  }

  if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener("message", (ev) => {
      const m = ev.data;
      if (!m || !m.type) return;
      if (m.type === "pong") return;
      if (m.type === "disks") renderDisks(m.payload || {});
      if (m.type === "diagnose_result") {
        renderDiag(m.payload || {});
        if (skipAutoTree) {
          setStatus("BitLocker 区画です。その行をクリックしてください");
          awaitingTree = false;
          stopClock();
        } else {
          setStatus("diagnose done");
          if (source) {
            awaitingTree = true;
            send("tree", { source: source, partition: partition, path: "/" });
          } else {
            awaitingTree = false;
            stopClock();
          }
        }
      }
      if (m.type === "tree_result") {
        const p = m.payload || {};
        if (p.ok === false) {
          const partInfo = lastParts.find((x) => x.index === partition);
          const isBl = p.error === "bitlocker_locked" || (partInfo && partInfo.fs === "BitLocker");
          if (isBl) {
            setStatus("BitLocker: 鍵が必要です");
            showBlDialog(p.volume || (partInfo && partInfo.volume) || "");
          } else {
            setStatus("tree failed: " + (p.error || "unknown"));
          }
          document.getElementById("tree-body").innerHTML = "";
        } else {
          if (p.volume) volumeSource = p.volume;
          const req = p.path || "/";
          if (req && req !== "/") insertChildren(req, (p.root && p.root.children) || []);
          else renderTree(p.root, 0);
          setStatus("tree done (partition " + (p.partition != null ? p.partition : partition) + ")");
        }
        awaitingTree = false;
        stopClock();
      }
      if (m.type === "unlock_result") {
        const p = m.payload || {};
        if (p.ok && p.unlocked) {
          hideBlDialog();
          setStatus("BitLocker unlocked (" + (p.method || "") + ")");
          volumeSource = blVolume;
          awaitingTree = true;
          startClock();
          send("tree", { source: blVolume || source, partition: blVolume ? 0 : partition, path: "/" });
        } else {
          setStatus("BitLocker unlock failed: " + (p.error || "unknown"));
          stopClock();
        }
      }
      if (m.type === "bek_path") {
        blBek = m.path || "";
        document.getElementById("bl-bek-path").textContent = blBek;
      }
      if (m.type === "progress") setStatus((m.message || "") + " " + fmtSize(m.done) + " / " + fmtSize(m.total));
      if (m.type === "copy_result") {
        const p = m.payload || {};
        if (p.ok === false) setStatus("copy failed: " + (p.error || "unknown"));
        else setStatus("copied " + (p.copied || 0) + " failed " + (p.failed || 0));
        stopClock();
      }
      if (m.type === "image_result") { setStatus("image " + JSON.stringify(m.payload || {})); stopClock(); }
      if (m.type === "carve_result") { setStatus("carved " + ((m.payload || {}).count || 0)); stopClock(); }
      if (m.type === "repair_gpt_result") {
        const p = m.payload || {};
        if (p.ok === false) setStatus("GPT repair failed: " + (p.error || "unknown"));
        else if (p.wrote) setStatus("GPT repaired (last usable " + p.old_last_usable + " -> " + p.new_last_usable + ")");
        else setStatus("GPT repair: " + (p.error || (p.need_repair ? "not written" : "already consistent")));
        stopClock();
      }
      if (m.type === "error") {
        setStatus(m.message === "busy" ? "前の作業の終了を待っています" : ("error: " + (m.message || "")));
        awaitingTree = false;
        stopClock();
      }
      if (m.type === "opened") {
        source = m.path;
        startClock();
        send("diagnose", { source: source });
      }
      if (m.type === "save_path") {
        startClock();
        if (m.kind === "image") send("image", { source: source, dest: m.path });
        else if (m.kind === "copy") send("copy_out", { source: copySource(), partition: copyPart(), path: selectedPath, dest: m.path });
        else if (m.kind === "carve") send("carve", { source: source, partition: partition, dest: m.path });
      }
    });
  }

  document.getElementById("btn-lang").onclick = () => { lang = lang === "ja" ? "en" : "ja"; applyI18n(); };
  document.getElementById("btn-diagnose").onclick = () => { send("diagnose", { source: source }); startClock(); };
  document.getElementById("btn-deep-scan").onclick = () => { send("deep_scan", { source: source }); startClock(); };
  document.getElementById("btn-image").onclick = () => send("browse_save", { kind: "image" });
  document.getElementById("btn-copy").onclick = () => send("browse_save", { kind: "copy" });
  document.getElementById("btn-carve").onclick = () => send("browse_save", { kind: "carve" });
  document.getElementById("btn-repair-gpt").onclick = () => {
    if (!source) { setStatus("select a disk first"); return; }
    if (!confirm(t("confirm_gpt"))) return;
    startClock();
    send("repair_gpt", { source: source });
  };
  document.getElementById("btn-cancel").onclick = () => send("cancel");
  function closeAllMenus() {
    document.querySelectorAll(".menu-dropdown").forEach((d) => d.classList.remove("open"));
  }

  document.querySelectorAll(".menu-item").forEach((item) => {
    item.addEventListener("click", (e) => {
      e.stopPropagation();
      const dropdown = item.querySelector(".menu-dropdown");
      const isOpen = dropdown.classList.contains("open");
      closeAllMenus();
      if (!isOpen) dropdown.classList.add("open");
    });
  });
  document.querySelectorAll(".menu-action").forEach((action) => {
    action.addEventListener("click", (e) => {
      e.stopPropagation();
      closeAllMenus();
      const a = action.getAttribute("data-action");
      if (a === "open") send("browse_open");
      if (a === "quit") send("quit");
      if (a === "about") showAbout();
      if (a === "help") showHelp();
    });
  });
  document.addEventListener("click", closeAllMenus);

  document.getElementById("about-close").onclick = () => document.getElementById("about-overlay").classList.add("hidden");
  document.getElementById("help-close").onclick = () => document.getElementById("help-overlay").classList.add("hidden");
  document.getElementById("about-overlay").addEventListener("click", (e) => {
    if (e.target.id === "about-overlay") document.getElementById("about-overlay").classList.add("hidden");
  });
  document.getElementById("help-overlay").addEventListener("click", (e) => {
    if (e.target.id === "help-overlay") document.getElementById("help-overlay").classList.add("hidden");
  });
  document.getElementById("bl-cancel").onclick = hideBlDialog;
  document.getElementById("bl-overlay").addEventListener("click", (e) => {
    if (e.target.id === "bl-overlay") hideBlDialog();
  });
  document.getElementById("bl-bek").onclick = () => send("browse_bek");
  document.getElementById("bl-unlock").onclick = () => {
    if (!blVolume && !source) { setStatus("select a disk first"); return; }
    startClock();
    send("unlock_bitlocker", {
      volume: blVolume,
      recovery_password: document.getElementById("bl-rp").value,
      passphrase: document.getElementById("bl-pw").value,
      bek: blBek
    });
  };

  document.addEventListener("keydown", (e) => {
    if (e.ctrlKey && e.key === "o") { send("browse_open"); e.preventDefault(); }
    if (e.ctrlKey && e.key === "q") { send("quit"); e.preventDefault(); }
  });

  applyI18n();
  send("ping");
  send("list_disks");
})();
