#include "search_server.h"
#include "../query/snippet_generator.h"
#include "../tokenizer/tokenizer.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%') {
            if (i + 2 < in.size()) {
                int hexVal = 0;
                std::istringstream iss(in.substr(i + 1, 2));
                if (iss >> std::hex >> hexVal) {
                    out += static_cast<char>(hexVal);
                    i += 2;
                } else {
                    out += '%';
                }
            } else {
                out += '%';
            }
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> parseQueryParams(const std::string& queryStr) {
    std::unordered_map<std::string, std::string> params;
    std::istringstream iss(queryStr);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = urlDecode(pair.substr(0, eq));
            std::string val = urlDecode(pair.substr(eq + 1));
            params[key] = val;
        } else if (!pair.empty()) {
            params[urlDecode(pair)] = "";
        }
    }
    return params;
}

std::string escapeJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b";  break;
            case '\f': o << "\\f";  break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}
} // anonymous namespace

SearchServer::SearchServer(SearchEngine& engine,
                           HNSWIndex* hnswIndex,
                           FlatEmbedIndex* flatIndex,
                           OrtEmbedder* embedder,
                           const std::unordered_map<int, std::string>& docContent)
    : engine_(engine),
      hnswIndex_(hnswIndex),
      flatIndex_(flatIndex),
      embedder_(embedder),
      docContent_(docContent),
      startTime_(std::chrono::steady_clock::now()) {
    prefixTrie_.buildFromCorpus(docContent_);
}

SearchServer::~SearchServer() {
    stop();
}

bool SearchServer::start(int port, bool background) {
    port_ = port;
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        std::cerr << "[SearchServer] Failed to create socket: " << strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[SearchServer] Failed to bind port " << port_ << ": " << strerror(errno) << "\n";
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    if (listen(serverFd_, 128) < 0) {
        std::cerr << "[SearchServer] Failed to listen: " << strerror(errno) << "\n";
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    running_ = true;
    std::cout << "[SearchServer] Listening on http://0.0.0.0:" << port_ << "\n";
    std::cout << "[SearchServer] Endpoints: GET /, GET /search, GET /suggest, GET /metrics, GET /health, POST /click, POST /document, DELETE /document\n";

    if (background) {
        workerThread_ = std::thread(&SearchServer::serverLoop, this, serverFd_);
        return true;
    } else {
        serverLoop(serverFd_);
        return true;
    }
}

void SearchServer::stop() {
    if (running_.exchange(false)) {
        if (serverFd_ >= 0) {
            close(serverFd_);
            serverFd_ = -1;
        }
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
        std::cout << "[SearchServer] Stopped.\n";
    }
}

void SearchServer::serverLoop(int serverFd) {
    while (running_.load()) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (!running_.load()) break;
            continue;
        }

        // Dispatch client handling in a detached thread for multi-threaded serving
        std::thread([this, clientFd]() {
            handleClient(clientFd);
        }).detach();
    }
}

