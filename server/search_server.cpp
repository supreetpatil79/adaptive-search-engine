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

    // Auto-generate structured metadata tags based on document text
    for (const auto& pair : docContent_) {
        int docId = pair.first;
        const std::string& text = pair.second;

        DocumentMetadata meta;
        meta.docId = docId;

        // Categorize
        if (text.find("neural") != std::string::npos || text.find("learning") != std::string::npos || text.find("intelligence") != std::string::npos) {
            meta.stringFields["category"] = "AI";
        } else if (text.find("cloud") != std::string::npos || text.find("microservices") != std::string::npos || text.find("docker") != std::string::npos) {
            meta.stringFields["category"] = "Cloud";
        } else if (text.find("security") != std::string::npos || text.find("cryptography") != std::string::npos || text.find("cyber") != std::string::npos) {
            meta.stringFields["category"] = "Security";
        } else if (text.find("genome") != std::string::npos || text.find("dna") != std::string::npos || text.find("biology") != std::string::npos) {
            meta.stringFields["category"] = "Genomics";
        } else if (text.find("quantum") != std::string::npos || text.find("computing") != std::string::npos) {
            meta.stringFields["category"] = "Quantum";
        } else {
            meta.stringFields["category"] = "General";
        }

        // Assign synthetic published year: 2022 + (docId % 4)
        meta.numericFields["year"] = 2022.0 + (docId % 4);

        metadataIndex_.setMetadata(docId, meta);
    }
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
        std::string filterExpr = params.count("filter") ? params["filter"] : "";
        int topK = 10;
        if (params.count("k")) {
            try { topK = std::stoi(params["k"]); } catch (...) {}
        }
        responseBody = handleSearch(q, mode, topK, filterExpr);
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
<html lang="en" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Adaptive Search Engine</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@400;500;700&family=Roboto+Mono:wght@400;500;600&family=Roboto:wght@400;500;700&display=swap" rel="stylesheet">
<style>
  :root[data-theme="dark"] {
    --bg: #202124;
    --surface: #303134;
    --surface-hover: #3c4043;
    --border: #3c4043;
    --border-light: #5f6368;
    --text: #e8eaed;
    --text-muted: #9aa0a6;
    --text-snippet: #bdc1c6;
    --link: #8ab4f8;
    --link-hover: #aecbfa;
    --accent: #8ab4f8;
    --accent-green: #81c995;
    --accent-yellow: #fdd663;
    --accent-red: #f28b82;
    --shadow: 0 1px 6px rgba(0,0,0,0.4);
    --shadow-lg: 0 8px 24px rgba(0,0,0,0.6);
  }
  :root[data-theme="light"] {
    --bg: #ffffff;
    --surface: #f8f9fa;
    --surface-hover: #f1f3f4;
    --border: #dfe1e5;
    --border-light: #dadce0;
    --text: #202124;
    --text-muted: #70757a;
    --text-snippet: #4d5156;
    --link: #1a0dab;
    --link-hover: #15098a;
    --accent: #1a73e8;
    --accent-green: #137333;
    --accent-yellow: #ea8600;
    --accent-red: #d93025;
    --shadow: 0 1px 6px rgba(32,33,36,0.18);
    --shadow-lg: 0 8px 24px rgba(32,33,36,0.18);
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background-color: var(--bg);
    color: var(--text);
    font-family: 'Roboto', -apple-system, sans-serif;
    font-size: 14px;
    line-height: 1.5;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    transition: background-color 0.2s, color 0.2s;
  }

  /* Shared Components */
  .logo-text {
    font-family: 'Google Sans', sans-serif;
    font-weight: 500;
    user-select: none;
    letter-spacing: -0.5px;
  }
  .c-blue { color: #4285f4; }
  .c-red { color: #ea4335; }
  .c-yellow { color: #fbbc05; }
  .c-green { color: #34a853; }
  [data-theme="dark"] .c-blue { color: #8ab4f8; }
  [data-theme="dark"] .c-red { color: #f28b82; }
  [data-theme="dark"] .c-yellow { color: #fdd663; }
  [data-theme="dark"] .c-green { color: #81c995; }

  /* ── VIEW 1: GOOGLE HOME VIEW ───────────────────────────────────────── */
  #homeView {
    flex: 1;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 20px;
    min-height: 100vh;
  }
  .home-header {
    position: absolute;
    top: 16px;
    right: 24px;
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .home-center {
    display: flex;
    flex-direction: column;
    align-items: center;
    max-width: 650px;
    width: 100%;
    margin-top: -60px;
  }
  .home-logo {
    font-size: 56px;
    margin-bottom: 8px;
    display: flex;
    align-items: center;
    gap: 2px;
  }
  .home-subtitle {
    font-size: 13px;
    color: var(--text-muted);
    font-family: 'Google Sans', sans-serif;
    margin-bottom: 28px;
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .version-tag {
    background: var(--surface);
    border: 1px solid var(--border);
    padding: 2px 8px;
    border-radius: 12px;
    font-size: 11px;
    font-family: 'Roboto Mono', monospace;
  }

  .home-omnibar-box {
    width: 100%;
    position: relative;
    margin-bottom: 24px;
  }
  .home-omnibar {
    display: flex;
    align-items: center;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 28px;
    height: 48px;
    padding: 0 18px;
    box-shadow: var(--shadow);
    transition: all 0.2s ease;
  }
  .home-omnibar:hover, .home-omnibar.focused {
    background: var(--surface-hover);
    border-color: transparent;
    box-shadow: var(--shadow-lg);
  }
  .home-omnibar.focused {
    border-radius: 24px 24px 0 0;
  }
  .home-omnibar input {
    flex: 1;
    background: transparent;
    border: none;
    outline: none;
    font-size: 16px;
    color: var(--text);
    font-family: 'Roboto', sans-serif;
    padding: 0 12px;
  }
  .home-omnibar input::placeholder { color: var(--text-muted); }

  .home-buttons {
    display: flex;
    gap: 12px;
    margin-bottom: 28px;
  }
  .g-action-btn {
    background: var(--surface);
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 9px 18px;
    font-family: 'Google Sans', sans-serif;
    font-size: 14px;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .g-action-btn:hover {
    background: var(--surface-hover);
    border-color: var(--border-light);
    transform: translateY(-1px);
  }

  .sample-chips {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
    justify-content: center;
    font-size: 13px;
    color: var(--text-muted);
  }
  .sample-chip {
    padding: 4px 12px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 16px;
    cursor: pointer;
    transition: all 0.15s;
  }
  .sample-chip:hover {
    background: var(--surface-hover);
    color: var(--link);
    border-color: var(--accent);
  }

  .home-footer-chips {
    position: absolute;
    bottom: 20px;
    display: flex;
    gap: 12px;
    font-size: 12px;
    font-family: 'Roboto Mono', monospace;
    color: var(--text-muted);
    flex-wrap: wrap;
    justify-content: center;
  }
  .tech-pill {
    background: var(--surface);
    padding: 4px 10px;
    border-radius: 12px;
    border: 1px solid var(--border);
  }

  /* ── VIEW 2: GOOGLE RESULTS VIEW ────────────────────────────────────── */
  #resultsView {
    display: none;
    flex-direction: column;
    flex: 1;
  }
  
  header.res-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 12px 24px;
    border-bottom: 1px solid var(--border);
    position: sticky;
    top: 0;
    background: var(--bg);
    z-index: 100;
  }
  .res-header-left {
    display: flex;
    align-items: center;
    gap: 24px;
    flex: 1;
    max-width: 820px;
  }
  .res-logo {
    font-size: 24px;
    cursor: pointer;
    white-space: nowrap;
  }
  
  .res-omnibar-box {
    position: relative;
    flex: 1;
  }
  .res-omnibar {
    display: flex;
    align-items: center;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 24px;
    height: 44px;
    padding: 0 16px;
    box-shadow: var(--shadow);
    transition: all 0.15s;
  }
  .res-omnibar:hover, .res-omnibar.focused {
    background: var(--surface-hover);
    box-shadow: var(--shadow-lg);
  }
  .res-omnibar.focused {
    border-radius: 20px 20px 0 0;
  }
  .res-omnibar input {
    flex: 1;
    background: transparent;
    border: none;
    outline: none;
    font-size: 15px;
    color: var(--text);
    padding: 0 10px;
  }

  /* Autocomplete Dropdown */
  .suggest-menu {
    position: absolute;
    top: 100%;
    left: 0;
    right: 0;
    background: var(--surface);
    border-radius: 0 0 20px 20px;
    box-shadow: var(--shadow-lg);
    border: 1px solid var(--border);
    border-top: none;
    z-index: 1000;
    overflow: hidden;
    display: none;
  }
  .suggest-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 9px 18px;
    cursor: pointer;
    font-size: 14px;
    color: var(--text);
  }
  .suggest-row:hover, .suggest-row.active {
    background: var(--surface-hover);
  }
  .suggest-row .match-text b { color: var(--accent); }
  .suggest-row .freq-tag {
    font-size: 11px;
    font-family: 'Roboto Mono', monospace;
    color: var(--text-muted);
  }

  /* Navigation Tabs */
  .res-tabs {
    display: flex;
    gap: 4px;
    padding: 0 160px;
    border-bottom: 1px solid var(--border);
    background: var(--bg);
    overflow-x: auto;
  }
  .res-tab {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 11px 16px;
    color: var(--text-muted);
    font-family: 'Google Sans', sans-serif;
    font-size: 13px;
    font-weight: 500;
    cursor: pointer;
    border-bottom: 3px solid transparent;
    transition: all 0.15s;
    user-select: none;
    white-space: nowrap;
  }
  .res-tab:hover { color: var(--text); }
  .res-tab.active {
    color: var(--accent);
    border-bottom-color: var(--accent);
  }

  /* Filter Bar */
  .res-filters {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 160px;
    border-bottom: 1px solid rgba(255,255,255,0.05);
    background: var(--bg);
    flex-wrap: wrap;
  }
  .filter-label {
    font-size: 12px;
    text-transform: uppercase;
    color: var(--text-muted);
    font-family: 'Google Sans', sans-serif;
    font-weight: 500;
  }
  .filter-chip {
    padding: 3px 10px;
    border-radius: 14px;
    font-size: 12px;
    background: var(--surface);
    border: 1px solid var(--border);
    color: var(--text-muted);
    cursor: pointer;
    transition: all 0.15s;
  }
  .filter-chip:hover {
    background: var(--surface-hover);
    color: var(--text);
  }
  .filter-chip.active {
    background: rgba(138, 180, 248, 0.15);
    border-color: var(--accent);
    color: var(--accent);
    font-weight: 500;
  }

  /* Results 2-Column Layout */
  .res-layout {
    max-width: 1280px;
    width: 100%;
    margin: 0 auto;
    padding: 16px 24px 48px 160px;
    display: grid;
    grid-template-columns: 652px 380px;
    gap: 50px;
    flex: 1;
  }

  .res-stats-line {
    font-size: 12px;
    color: var(--text-muted);
    margin-bottom: 20px;
    font-family: 'Roboto Mono', monospace;
  }

  .res-list {
    display: flex;
    flex-direction: column;
    gap: 26px;
  }

  .res-card {
    display: flex;
    flex-direction: column;
  }
  .res-breadcrumb {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 12px;
    color: var(--text-muted);
    margin-bottom: 4px;
  }
  .res-favicon {
    width: 18px;
    height: 18px;
    border-radius: 50%;
    background: var(--surface-hover);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 10px;
    font-weight: bold;
    color: var(--accent);
  }
  .res-cat-badge {
    padding: 1px 6px;
    background: var(--surface);
    border-radius: 4px;
    font-family: 'Roboto Mono', monospace;
    font-size: 11px;
    color: var(--text-muted);
  }
  .res-title {
    font-family: 'Google Sans', sans-serif;
    font-size: 19px;
    color: var(--link);
    text-decoration: none;
    line-height: 1.35;
    margin-bottom: 6px;
    cursor: pointer;
  }
  .res-title:hover { text-decoration: underline; color: var(--link-hover); }
  .res-snippet {
    font-size: 14px;
    color: var(--text-snippet);
    line-height: 1.58;
    margin-bottom: 8px;
  }
  .res-snippet b {
    color: var(--text);
    font-weight: 600;
  }

  .res-chips {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  }
  .meta-tag {
    font-size: 11px;
    font-family: 'Roboto Mono', monospace;
    padding: 2px 8px;
    border-radius: 12px;
    background: var(--surface);
    border: 1px solid var(--border);
    color: var(--text-muted);
  }
  .meta-tag.score { color: var(--accent); border-color: rgba(138, 180, 248, 0.3); }
  .meta-tag.year { color: var(--accent-yellow); border-color: rgba(253, 214, 99, 0.3); }

  .feedback-btn {
    margin-left: auto;
    background: transparent;
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 2px 10px;
    font-size: 11px;
    color: var(--text-muted);
    cursor: pointer;
    transition: all 0.15s;
  }
  .feedback-btn:hover {
    border-color: var(--accent);
    color: var(--accent);
  }
  .feedback-btn.clicked {
    border-color: var(--accent-green);
    color: var(--accent-green);
    background: rgba(129, 201, 149, 0.12);
  }

  /* Knowledge Panel / Telemetry Sidebar */
  .res-sidebar {
    display: flex;
    flex-direction: column;
    gap: 20px;
  }
  .kg-card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 20px;
    box-shadow: var(--shadow);
  }
  .kg-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 14px;
  }
  .kg-title {
    font-family: 'Google Sans', sans-serif;
    font-size: 15px;
    font-weight: 500;
    color: var(--text);
  }
  .kg-sub {
    font-size: 11px;
    font-family: 'Roboto Mono', monospace;
    color: var(--text-muted);
  }

  .telemetry-boxes {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
    margin-bottom: 16px;
  }
  .tel-box {
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 10px;
  }
  .tel-label {
    font-size: 11px;
    color: var(--text-muted);
    margin-bottom: 2px;
  }
  .tel-val {
    font-size: 16px;
    font-weight: 600;
    font-family: 'Roboto Mono', monospace;
    color: var(--text);
  }

  .ir-flow {
    display: flex;
    flex-direction: column;
    gap: 8px;
    font-size: 12px;
    font-family: 'Roboto Mono', monospace;
    color: var(--text-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 12px;
  }
  .ir-step {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .ir-step.active {
    color: var(--accent);
    font-weight: 500;
  }

  /* Slide-Over Drawer for Document Ingestion */
  .drawer-overlay {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background: rgba(0,0,0,0.6);
    backdrop-filter: blur(2px);
    z-index: 2000;
    display: none;
  }
  .drawer-panel {
    position: fixed;
    top: 0;
    right: 0;
    bottom: 0;
    width: 440px;
    max-width: 100%;
    background: var(--surface);
    border-left: 1px solid var(--border);
    padding: 28px;
    z-index: 2001;
    display: flex;
    flex-direction: column;
    gap: 18px;
    transform: translateX(100%);
    transition: transform 0.25s cubic-bezier(0.16, 1, 0.3, 1);
    box-shadow: var(--shadow-lg);
  }
  .drawer-panel.open {
    transform: translateX(0);
  }
  .drawer-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .drawer-title {
    font-family: 'Google Sans', sans-serif;
    font-size: 18px;
    font-weight: 500;
  }
  .drawer-close {
    background: none;
    border: none;
    font-size: 20px;
    color: var(--text-muted);
    cursor: pointer;
  }
  .drawer-close:hover { color: var(--text); }

  .ingest-field {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .ingest-field label {
    font-size: 12px;
    font-weight: 500;
    color: var(--text-muted);
    font-family: 'Google Sans', sans-serif;
  }
  .g-input {
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 10px 14px;
    color: var(--text);
    font-size: 13px;
    outline: none;
  }
  .g-input:focus { border-color: var(--accent); }
  .g-textarea { min-height: 120px; resize: vertical; font-family: 'Roboto', sans-serif; }
  .primary-btn {
    background: var(--accent);
    color: #202124;
    border: none;
    border-radius: 8px;
    padding: 11px 20px;
    font-family: 'Google Sans', sans-serif;
    font-size: 14px;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.15s;
  }
  .primary-btn:hover { background: #aecbfa; }

  /* Utility icons */
  .icon-btn {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 50%;
    width: 36px;
    height: 36px;
    display: flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;
    color: var(--text);
    transition: all 0.15s;
  }
  .icon-btn:hover { background: var(--surface-hover); }

  @media (max-width: 1100px) {
    .res-tabs, .res-filters, .res-layout { padding-left: 24px; padding-right: 24px; }
    .res-layout { grid-template-columns: 1fr; }
  }
</style>
</head>
<body>

<!-- ══════════════════════════════════════════════════════════════════ -->
<!-- VIEW 1: GOOGLE SEARCH HOME CANVAS                                  -->
<!-- ══════════════════════════════════════════════════════════════════ -->
<div id="homeView">
  <div class="home-header">
    <button class="g-action-btn" onclick="openDrawer()">+ Ingest Document</button>
    <button class="icon-btn" onclick="toggleTheme()" title="Toggle Theme" id="themeBtnHome">
      <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M12 3a9 9 0 1 0 9 9c0-.46-.04-.92-.1-1.36a5.389 5.389 0 0 1-4.4 2.26 5.403 5.403 0 0 1-3.14-9.8c-.44-.06-.9-.1-1.36-.1z"/></svg>
    </button>
  </div>

  <div class="home-center">
    <div class="home-logo logo-text">
      <span class="c-blue">A</span><span class="c-red">d</span><span class="c-yellow">a</span><span class="c-blue">p</span><span class="c-green">t</span><span class="c-red">i</span><span class="c-blue">v</span><span class="c-green">e</span>
      <span style="margin-left:8px; font-weight:400;">Search</span>
    </div>
    <div class="home-subtitle">
      <span>Neural Hybrid & Stage-2 IR Engine</span>
      <span class="version-tag" id="homeVersionTag">v0.3.0</span>
    </div>

    <div class="home-omnibar-box">
      <div class="home-omnibar" id="homeOmnibar">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="var(--text-muted)"><path d="M15.5 14h-.79l-.28-.27C15.41 12.59 16 11.11 16 9.5 16 5.91 13.09 3 9.5 3S3 5.91 3 9.5 5.91 16 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/></svg>
        <input type="text" id="homeInput" placeholder="Search across documents or technical terms (/ to focus)..." autocomplete="off" spellcheck="false">
      </div>
      <div class="suggest-menu" id="homeSuggestMenu"></div>
    </div>

    <div class="home-buttons">
      <button class="g-action-btn" onclick="triggerSearchFromHome()">Adaptive Search</button>
      <button class="g-action-btn" onclick="feelingLucky()">I'm Feeling Lucky</button>
    </div>

    <div class="sample-chips">
      <span>Try searching:</span>
      <span class="sample-chip" onclick="searchSample('machine learning')">machine learning</span>
      <span class="sample-chip" onclick="searchSample('neural networks')">neural networks</span>
      <span class="sample-chip" onclick="searchSample('cloud microservices')">cloud microservices</span>
      <span class="sample-chip" onclick="searchSample('cryptography')">cryptography</span>
      <span class="sample-chip" onclick="searchSample('genomic sequencing')">genomic sequencing</span>
    </div>
  </div>

  <div class="home-footer-chips">
    <span class="tech-pill">HNSW Graph (M=16, ef=200)</span>
    <span class="tech-pill">BM25 + WAND Pruning</span>
    <span class="tech-pill">SIMD SQ8 Quantization</span>
    <span class="tech-pill">Cross-Encoder Neural L2</span>
    <span class="tech-pill">Prefix Trie &lt;6 µs</span>
    <span class="tech-pill">LSM WAL Crash Recovery</span>
  </div>
</div>

<!-- ══════════════════════════════════════════════════════════════════ -->
<!-- VIEW 2: GOOGLE SEARCH RESULTS PAGE                                  -->
<!-- ══════════════════════════════════════════════════════════════════ -->
<div id="resultsView">
  <header class="res-header">
    <div class="res-header-left">
      <div class="res-logo logo-text" onclick="showHomeView()">
        <span class="c-blue">A</span><span class="c-red">d</span><span class="c-yellow">a</span><span class="c-blue">p</span><span class="c-green">t</span><span class="c-red">i</span><span class="c-blue">v</span><span class="c-green">e</span>
      </div>

      <div class="res-omnibar-box">
        <div class="res-omnibar" id="resOmnibar">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="var(--text-muted)"><path d="M15.5 14h-.79l-.28-.27C15.41 12.59 16 11.11 16 9.5 16 5.91 13.09 3 9.5 3S3 5.91 3 9.5 5.91 16 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/></svg>
          <input type="text" id="resInput" placeholder="Search across documents..." autocomplete="off" spellcheck="false">
          <button style="background:none; border:none; color:var(--text-muted); cursor:pointer; font-size:16px;" onclick="clearResSearch()">✕</button>
        </div>
        <div class="suggest-menu" id="resSuggestMenu"></div>
      </div>
    </div>

    <div style="display:flex; align-items:center; gap:12px;">
      <button class="g-action-btn" onclick="openDrawer()">+ Ingest</button>
      <button class="icon-btn" onclick="toggleTheme()" title="Toggle Theme" id="themeBtnRes">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M12 3a9 9 0 1 0 9 9c0-.46-.04-.92-.1-1.36a5.389 5.389 0 0 1-4.4 2.26 5.403 5.403 0 0 1-3.14-9.8c-.44-.06-.9-.1-1.36-.1z"/></svg>
      </button>
    </div>
  </header>

  <!-- Google Search Tabs -->
  <div class="res-tabs">
    <div class="res-tab active" data-mode="hybrid">All (Hybrid RRF)</div>
    <div class="res-tab" data-mode="rerank">Stage-2 Neural (Cross-Encoder)</div>
    <div class="res-tab" data-mode="wand">WAND Top-K Pruned</div>
    <div class="res-tab" data-mode="bm25">BM25 Lexical</div>
    <div class="res-tab" data-mode="phrase">Exact Phrases</div>
  </div>

  <!-- Filter Toolbar -->
  <div class="res-filters">
    <span class="filter-label">Category:</span>
    <span class="filter-chip active" data-cat="">All</span>
    <span class="filter-chip" data-cat="category:AI">AI & ML</span>
    <span class="filter-chip" data-cat="category:Cloud">Cloud</span>
    <span class="filter-chip" data-cat="category:Security">Security</span>
    <span class="filter-chip" data-cat="category:Genomics">Genomics</span>
    <span class="filter-chip" data-cat="category:Quantum">Quantum</span>

    <span class="filter-label" style="margin-left:14px;">Year:</span>
    <span class="filter-chip" data-yr="year:>=2024">≥ 2024</span>
    <span class="filter-chip" data-yr="year:>=2023">≥ 2023</span>
  </div>

  <!-- 2-Column Main Results Area -->
  <main class="res-layout">
    <section>
      <div class="res-stats-line" id="resStats">Searching...</div>
      <div class="res-list" id="resList"></div>
    </section>

    <!-- Knowledge Graph & Telemetry Card -->
    <aside class="res-sidebar">
      <div class="kg-card">
        <div class="kg-header">
          <span class="kg-title">IR Engine Telemetry</span>
          <span class="kg-sub" id="sideLatency">— µs</span>
        </div>

        <div class="telemetry-boxes">
          <div class="tel-box">
            <div class="tel-label">Server Latency</div>
            <div class="tel-val" id="valServerLat">—</div>
          </div>
          <div class="tel-box">
            <div class="tel-label">Roundtrip</div>
            <div class="tel-val" id="valClientLat">—</div>
          </div>
          <div class="tel-box">
            <div class="tel-label">Retrieval Mode</div>
            <div class="tel-val" id="valMode" style="font-size:13px; color:var(--accent);">Hybrid</div>
          </div>
          <div class="tel-box">
            <div class="tel-label">Corpus Size</div>
            <div class="tel-val" id="valDocs">30</div>
          </div>
        </div>

        <div class="ir-flow">
          <div class="ir-step" id="flowToken"><span>1. Tokenizer & Stemmer</span><span>✓</span></div>
          <div class="ir-step" id="flowL1"><span>2. Stage-1 Lexical + HNSW</span><span>✓</span></div>
          <div class="ir-step" id="flowRRF"><span>3. Reciprocal Rank Fusion</span><span>✓</span></div>
          <div class="ir-step" id="flowL2"><span>4. Stage-2 Cross-Encoder</span><span id="flowL2Status">Standby</span></div>
          <div class="ir-step" id="flowFilter"><span>5. Bitset Attribute Filter</span><span id="flowFilterStatus">Pass</span></div>
        </div>
      </div>
    </aside>
  </main>
</div>

<!-- ══════════════════════════════════════════════════════════════════ -->
<!-- SLIDE-OVER INGESTION DRAWER                                         -->
<!-- ══════════════════════════════════════════════════════════════════ -->
<div class="drawer-overlay" id="drawerOverlay" onclick="closeDrawer()"></div>
<div class="drawer-panel" id="drawerPanel">
  <div class="drawer-header">
    <span class="drawer-title">Real-Time Document Ingestion</span>
    <button class="drawer-close" onclick="closeDrawer()">✕</button>
  </div>
  <p style="font-size:13px; color:var(--text-muted);">Insert new text or paste extracted PDF passages. The document is indexed live into the inverted index, prefix trie, and WAL.</p>
  <form id="ingestDrawerForm" style="display:flex; flex-direction:column; gap:14px;">
    <div class="ingest-field">
      <label>Document ID</label>
      <input type="number" id="drawerDocId" class="g-input" value="1001" required>
    </div>
    <div class="ingest-field">
      <label>Category Attribute</label>
      <input type="text" id="drawerDocCat" class="g-input" value="AI" required>
    </div>
    <div class="ingest-field">
      <label>Document Content / Passage</label>
      <textarea id="drawerDocContent" class="g-input g-textarea" placeholder="Paste full document content, abstract, or extracted PDF passage..." required></textarea>
    </div>
    <button type="submit" class="primary-btn">Index into Corpus</button>
  </form>
</div>

<script>
let currentMode = 'hybrid';
let activeCategory = '';
let activeYear = '';
let searchDebounce = null;
let suggestDebounce = null;
let activeSuggestIdx = -1;

const homeView = document.getElementById('homeView');
const resultsView = document.getElementById('resultsView');
const homeInput = document.getElementById('homeInput');
const resInput = document.getElementById('resInput');
const homeOmnibar = document.getElementById('homeOmnibar');
const resOmnibar = document.getElementById('resOmnibar');
const homeSuggestMenu = document.getElementById('homeSuggestMenu');
const resSuggestMenu = document.getElementById('resSuggestMenu');
const resList = document.getElementById('resList');
const resStats = document.getElementById('resStats');

// Focus shortcut '/'
window.addEventListener('keydown', (e) => {
  if (e.key === '/' && document.activeElement.tagName !== 'INPUT' && document.activeElement.tagName !== 'TEXTAREA') {
    e.preventDefault();
    if (resultsView.style.display === 'flex') {
      resInput.focus();
      resInput.select();
    } else {
      homeInput.focus();
      homeInput.select();
    }
  }
});

// Theme Toggle
function toggleTheme() {
  const current = document.documentElement.getAttribute('data-theme');
  const next = current === 'dark' ? 'light' : 'dark';
  document.documentElement.setAttribute('data-theme', next);
  localStorage.setItem('g-theme', next);
}
const savedTheme = localStorage.getItem('g-theme') || 'dark';
document.documentElement.setAttribute('data-theme', savedTheme);

// State transitions
function showResultsView() {
  homeView.style.display = 'none';
  resultsView.style.display = 'flex';
}
function showHomeView() {
  resultsView.style.display = 'none';
  homeView.style.display = 'flex';
  homeInput.value = '';
  resInput.value = '';
  homeInput.focus();
}

// Lucky queries
const luckyQueries = [
  'machine learning',
  'deep learning neural networks',
  'cloud microservices docker',
  'quantum computing algorithms',
  'genomics DNA sequencing',
  'cybersecurity zero-trust cryptography'
];
function feelingLucky() {
  const randomQ = luckyQueries[Math.floor(Math.random() * luckyQueries.length)];
  homeInput.value = randomQ;
  resInput.value = randomQ;
  showResultsView();
  executeSearch();
}

function searchSample(q) {
  homeInput.value = q;
  resInput.value = q;
  showResultsView();
  executeSearch();
}

function triggerSearchFromHome() {
  const q = homeInput.value.trim();
  if (q) {
    resInput.value = q;
    showResultsView();
    executeSearch();
  } else {
    homeInput.focus();
  }
}

function clearResSearch() {
  resInput.value = '';
  resSuggestMenu.style.display = 'none';
  executeSearch();
  resInput.focus();
}

// Drawer Controls
function openDrawer() {
  document.getElementById('drawerOverlay').style.display = 'block';
  document.getElementById('drawerPanel').classList.add('open');
}
function closeDrawer() {
  document.getElementById('drawerOverlay').style.display = 'none';
  document.getElementById('drawerPanel').classList.remove('open');
}

// Status fetch
async function loadStatus() {
  try {
    const res = await fetch('/health');
    const data = await res.json();
    document.getElementById('valDocs').innerText = data.totalDocs;
    document.getElementById('homeVersionTag').innerText = `v${data.version}`;
  } catch (e) {}
}
loadStatus();

// Navigation Tabs
document.querySelectorAll('.res-tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.res-tab').forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
    currentMode = tab.dataset.mode;
    document.getElementById('valMode').innerText = tab.innerText.split(' ')[0];
    executeSearch();
  });
});

// Category Filter Chips
document.querySelectorAll('.filter-chip[data-cat]').forEach(chip => {
  chip.addEventListener('click', () => {
    document.querySelectorAll('.filter-chip[data-cat]').forEach(c => c.classList.remove('active'));
    chip.classList.add('active');
    activeCategory = chip.dataset.cat;
    executeSearch();
  });
});

// Year Filter Chips
document.querySelectorAll('.filter-chip[data-yr]').forEach(chip => {
  chip.addEventListener('click', () => {
    if (chip.classList.contains('active')) {
      chip.classList.remove('active');
      activeYear = '';
    } else {
      document.querySelectorAll('.filter-chip[data-yr]').forEach(c => c.classList.remove('active'));
      chip.classList.add('active');
      activeYear = chip.dataset.yr;
    }
    executeSearch();
  });
});

// Omnibar listeners (Home & Results)
function bindOmnibar(input, omnibar, menu, isHome) {
  input.addEventListener('focus', () => {
    omnibar.classList.add('focused');
    if (input.value.trim().length >= 2) fetchSuggest(input.value.trim(), menu, input);
  });
  input.addEventListener('blur', () => {
    setTimeout(() => {
      omnibar.classList.remove('focused');
      menu.style.display = 'none';
    }, 200);
  });
  input.addEventListener('input', () => {
    const q = input.value;
    if (isHome && q.trim().length > 0) {
      resInput.value = q;
    }
    clearTimeout(searchDebounce);
    searchDebounce = setTimeout(() => {
      if (isHome && q.trim().length > 0) {
        showResultsView();
        resInput.focus();
      }
      executeSearch();
    }, 140);

    clearTimeout(suggestDebounce);
    suggestDebounce = setTimeout(() => fetchSuggest(input.value.trim(), menu, input), 60);
  });
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      menu.style.display = 'none';
      if (isHome) {
        showResultsView();
        resInput.value = homeInput.value;
      }
      executeSearch();
    }
  });
}
bindOmnibar(homeInput, homeOmnibar, homeSuggestMenu, true);
bindOmnibar(resInput, resOmnibar, resSuggestMenu, false);

async function fetchSuggest(q, menu, targetInput) {
  if (q.length < 2) {
    menu.style.display = 'none';
    return;
  }
  try {
    const res = await fetch(`/suggest?q=${encodeURIComponent(q)}&k=5`);
    const data = await res.json();
    if (data.suggestions && data.suggestions.length > 0) {
      menu.innerHTML = data.suggestions.map(s => `
        <div class="suggest-row" onclick="selectSuggest('${s.text.replace(/'/g, "\\'")}')">
          <div class="match-text">🔍 ${highlightPrefix(s.text, q)}</div>
          <span class="freq-tag">${s.frequency}</span>
        </div>
      `).join('');
      menu.style.display = 'block';
    } else {
      menu.style.display = 'none';
    }
  } catch (e) {
    menu.style.display = 'none';
  }
}

function highlightPrefix(text, prefix) {
  if (text.toLowerCase().startsWith(prefix.toLowerCase())) {
    return `<b>${text.substring(0, prefix.length)}</b>${text.substring(prefix.length)}`;
  }
  return text;
}

function selectSuggest(text) {
  homeInput.value = text;
  resInput.value = text;
  homeSuggestMenu.style.display = 'none';
  resSuggestMenu.style.display = 'none';
  showResultsView();
  executeSearch();
}

// Execute Search Engine Query
async function executeSearch() {
  const q = (resultsView.style.display === 'flex' ? resInput.value : homeInput.value).trim();
  if (!q) {
    showHomeView();
    return;
  }

  const filters = [];
  if (activeCategory) filters.push(activeCategory);
  if (activeYear) filters.push(activeYear);
  const filterExpr = filters.join(',');

  const t0 = performance.now();
  try {
    const url = `/search?q=${encodeURIComponent(q)}&mode=${currentMode}&k=10${filterExpr ? `&filter=${encodeURIComponent(filterExpr)}` : ''}`;
    const res = await fetch(url);
    const data = await res.json();
    const clientMs = (performance.now() - t0).toFixed(1);

    // Update telemetry
    document.getElementById('sideLatency').innerText = `${data.latencyUs} µs`;
    document.getElementById('valServerLat').innerText = `${data.latencyUs} µs`;
    document.getElementById('valClientLat').innerText = `${clientMs} ms`;

    document.getElementById('flowL2Status').innerText = currentMode === 'rerank' ? 'Active' : 'Bypassed';
    document.getElementById('flowFilterStatus').innerText = filterExpr ? filterExpr : 'Pass';

    const num = data.results ? data.results.length : 0;
    resStats.innerText = `About ${num} results (${data.latencyUs} µs server latency | ${clientMs} ms roundtrip)`;

    if (!data.results || data.results.length === 0) {
      resList.innerHTML = `
        <div style="padding:40px 0; color:var(--text-muted);">
          <div style="font-size:16px; color:var(--text); margin-bottom:8px;">No matching documents found for <b>"${data.query}"</b></div>
          <p>Suggestions: Verify keyword spelling, try broader concepts, or clear active category filters.</p>
        </div>`;
      return;
    }

    resList.innerHTML = data.results.map((r, i) => {
      const category = r.category || 'General';
      const year = r.year || 2024;
      const title = formatTitle(r.content, r.docId);

      return `
        <article class="res-card">
          <div class="res-breadcrumb">
            <div class="res-favicon">${r.docId}</div>
            <span>https://corpus.internal</span>
            <span>›</span>
            <span class="res-cat-badge">${category}</span>
            <span>›</span>
            <span>doc_${r.docId}</span>
          </div>
          <a class="res-title" onclick="giveFeedback(${r.docId}, this)">${title}</a>
          <div class="res-snippet">${r.snippet || r.content}</div>
          <div class="res-chips">
            <span class="meta-tag score">Score: ${r.score}</span>
            ${r.bm25Score ? `<span class="meta-tag">BM25: ${r.bm25Score}</span>` : ''}
            ${r.denseScore ? `<span class="meta-tag">Dense: ${r.denseScore}</span>` : ''}
            ${r.l1Score ? `<span class="meta-tag">L1: ${r.l1Score}</span>` : ''}
            <span class="meta-tag year">Year: ${year}</span>
            <button class="feedback-btn" onclick="giveFeedback(${r.docId}, this)">★ Relevant</button>
          </div>
        </article>
      `;
    }).join('');

  } catch (err) {
    resStats.innerText = 'Error connecting to search engine';
  }
}

function formatTitle(content, docId) {
  if (!content) return `Document #${docId}`;
  const sentence = content.split('.')[0].trim();
  if (sentence.length > 5 && sentence.length < 85) return sentence;
  return content.substring(0, 75) + '...';
}

async function giveFeedback(docId, el) {
  try {
    await fetch('/click', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({docId})
    });
    const btn = el.closest('.res-card').querySelector('.feedback-btn');
    if (btn) {
      btn.classList.add('clicked');
      btn.innerText = '✓ Personalized';
    }
  } catch (e) {}
}

// Drawer Ingestion
document.getElementById('ingestDrawerForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const docId = parseInt(document.getElementById('drawerDocId').value);
  const content = document.getElementById('drawerDocContent').value.trim();
  if (!content) return;

  try {
    await fetch('/document', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({docId, content})
    });
    document.getElementById('drawerDocContent').value = '';
    document.getElementById('drawerDocId').value = docId + 1;
    closeDrawer();
    loadStatus();
    if (resultsView.style.display === 'flex') executeSearch();
  } catch (err) {
    alert('Failed to index document');
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

std::string SearchServer::handleSearch(const std::string& query, const std::string& mode, int topK, const std::string& filterExpr) {
    if (query.empty()) {
        return "{\"query\":\"\",\"mode\":\"" + mode + "\",\"total\":0,\"results\":[]}";
    }

    totalSearchRequests_++;
    auto t0 = std::chrono::high_resolution_clock::now();

    auto qTokens = Tokenizer::tokenize(query);
    MetadataBitset filterMask = metadataIndex_.evaluateFilter(filterExpr, engine_.totalDocs());

    std::ostringstream oss;
    oss << "{\"query\":\"" << escapeJson(query) << "\",\"mode\":\"" << mode << "\",\"filter\":\"" << escapeJson(filterExpr) << "\",\"results\":[";

    int count = 0;
    bool hybridPossible = (embedder_ && embedder_->isLoaded() && (hnswIndex_ || flatIndex_));

    if (mode == "rerank") {
        // Stage-1: Retrieve Top-50 candidates via Hybrid / BM25
        auto l1Candidates = engine_.search(query, topK * 5);
        auto reranked = crossEncoder_.rerank(query, l1Candidates, topK * 2);

        for (size_t i = 0; i < reranked.size(); ++i) {
            const auto& r = reranked[i];
            if (tombstones_.isDeleted(r.docId) || !filterMask.test(r.docId)) continue;

            DocumentMetadata meta;
            metadataIndex_.getMetadata(r.docId, &meta);
            std::string cat = meta.stringFields.count("category") ? meta.stringFields["category"] : "General";
            int year = meta.numericFields.count("year") ? static_cast<int>(meta.numericFields["year"]) : 2024;

            std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":" << std::fixed << std::setprecision(5) << r.score << ","
                << "\"l1Score\":" << std::setprecision(3) << r.l1Score << ","
                << "\"category\":\"" << cat << "\","
                << "\"year\":" << year << ","
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
            auto hnswMatches = hnswIndex_->searchFiltered(qvec.data(), topK * 2, [&](int d) {
                return filterMask.test(d) && !tombstones_.isDeleted(d);
            }, 50);
            for (const auto& m : hnswMatches) denseRes.emplace_back(m.docId, m.distance);
        } else if (flatIndex_) {
            auto rawDense = flatIndex_->search(qvec.data(), topK * 4);
            for (const auto& p : rawDense) {
                if (filterMask.test(p.first) && !tombstones_.isDeleted(p.first)) {
                    denseRes.push_back(p);
                }
            }
        }

        auto fused = rrf_.fuse(lexicalRes, denseRes, topK * 2);
        for (size_t i = 0; i < fused.size(); ++i) {
            const auto& r = fused[i];
            if (tombstones_.isDeleted(r.docId) || !filterMask.test(r.docId)) continue;

            DocumentMetadata meta;
            metadataIndex_.getMetadata(r.docId, &meta);
            std::string cat = meta.stringFields.count("category") ? meta.stringFields["category"] : "General";
            int year = meta.numericFields.count("year") ? static_cast<int>(meta.numericFields["year"]) : 2024;

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
                << "\"category\":\"" << cat << "\","
                << "\"year\":" << year << ","
                << "\"snippet\":\"" << escapeJson(snippet) << "\","
                << "\"content\":\"" << escapeJson(content) << "\""
                << "}";
            count++;
            if (count >= topK) break;
        }
    } else if (mode == "wand") {
        WANDStats stats;
        auto wandRes = engine_.searchWAND(query, topK * 3, &stats);
        for (size_t i = 0; i < wandRes.size(); ++i) {
            const auto& r = wandRes[i];
            if (tombstones_.isDeleted(r.docId) || !filterMask.test(r.docId)) continue;

            DocumentMetadata meta;
            metadataIndex_.getMetadata(r.docId, &meta);
            std::string cat = meta.stringFields.count("category") ? meta.stringFields["category"] : "General";
            int year = meta.numericFields.count("year") ? static_cast<int>(meta.numericFields["year"]) : 2024;

            std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":" << std::fixed << std::setprecision(4) << r.score << ","
                << "\"category\":\"" << cat << "\","
                << "\"year\":" << year << ","
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
            if (tombstones_.isDeleted(r.docId) || !filterMask.test(r.docId)) continue;

            DocumentMetadata meta;
            metadataIndex_.getMetadata(r.docId, &meta);
            std::string cat = meta.stringFields.count("category") ? meta.stringFields["category"] : "General";
            int year = meta.numericFields.count("year") ? static_cast<int>(meta.numericFields["year"]) : 2024;

            std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":1.0,"
                << "\"category\":\"" << cat << "\","
                << "\"year\":" << year << ","
                << "\"snippet\":\"" << escapeJson(snippet) << "\","
                << "\"content\":\"" << escapeJson(r.content) << "\""
                << "}";
            count++;
            if (count >= topK) break;
        }
    } else {
        // BM25 default
        auto lexicalRes = engine_.search(query, topK * 3);
        for (size_t i = 0; i < lexicalRes.size(); ++i) {
            const auto& r = lexicalRes[i];
            if (tombstones_.isDeleted(r.docId) || !filterMask.test(r.docId)) continue;

            DocumentMetadata meta;
            metadataIndex_.getMetadata(r.docId, &meta);
            std::string cat = meta.stringFields.count("category") ? meta.stringFields["category"] : "General";
            int year = meta.numericFields.count("year") ? static_cast<int>(meta.numericFields["year"]) : 2024;

            std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 160, HighlightFormat::HTML);
            if (count > 0) oss << ",";
            oss << "{"
                << "\"docId\":" << r.docId << ","
                << "\"score\":" << std::fixed << std::setprecision(4) << r.score << ","
                << "\"category\":\"" << cat << "\","
                << "\"year\":" << year << ","
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