void SearchServer::handleClient(int clientFd) {
    totalRequests_++;
    char buffer[4096];
    ssize_t bytesRead = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        close(clientFd);
        return;
    }
    buffer[bytesRead] = '\0';

    std::string req(buffer);
    std::istringstream iss(req);
    std::string method, path, httpVer;
    iss >> method >> path >> httpVer;

    std::string responseBody;
    std::string contentType = "application/json";
    int statusCode = 200;
    std::string statusText = "OK";

    std::string endpoint = path;
    std::string queryStr;
    auto qPos = path.find('?');
    if (qPos != std::string::npos) {
        endpoint = path.substr(0, qPos);
        queryStr = path.substr(qPos + 1);
    }

    if (endpoint == "/" || endpoint == "/ui") {
        contentType = "text/html; charset=utf-8";
        responseBody = handleWebUI();
    } else if (endpoint == "/health") {
        responseBody = handleHealth();
    } else if (endpoint == "/metrics") {
        contentType = "text/plain; version=0.0.4";
        responseBody = handleMetrics();
    } else if (endpoint == "/suggest" && method == "GET") {
        auto params = parseQueryParams(queryStr);
        std::string q = params["q"];
        int topK = 5;
        if (params.count("k")) {
            try { topK = std::stoi(params["k"]); } catch (...) {}
        }
        responseBody = handleSuggest(q, topK);
    } else if (endpoint == "/search" && method == "GET") {
        auto params = parseQueryParams(queryStr);
        std::string q = params["q"];
        std::string mode = params.count("mode") ? params["mode"] : "hybrid";
        int topK = 10;
        if (params.count("k")) {
            try { topK = std::stoi(params["k"]); } catch (...) {}
        }
        responseBody = handleSearch(q, mode, topK);
    } else if (endpoint == "/click" && method == "POST") {
        auto bodyPos = req.find("\r\n\r\n");
        std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
        responseBody = handleClick(body);
    } else if (endpoint == "/document" && method == "POST") {
        auto bodyPos = req.find("\r\n\r\n");
        std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
        responseBody = handleInsertDocument(body);
    } else if (endpoint == "/document" && (method == "DELETE" || method == "POST")) {
        auto bodyPos = req.find("\r\n\r\n");
        std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
        responseBody = handleDeleteDocument(body);
    } else {
        statusCode = 404;
        statusText = "Not Found";
        responseBody = "{\"error\": \"Endpoint not found\"}";
    }

    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    oss << "Content-Type: " << contentType << "\r\n";
    oss << "Content-Length: " << responseBody.size() << "\r\n";
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "Connection: close\r\n\r\n";
    oss << responseBody;

    std::string resp = oss.str();
    send(clientFd, resp.data(), resp.size(), 0);
    close(clientFd);
}

std::string SearchServer::handleWebUI() {
    std::string html = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Adaptive Search Engine — Neural Hybrid & Stage-2 Re-ranker</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>
  :root {
    --bg-primary: #0a0e17;
    --bg-secondary: #131b2e;
    --bg-card: #1c2742;
    --border: #2a3b63;
    --text-main: #f1f5f9;
    --text-muted: #94a3b8;
    --accent: #3b82f6;
    --accent-hover: #60a5fa;
    --highlight: #fde047;
    --badge-bg: #1e3a8a;
    --badge-text: #93c5fd;
    --success: #10b981;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Inter', -apple-system, sans-serif; }
  body { background: var(--bg-primary); color: var(--text-main); min-height: 100vh; display: flex; flex-direction: column; }
  header { background: var(--bg-secondary); border-bottom: 1px solid var(--border); padding: 1.25rem 2rem; display: flex; justify-content: space-between; align-items: center; }
  .logo { display: flex; align-items: center; gap: 0.75rem; font-size: 1.25rem; font-weight: 700; color: #fff; letter-spacing: -0.5px; }
  .logo span { background: linear-gradient(135deg, #38bdf8, #818cf8); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
  .status-badge { display: flex; align-items: center; gap: 0.5rem; font-size: 0.85rem; padding: 0.35rem 0.85rem; background: rgba(16, 185, 129, 0.1); border: 1px solid rgba(16, 185, 129, 0.3); border-radius: 999px; color: var(--success); font-family: 'JetBrains Mono', monospace; }
  .status-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--success); box-shadow: 0 0 8px var(--success); }
  
  main { max-width: 900px; width: 100%; margin: 0 auto; padding: 2.5rem 1.5rem; flex: 1; }
  
  .search-container { position: relative; margin-bottom: 1.5rem; }
  .search-input { width: 100%; padding: 1.1rem 1.4rem; font-size: 1.1rem; background: var(--bg-secondary); border: 2px solid var(--border); border-radius: 12px; color: #fff; outline: none; transition: all 0.2s ease; box-shadow: 0 4px 20px rgba(0,0,0,0.3); }
  .search-input:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.25); }
  
  .suggest-box { position: absolute; top: 100%; left: 0; right: 0; background: var(--bg-secondary); border: 1px solid var(--border); border-top: none; border-radius: 0 0 12px 12px; z-index: 100; box-shadow: 0 8px 30px rgba(0,0,0,0.5); overflow: hidden; display: none; }
  .suggest-item { padding: 0.75rem 1.4rem; cursor: pointer; display: flex; justify-content: space-between; align-items: center; font-size: 0.95rem; color: #cbd5e1; border-bottom: 1px solid rgba(255,255,255,0.05); }
  .suggest-item:hover, .suggest-item.selected { background: rgba(59, 130, 246, 0.2); color: #fff; }
  .suggest-item:last-child { border-bottom: none; }
  .suggest-freq { font-size: 0.75rem; color: var(--text-muted); font-family: 'JetBrains Mono', monospace; }

  .mode-selector { display: flex; gap: 0.5rem; flex-wrap: wrap; margin-bottom: 2rem; }
  .mode-pill { padding: 0.5rem 1rem; border-radius: 8px; font-size: 0.85rem; font-weight: 600; cursor: pointer; border: 1px solid var(--border); background: var(--bg-secondary); color: var(--text-muted); transition: all 0.15s ease; }
  .mode-pill.active { background: var(--accent); color: #fff; border-color: var(--accent); box-shadow: 0 2px 8px rgba(59, 130, 246, 0.4); }
  
  .metrics-bar { display: flex; justify-content: space-between; align-items: center; font-size: 0.85rem; color: var(--text-muted); margin-bottom: 1.25rem; font-family: 'JetBrains Mono', monospace; }
  
  .results-list { display: flex; flex-direction: column; gap: 1rem; }
  .result-card { background: var(--bg-secondary); border: 1px solid var(--border); border-radius: 10px; padding: 1.25rem; transition: transform 0.15s ease, border-color 0.15s ease; position: relative; }
  .result-card:hover { transform: translateY(-2px); border-color: #3b82f6; }
  .result-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 0.6rem; }
  .doc-badge { background: var(--badge-bg); color: var(--badge-text); font-size: 0.75rem; font-weight: 600; padding: 0.2rem 0.6rem; border-radius: 6px; font-family: 'JetBrains Mono', monospace; }
  .score-badge { font-size: 0.8rem; font-weight: 600; color: #38bdf8; font-family: 'JetBrains Mono', monospace; }
  
  .result-snippet { font-size: 0.95rem; line-height: 1.55; color: #cbd5e1; }
  .result-snippet b { color: var(--highlight); font-weight: 600; }
  
  .click-btn { margin-top: 0.75rem; font-size: 0.75rem; padding: 0.35rem 0.7rem; background: rgba(59,130,246,0.15); border: 1px solid rgba(59,130,246,0.3); border-radius: 6px; color: #93c5fd; cursor: pointer; transition: all 0.15s; font-family: 'JetBrains Mono', monospace; }
  .click-btn:hover { background: rgba(59,130,246,0.3); }
  .click-btn.clicked { background: rgba(16,185,129,0.2); border-color: var(--success); color: var(--success); }

  .add-doc-panel { margin-top: 3rem; background: var(--bg-secondary); border: 1px solid var(--border); border-radius: 12px; padding: 1.5rem; }
  .add-doc-panel h3 { font-size: 1rem; font-weight: 600; margin-bottom: 0.75rem; color: #e2e8f0; }
  .add-form { display: flex; gap: 0.75rem; }
  .add-form input[type="number"] { width: 100px; padding: 0.6rem; background: var(--bg-primary); border: 1px solid var(--border); border-radius: 6px; color: #fff; }
  .add-form input[type="text"] { flex: 1; padding: 0.6rem; background: var(--bg-primary); border: 1px solid var(--border); border-radius: 6px; color: #fff; }
  .add-form button { padding: 0.6rem 1.2rem; background: var(--accent); border: none; border-radius: 6px; color: #fff; font-weight: 600; cursor: pointer; }
</style>
</head>
<body>
<header>
  <div class="logo">⚡ <span>Adaptive Search</span></div>
  <div class="status-badge"><div class="status-dot"></div><span id="docCount">Online</span></div>
</header>
<main>
  <div class="search-container">
    <input type="text" id="queryInput" class="search-input" placeholder="Search across 10,000+ documents (e.g. 'machine learning', 'cloud DevOps', 'cryptography')..." autofocus autocomplete="off">
    <div class="suggest-box" id="suggestBox"></div>
  </div>
  <div class="mode-selector">
    <div class="mode-pill active" data-mode="hybrid">⚡ Hybrid (RRF)</div>
    <div class="mode-pill" data-mode="rerank">🧠 Stage-2 Neural Re-ranker</div>
    <div class="mode-pill" data-mode="wand">📊 WAND Top-K Pruned</div>
    <div class="mode-pill" data-mode="bm25">📖 BM25 Lexical</div>
    <div class="mode-pill" data-mode="phrase">🔍 Phrase (Positional)</div>
  </div>
  <div class="metrics-bar">
    <span id="metricsResult">Type a query to search</span>
    <span id="metricsLatency"></span>
  </div>
  <div class="results-list" id="resultsList"></div>

  <div class="add-doc-panel">
    <h3>⚡ Live Real-Time Document Ingestion (LSM WAL)</h3>
    <form class="add-form" id="addDocForm">
      <input type="number" id="newDocId" placeholder="Doc ID" required value="10001">
      <input type="text" id="newDocContent" placeholder="Document content..." required>
      <button type="submit">Ingest</button>
    </form>
  </div>
</main>
<script>
let currentMode = 'hybrid';
let debounceTimer = null;
let suggestDebounce = null;

async function updateStatus() {
  try {
    const res = await fetch('/health');
    const data = await res.json();
    document.getElementById('docCount').innerText = `${data.totalDocs} docs indexed | v${data.version}`;
  } catch (e) {}
}
updateStatus();

document.querySelectorAll('.mode-pill').forEach(pill => {
  pill.addEventListener('click', () => {
    document.querySelectorAll('.mode-pill').forEach(p => p.classList.remove('active'));
    pill.classList.add('active');
    currentMode = pill.dataset.mode;
    executeSearch();
  });
});

const queryInput = document.getElementById('queryInput');
const suggestBox = document.getElementById('suggestBox');

queryInput.addEventListener('input', () => {
  clearTimeout(debounceTimer);
  debounceTimer = setTimeout(executeSearch, 150);

  clearTimeout(suggestDebounce);
  suggestDebounce = setTimeout(fetchSuggestions, 80);
});

async function fetchSuggestions() {
  const q = queryInput.value.trim();
  if (q.length < 2) {
    suggestBox.style.display = 'none';
    return;
  }
  try {
    const res = await fetch(`/suggest?q=${encodeURIComponent(q)}&k=5`);
    const data = await res.json();
    if (data.suggestions && data.suggestions.length > 0) {
      suggestBox.innerHTML = data.suggestions.map(s => `
        <div class="suggest-item" onclick="selectSuggestion('${s.text.replace(/'/g, "\\'")}')">
          <span>🔍 ${s.text}</span>
          <span class="suggest-freq">${s.frequency} hits</span>
        </div>
      `).join('');
      suggestBox.style.display = 'block';
    } else {
      suggestBox.style.display = 'none';
    }
  } catch (e) {
    suggestBox.style.display = 'none';
  }
}

function selectSuggestion(text) {
  queryInput.value = text;
  suggestBox.style.display = 'none';
  executeSearch();
}

document.addEventListener('click', (e) => {
  if (!e.target.closest('.search-container')) {
    suggestBox.style.display = 'none';
  }
});

async function executeSearch() {
  const q = queryInput.value.trim();
  if (!q) {
    document.getElementById('resultsList').innerHTML = '';
    document.getElementById('metricsResult').innerText = 'Type a query to search';
    document.getElementById('metricsLatency').innerText = '';
    return;
  }

  const t0 = performance.now();
  try {
    const res = await fetch(`/search?q=${encodeURIComponent(q)}&mode=${currentMode}&k=10`);
    const data = await res.json();
    const clientLatency = Math.round((performance.now() - t0) * 10) / 10;
    
    document.getElementById('metricsResult').innerText = `Found ${data.total} results for "${data.query}"`;
    document.getElementById('metricsLatency').innerText = `Server: ${data.latencyUs} µs | Client: ${clientLatency} ms`;

    const list = document.getElementById('resultsList');
    if (data.results.length === 0) {
      list.innerHTML = `<div style="text-align:center; padding: 2rem; color: var(--text-muted);">No matching documents found.</div>`;
      return;
    }

    list.innerHTML = data.results.map((r, idx) => `
      <div class="result-card">
        <div class="result-header">
          <span class="doc-badge">DOC #${r.docId}</span>
          <span class="score-badge">Score: ${r.score}</span>
        </div>
        <div class="result-snippet">${r.snippet || r.content}</div>
        <button class="click-btn" onclick="recordClick(${r.docId}, this)">👍 Relevant (Click feedback)</button>
      </div>
    `).join('');
  } catch (e) {
    document.getElementById('metricsResult').innerText = 'Error executing search';
  }
}

async function recordClick(docId, btn) {
  try {
    await fetch('/click', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({docId})
    });
    btn.classList.add('clicked');
    btn.innerText = '✓ Feedback Recorded (Personalized)';
  } catch (e) {}
}

document.getElementById('addDocForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const docId = parseInt(document.getElementById('newDocId').value);
  const content = document.getElementById('newDocContent').value;
  try {
    await fetch('/document', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({docId, content})
    });
    alert(`Document #${docId} ingested live!`);
    updateStatus();
    executeSearch();
  } catch (err) {
    alert('Error ingesting document');
  }
});
</script>
</body>
</html>)rawhtml";
    return html;
}

std::string SearchServer::handleHealth() {
    auto now = std::chrono::steady_clock::now();
    auto uptimeSec = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();

    std::ostringstream oss;
    oss << "{"
        << "\"status\":\"ok\","
        << "\"version\":\"0.3.0\","
        << "\"totalDocs\":" << engine_.totalDocs() << ","
        << "\"uptimeSeconds\":" << uptimeSec << ","
        << "\"semanticAvailable\":" << ((embedder_ && embedder_->isLoaded()) ? "true" : "false")
        << "}";
    return oss.str();
}

std::string SearchServer::handleMetrics() {
    auto now = std::chrono::steady_clock::now();
    auto uptimeSec = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();

    uint64_t searches = totalSearchRequests_.load();
    uint64_t latency = totalLatencyUs_.load();
    double avgLatencyMs = searches > 0 ? (static_cast<double>(latency) / (searches * 1000.0)) : 0.0;

    std::ostringstream oss;
    oss << "# HELP search_requests_total Total number of HTTP search requests received.\n"
        << "# TYPE search_requests_total counter\n"
        << "search_requests_total " << searches << "\n\n"
        << "# HELP search_latency_microseconds_total Total accumulated search latency in microseconds.\n"
        << "# TYPE search_latency_microseconds_total counter\n"
        << "search_latency_microseconds_total " << latency << "\n\n"
        << "# HELP search_avg_latency_milliseconds Average search query latency.\n"
        << "# TYPE search_avg_latency_milliseconds gauge\n"
        << "search_avg_latency_milliseconds " << std::fixed << std::setprecision(3) << avgLatencyMs << "\n\n"
        << "# HELP search_clicks_total Total recorded user clicks for personalization.\n"
        << "# TYPE search_clicks_total counter\n"
        << "search_clicks_total " << totalClicks_.load() << "\n\n"
        << "# HELP search_indexed_docs Total documents currently indexed.\n"
        << "# TYPE search_indexed_docs gauge\n"
        << "search_indexed_docs " << engine_.totalDocs() << "\n\n"
        << "# HELP search_server_uptime_seconds Server uptime in seconds.\n"
        << "# TYPE search_server_uptime_seconds gauge\n"
        << "search_server_uptime_seconds " << uptimeSec << "\n";
    return oss.str();
}

std::string SearchServer::handleSearch(const std::string& query, const std::string& mode, int topK) {
    if (query.empty()) {
        return "{\"query\":\"\",\"mode\":\"" + mode + "\",\"total\":0,\"results\":[]}";
    }

    totalSearchRequests_++;
    auto t0 = std::chrono::high_resolution_clock::now();

    auto qTokens = Tokenizer::tokenize(query);

    std::ostringstream oss;
    oss << "{\"query\":\"" << escapeJson(query) << "\",\"mode\":\"" << mode << "\",\"results\":[";

    int count = 0;
    bool hybridPossible = (embedder_ && embedder_->isLoaded() && (hnswIndex_ || flatIndex_));

    if (mode == "rerank") {
        // Stage-1: Retrieve Top-50 candidates via Hybrid / BM25
        auto l1Candidates = engine_.search(query, topK * 5);
        auto reranked = crossEncoder_.rerank(query, l1Candidates, topK);

        for (size_t i = 0; i < reranked.size(); ++i) {
            const auto& r = reranked[i];
            if (tombstones_.isDeleted(r.docId)) continue;

            std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":" << std::fixed << std::setprecision(5) << r.score << ","
                << "\"l1Score\":" << std::setprecision(3) << r.l1Score << ","
                << "\"snippet\":\"" << escapeJson(snippet) << "\","
                << "\"content\":\"" << escapeJson(r.content) << "\""
                << "}";
            count++;
            if (count >= topK) break;
        }
    } else if (mode == "hybrid" && hybridPossible) {
        auto lexicalRes = engine_.search(query, topK * 2);
        std::vector<float> qvec = embedder_->encode(query);
        std::vector<std::pair<int, float>> denseRes;

        if (hnswIndex_) {
            auto hnswMatches = hnswIndex_->search(qvec.data(), topK * 2, 50);
            for (const auto& m : hnswMatches) denseRes.emplace_back(m.docId, m.distance);
        } else if (flatIndex_) {
            denseRes = flatIndex_->search(qvec.data(), topK * 2);
        }

        auto fused = rrf_.fuse(lexicalRes, denseRes, topK * 2);
        for (size_t i = 0; i < fused.size(); ++i) {
            const auto& r = fused[i];
            if (tombstones_.isDeleted(r.docId)) continue;

            std::string content = r.content;
            if (content.empty()) {
                auto it = docContent_.find(r.docId);
                if (it != docContent_.end()) content = it->second;
            }
            std::string snippet = SnippetGenerator::generateSnippet(content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":" << std::fixed << std::setprecision(5) << r.rrfScore << ","
                << "\"bm25Score\":" << std::setprecision(3) << r.bm25Score << ","
                << "\"denseScore\":" << std::setprecision(3) << r.denseScore << ","
                << "\"snippet\":\"" << escapeJson(snippet) << "\","
                << "\"content\":\"" << escapeJson(content) << "\""
                << "}";
            count++;
            if (count >= topK) break;
        }
    } else if (mode == "wand") {
        WANDStats stats;
        auto wandRes = engine_.searchWAND(query, topK * 2, &stats);
        for (size_t i = 0; i < wandRes.size(); ++i) {
            const auto& r = wandRes[i];
            if (tombstones_.isDeleted(r.docId)) continue;

            std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":" << std::fixed << std::setprecision(4) << r.score << ","
                << "\"snippet\":\"" << escapeJson(snippet) << "\","
                << "\"content\":\"" << escapeJson(r.content) << "\""
                << "}";
            count++;
            if (count >= topK) break;
        }
    } else if (mode == "phrase") {
        auto phraseRes = engine_.searchPhrase(query);
        for (size_t i = 0; i < phraseRes.size(); ++i) {
            const auto& r = phraseRes[i];
            if (tombstones_.isDeleted(r.docId)) continue;

            std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":1.0,"
                << "\"snippet\":\"" << escapeJson(snippet) << "\","
                << "\"content\":\"" << escapeJson(r.content) << "\""
                << "}";
            count++;
            if (count >= topK) break;
        }
    } else {
        // BM25 default
        auto lexicalRes = engine_.search(query, topK * 2);
        for (size_t i = 0; i < lexicalRes.size(); ++i) {
            const auto& r = lexicalRes[i];
            if (tombstones_.isDeleted(r.docId)) continue;

            std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":" << std::fixed << std::setprecision(4) << r.score << ","
                << "\"snippet\":\"" << escapeJson(snippet) << "\","
                << "\"content\":\"" << escapeJson(r.content) << "\""
                << "}";
            count++;
            if (count >= topK) break;
        }
    }



    auto t1 = std::chrono::high_resolution_clock::now();
    auto latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    totalLatencyUs_ += latencyUs;

    oss << "],\"total\":" << count << ",\"latencyUs\":" << latencyUs << "}";
    return oss.str();
}

std::string SearchServer::handleClick(const std::string& body) {
    // Parse docId from JSON: {"docId": 123}
    auto pos = body.find("\"docId\"");
    if (pos != std::string::npos) {
        auto colon = body.find(':', pos);
        if (colon != std::string::npos) {
            int docId = 0;
            std::istringstream iss(body.substr(colon + 1));
            if (iss >> docId && docId > 0) {
                engine_.recordClick(docId);
                totalClicks_++;
                return "{\"status\":\"ok\",\"clickedDocId\":" + std::to_string(docId) + "}";
            }
        }
    }
    return "{\"error\":\"Invalid docId payload\"}";
}

std::string SearchServer::handleSuggest(const std::string& prefix, int topK) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto suggestions = prefixTrie_.suggest(prefix, topK);
    auto t1 = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    std::ostringstream oss;
    oss << "{\"prefix\":\"" << escapeJson(prefix) << "\",\"latencyUs\":" << us << ",\"suggestions\":[";
    for (size_t i = 0; i < suggestions.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"text\":\"" << escapeJson(suggestions[i].text) << "\",\"frequency\":" << suggestions[i].frequency << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string SearchServer::handleInsertDocument(const std::string& body) {
    // Parse docId and content: {"docId": 123, "content": "..."}
    int docId = 0;
    std::string content;

    auto idPos = body.find("\"docId\"");
    if (idPos != std::string::npos) {
        auto colon = body.find(':', idPos);
        if (colon != std::string::npos) {
            std::istringstream iss(body.substr(colon + 1));
            iss >> docId;
        }
    }

    auto contentPos = body.find("\"content\"");
    if (contentPos != std::string::npos) {
        auto quote1 = body.find('"', contentPos + 9);
        if (quote1 != std::string::npos) {
            auto quote2 = body.find('"', quote1 + 1);
            if (quote2 != std::string::npos) {
                content = body.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
    }

    if (docId > 0 && !content.empty()) {
        engine_.addDocument(docId, content);
        engine_.finalizeIndex();
        docContent_[docId] = content;
        tombstones_.unmarkDeleted(docId);

        // Update PrefixTrie
        auto tokens = Tokenizer::tokenize(content);
        for (const auto& t : tokens) {
            if (t.size() >= 3) prefixTrie_.insert(t, 1);
        }

        return "{\"status\":\"ok\",\"action\":\"inserted\",\"docId\":" + std::to_string(docId) + "}";
    }
    return "{\"error\":\"Invalid document payload (requires docId > 0 and non-empty content)\"}";
}

std::string SearchServer::handleDeleteDocument(const std::string& body) {
    // Parse docId: {"docId": 123}
    auto pos = body.find("\"docId\"");
    if (pos != std::string::npos) {
        auto colon = body.find(':', pos);
        if (colon != std::string::npos) {
            int docId = 0;
            std::istringstream iss(body.substr(colon + 1));
            if (iss >> docId && docId > 0) {
                tombstones_.markDeleted(docId);
                return "{\"status\":\"ok\",\"action\":\"deleted\",\"docId\":" + std::to_string(docId) + "}";
            }
        }
    }
    return "{\"error\":\"Invalid docId payload for deletion\"}";
}

