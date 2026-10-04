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
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Adaptive Search Engine</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@400;500;700&family=Roboto+Mono:wght@400;500&family=Roboto:wght@400;500&display=swap" rel="stylesheet">
<style>
  :root {
    --g-bg: #202124;
    --g-surface: #303134;
    --g-surface-hover: #3c4043;
    --g-border: #3c4043;
    --g-border-subtle: #5f6368;
    --g-text: #e8eaed;
    --g-text-muted: #9aa0a6;
    --g-text-snippet: #bdc1c6;
    --g-link: #8ab4f8;
    --g-link-visited: #c58af9;
    --g-accent: #8ab4f8;
    --g-accent-green: #81c995;
    --g-accent-yellow: #fdd663;
    --g-accent-red: #f28b82;
    --g-badge-bg: #303134;
    --font-main: 'Google Sans', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    --font-body: 'Roboto', -apple-system, sans-serif;
    --font-mono: 'Roboto Mono', 'SF Mono', Consolas, monospace;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background-color: var(--g-bg);
    color: var(--g-text);
    font-family: var(--font-body);
    font-size: 14px;
    line-height: 1.5;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    overflow-x: hidden;
  }
  
  /* Top Bar */
  header.g-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 12px 24px;
    border-bottom: 1px solid var(--g-border);
    position: sticky;
    top: 0;
    background-color: var(--g-bg);
    z-index: 100;
  }
  .g-header-left {
    display: flex;
    align-items: center;
    gap: 16px;
    flex: 1;
    max-width: 780px;
  }
  .g-logo {
    display: flex;
    align-items: center;
    gap: 8px;
    font-family: var(--font-main);
    font-size: 20px;
    font-weight: 500;
    color: #fff;
    cursor: pointer;
    white-space: nowrap;
    user-select: none;
  }
  .g-logo .logo-blue { color: #8ab4f8; }
  .g-logo .logo-red { color: #f28b82; }
  .g-logo .logo-yellow { color: #fdd663; }
  .g-logo .logo-green { color: #81c995; }
  .g-logo .logo-badge {
    font-size: 11px;
    font-weight: 500;
    padding: 2px 6px;
    background: #3c4043;
    border-radius: 4px;
    color: #9aa0a6;
    margin-left: 4px;
    font-family: var(--font-mono);
  }

  /* Omnibar */
  .g-omnibar-wrapper {
    position: relative;
    flex: 1;
  }
  .g-omnibar {
    display: flex;
    align-items: center;
    background: var(--g-surface);
    border: 1px solid transparent;
    border-radius: 24px;
    padding: 0 16px;
    height: 44px;
    box-shadow: 0 1px 6px rgba(0,0,0,0.28);
    transition: background 0.15s, box-shadow 0.15s, border-radius 0.15s;
  }
  .g-omnibar:hover {
    background: var(--g-surface-hover);
    box-shadow: 0 2px 8px rgba(0,0,0,0.38);
  }
  .g-omnibar.focused {
    background: var(--g-surface);
    border-color: transparent;
    box-shadow: 0 2px 10px rgba(0,0,0,0.5);
    border-radius: 24px 24px 0 0;
  }
  .g-search-icon {
    width: 18px;
    height: 18px;
    fill: var(--g-text-muted);
    margin-right: 12px;
    flex-shrink: 0;
  }
  .g-search-input {
    flex: 1;
    background: transparent;
    border: none;
    outline: none;
    color: var(--g-text);
    font-size: 15px;
    font-family: var(--font-body);
  }
  .g-search-input::placeholder {
    color: var(--g-text-muted);
  }
  .g-clear-btn {
    background: none;
    border: none;
    color: var(--g-text-muted);
    cursor: pointer;
    font-size: 16px;
    padding: 4px 8px;
    display: none;
  }
  .g-clear-btn:hover { color: var(--g-text); }

  /* Autocomplete dropdown */
  .g-suggest-dropdown {
    position: absolute;
    top: 44px;
    left: 0;
    right: 0;
    background: var(--g-surface);
    border-radius: 0 0 24px 24px;
    box-shadow: 0 8px 16px rgba(0,0,0,0.5);
    overflow: hidden;
    z-index: 1000;
    display: none;
    border-top: 1px solid var(--g-border);
  }
  .g-suggest-item {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 18px;
    cursor: pointer;
    font-size: 14px;
    color: var(--g-text);
  }
  .g-suggest-item:hover, .g-suggest-item.active {
    background: var(--g-surface-hover);
  }
  .g-suggest-left {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .g-suggest-left svg {
    width: 14px;
    height: 14px;
    fill: var(--g-text-muted);
  }
  .g-suggest-freq {
    font-size: 11px;
    color: var(--g-text-muted);
    font-family: var(--font-mono);
  }

  /* Header Right */
  .g-header-right {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .g-telemetry-chip {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 4px 10px;
    background: rgba(129, 201, 149, 0.12);
    border: 1px solid rgba(129, 201, 149, 0.3);
    border-radius: 16px;
    font-size: 12px;
    font-family: var(--font-mono);
    color: var(--g-accent-green);
  }
  .g-pulse-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--g-accent-green);
    box-shadow: 0 0 6px var(--g-accent-green);
  }

  /* Navigation Tabs */
  .g-nav-tabs {
    display: flex;
    gap: 4px;
    padding: 0 24px;
    background: var(--g-bg);
    border-bottom: 1px solid var(--g-border);
    overflow-x: auto;
    scrollbar-width: none;
  }
  .g-nav-tab {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 10px 14px;
    color: var(--g-text-muted);
    font-family: var(--font-main);
    font-size: 13px;
    font-weight: 500;
    cursor: pointer;
    border-bottom: 3px solid transparent;
    transition: color 0.15s, border-color 0.15s;
    white-space: nowrap;
    user-select: none;
  }
  .g-nav-tab:hover {
    color: var(--g-text);
  }
  .g-nav-tab.active {
    color: var(--g-accent);
    border-bottom-color: var(--g-accent);
  }

  /* Filter Toolbar */
  .g-filter-bar {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 24px;
    background: var(--g-bg);
    border-bottom: 1px solid rgba(255,255,255,0.05);
    flex-wrap: wrap;
  }
  .g-filter-label {
    font-size: 12px;
    color: var(--g-text-muted);
    text-transform: uppercase;
    font-weight: 500;
    margin-right: 4px;
    font-family: var(--font-main);
  }
  .g-filter-pill {
    padding: 4px 10px;
    border-radius: 16px;
    font-size: 12px;
    color: var(--g-text-muted);
    background: var(--g-surface);
    border: 1px solid var(--g-border);
    cursor: pointer;
    transition: all 0.15s;
    user-select: none;
  }
  .g-filter-pill:hover {
    background: var(--g-surface-hover);
    color: var(--g-text);
  }
  .g-filter-pill.active {
    background: rgba(138, 180, 248, 0.15);
    border-color: var(--g-accent);
    color: var(--g-accent);
    font-weight: 500;
  }

  /* Main Container (Two Columns) */
  .g-layout {
    max-width: 1280px;
    width: 100%;
    margin: 0 auto;
    padding: 16px 24px 48px 24px;
    display: grid;
    grid-template-columns: minmax(0, 680px) 380px;
    gap: 40px;
    flex: 1;
  }

  /* Left Column: Results */
  .g-results-pane {
    display: flex;
    flex-direction: column;
  }
  .g-stats-line {
    font-size: 12px;
    color: var(--g-text-muted);
    margin-bottom: 18px;
    font-family: var(--font-mono);
  }

  .g-results-list {
    display: flex;
    flex-direction: column;
    gap: 24px;
  }

  .g-result-item {
    display: flex;
    flex-direction: column;
  }
  .g-result-meta {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 4px;
  }
  .g-result-favicon {
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background: var(--g-surface-hover);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 9px;
    font-weight: 700;
    color: var(--g-accent);
  }
  .g-result-source {
    font-size: 12px;
    color: var(--g-text-muted);
    display: flex;
    align-items: center;
    gap: 4px;
  }
  .g-result-category {
    padding: 1px 6px;
    border-radius: 4px;
    background: var(--g-surface);
    font-size: 11px;
    color: #9aa0a6;
    font-family: var(--font-mono);
  }
  .g-result-title {
    font-family: var(--font-main);
    font-size: 18px;
    font-weight: 400;
    color: var(--g-link);
    text-decoration: none;
    margin-bottom: 6px;
    cursor: pointer;
    line-height: 1.35;
  }
  .g-result-title:hover {
    text-decoration: underline;
  }
  .g-result-snippet {
    font-size: 14px;
    color: var(--g-text-snippet);
    line-height: 1.58;
    margin-bottom: 8px;
  }
  .g-result-snippet b {
    color: #fff;
    font-weight: 600;
  }

  .g-chips-row {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  }
  .g-chip {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    font-size: 11px;
    font-family: var(--font-mono);
    padding: 2px 8px;
    border-radius: 12px;
    background: var(--g-surface);
    color: var(--g-text-muted);
    border: 1px solid var(--g-border);
  }
  .g-chip.score { color: var(--g-accent); border-color: rgba(138, 180, 248, 0.3); }
  .g-chip.year { color: var(--g-accent-yellow); border-color: rgba(253, 214, 99, 0.3); }

  .g-feedback-btn {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    font-size: 11px;
    font-family: var(--font-main);
    padding: 2px 10px;
    border-radius: 12px;
    background: transparent;
    border: 1px solid var(--g-border);
    color: var(--g-text-muted);
    cursor: pointer;
    transition: all 0.15s;
    margin-left: auto;
  }
  .g-feedback-btn:hover {
    border-color: var(--g-accent);
    color: var(--g-accent);
    background: rgba(138, 180, 248, 0.08);
  }
  .g-feedback-btn.clicked {
    border-color: var(--g-accent-green);
    color: var(--g-accent-green);
    background: rgba(129, 201, 149, 0.15);
  }

  /* Right Column: Knowledge Panel / Telemetry & Ingestion */
  .g-sidebar-pane {
    display: flex;
    flex-direction: column;
    gap: 20px;
  }

  .g-card {
    background: var(--g-surface);
    border: 1px solid var(--g-border);
    border-radius: 12px;
    padding: 18px;
  }
  .g-card-title {
    font-family: var(--font-main);
    font-size: 14px;
    font-weight: 500;
    color: #fff;
    margin-bottom: 12px;
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .g-card-title .card-sub {
    font-size: 11px;
    font-family: var(--font-mono);
    color: var(--g-text-muted);
    font-weight: normal;
  }

  .g-telemetry-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
    margin-bottom: 14px;
  }
  .g-metric-box {
    background: rgba(0,0,0,0.25);
    border: 1px solid rgba(255,255,255,0.06);
    border-radius: 8px;
    padding: 10px;
  }
  .g-metric-label {
    font-size: 11px;
    color: var(--g-text-muted);
    font-family: var(--font-main);
    margin-bottom: 2px;
  }
  .g-metric-value {
    font-size: 16px;
    font-weight: 600;
    font-family: var(--font-mono);
    color: var(--g-text);
  }

  .g-pipeline-flow {
    display: flex;
    flex-direction: column;
    gap: 6px;
    font-size: 12px;
    font-family: var(--font-mono);
    color: var(--g-text-muted);
    background: rgba(0,0,0,0.25);
    border-radius: 8px;
    padding: 10px;
  }
  .g-flow-step {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .g-flow-step.active {
    color: var(--g-accent);
    font-weight: 500;
  }

  /* Ingestion Card */
  .g-ingest-form {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .g-form-row {
    display: flex;
    gap: 8px;
  }
  .g-input {
    background: rgba(0,0,0,0.25);
    border: 1px solid var(--g-border);
    border-radius: 6px;
    padding: 8px 12px;
    color: var(--g-text);
    font-size: 13px;
    font-family: var(--font-body);
    outline: none;
  }
  .g-input:focus { border-color: var(--g-accent); }
  .g-input.id-input { width: 90px; font-family: var(--font-mono); }
  .g-input.cat-input { width: 110px; }
  .g-textarea {
    width: 100%;
    min-height: 70px;
    resize: vertical;
  }
  .g-btn {
    background: var(--g-accent);
    color: #202124;
    border: none;
    border-radius: 6px;
    padding: 8px 14px;
    font-family: var(--font-main);
    font-size: 13px;
    font-weight: 500;
    cursor: pointer;
    transition: background 0.15s;
    align-self: flex-start;
  }
  .g-btn:hover { background: #aecbfa; }

  /* Empty State */
  .g-empty-state {
    padding: 40px 0;
    text-align: center;
    color: var(--g-text-muted);
  }
  .g-empty-title {
    font-family: var(--font-main);
    font-size: 16px;
    margin-bottom: 6px;
    color: var(--g-text);
  }

  @media (max-width: 900px) {
    .g-layout {
      grid-template-columns: 1fr;
    }
  }
</style>
</head>
<body>

<header class="g-header">
  <div class="g-header-left">
    <div class="g-logo" onclick="resetSearch()">
      <span class="logo-blue">A</span><span class="logo-red">d</span><span class="logo-yellow">a</span><span class="logo-blue">p</span><span class="logo-green">t</span><span class="logo-red">i</span><span class="logo-blue">v</span><span class="logo-green">e</span>
      <span style="color:#e8eaed; margin-left:4px; font-weight:400;">Search</span>
      <span class="logo-badge" id="engineVersion">v0.3.0</span>
    </div>

    <div class="g-omnibar-wrapper">
      <div class="g-omnibar" id="omnibar">
        <svg class="g-search-icon" viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27C15.41 12.59 16 11.11 16 9.5 16 5.91 13.09 3 9.5 3S3 5.91 3 9.5 5.91 16 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/></svg>
        <input type="text" id="queryInput" class="g-search-input" placeholder="Search across documents or query terms (/ to focus)..." autofocus autocomplete="off" spellcheck="false">
        <button class="g-clear-btn" id="clearBtn" onclick="clearSearch()">✕</button>
      </div>
      <div class="g-suggest-dropdown" id="suggestDropdown"></div>
    </div>
  </div>

  <div class="g-header-right">
    <div class="g-telemetry-chip">
      <div class="g-pulse-dot"></div>
      <span id="headerDocCount">30 docs indexed</span>
    </div>
  </div>
</header>

<div class="g-nav-tabs">
  <div class="g-nav-tab active" data-mode="hybrid">All (Hybrid RRF)</div>
  <div class="g-nav-tab" data-mode="rerank">Stage-2 Neural (Cross-Encoder)</div>
  <div class="g-nav-tab" data-mode="wand">WAND Top-K Pruned</div>
  <div class="g-nav-tab" data-mode="bm25">BM25 Lexical</div>
  <div class="g-nav-tab" data-mode="phrase">Exact Phrases</div>
</div>

<div class="g-filter-bar">
  <span class="g-filter-label">Category:</span>
  <span class="g-filter-pill active" data-filter="">All</span>
  <span class="g-filter-pill" data-filter="category:AI">AI & ML</span>
  <span class="g-filter-pill" data-filter="category:Cloud">Cloud</span>
  <span class="g-filter-pill" data-filter="category:Security">Security</span>
  <span class="g-filter-pill" data-filter="category:Genomics">Genomics</span>
  <span class="g-filter-pill" data-filter="category:Quantum">Quantum</span>

  <span class="g-filter-label" style="margin-left:16px;">Year:</span>
  <span class="g-filter-pill" data-filter-year="year:>=2024">≥ 2024</span>
  <span class="g-filter-pill" data-filter-year="year:>=2023">≥ 2023</span>
</div>

<main class="g-layout">
  <!-- Left: Results List -->
  <section class="g-results-pane">
    <div class="g-stats-line" id="statsLine">Type a query to search the indexed corpus</div>
    <div class="g-results-list" id="resultsList">
      <div class="g-empty-state">
        <div class="g-empty-title">Instant Multi-Stage IR Search</div>
        <p>Type keywords like <code>machine learning</code>, <code>cloud microservices</code>, <code>neural networks</code> to test real-time retrieval.</p>
      </div>
    </div>
  </section>

  <!-- Right: Telemetry & Ingestion Card -->
  <aside class="g-sidebar-pane">
    <div class="g-card">
      <div class="g-card-title">
        <span>Execution Telemetry</span>
        <span class="card-sub" id="telemetryLatency">— µs</span>
      </div>
      <div class="g-telemetry-grid">
        <div class="g-metric-box">
          <div class="g-metric-label">Server Latency</div>
          <div class="g-metric-value" id="statLatency">—</div>
        </div>
        <div class="g-metric-box">
          <div class="g-metric-label">Client Latency</div>
          <div class="g-metric-value" id="statClientLatency">—</div>
        </div>
        <div class="g-metric-box">
          <div class="g-metric-label">Active Mode</div>
          <div class="g-metric-value" id="statMode" style="font-size:13px; color:var(--g-accent);">Hybrid</div>
        </div>
        <div class="g-metric-box">
          <div class="g-metric-label">Total Index</div>
          <div class="g-metric-value" id="statDocs">30</div>
        </div>
      </div>

      <div class="g-pipeline-flow">
        <div class="g-flow-step" id="stepToken"><span>1. Tokenizer & Stemmer</span><span>✓</span></div>
        <div class="g-flow-step" id="stepL1"><span>2. Stage-1 Lexical + HNSW</span><span>✓</span></div>
        <div class="g-flow-step" id="stepRRF"><span>3. Reciprocal Rank Fusion</span><span>✓</span></div>
        <div class="g-flow-step" id="stepL2"><span>4. Stage-2 Neural Rerank</span><span id="stepL2Status">Standby</span></div>
        <div class="g-flow-step" id="stepFilter"><span>5. Bitset Attribute Filter</span><span id="stepFilterStatus">Pass</span></div>
      </div>
    </div>

    <!-- Live Document Ingestion Panel -->
    <div class="g-card">
      <div class="g-card-title">
        <span>Real-Time Index Ingestion</span>
        <span class="card-sub">LSM WAL</span>
      </div>
      <form class="g-ingest-form" id="ingestForm">
        <div class="g-form-row">
          <input type="number" id="inDocId" class="g-input id-input" placeholder="Doc ID" value="1001" required>
          <input type="text" id="inDocCat" class="g-input cat-input" placeholder="Category" value="AI">
        </div>
        <textarea id="inDocText" class="g-input g-textarea" placeholder="Paste document content, abstract, or extracted PDF passage..." required></textarea>
        <button type="submit" class="g-btn">Index Document</button>
      </form>
    </div>
  </aside>
</main>

<script>
let currentMode = 'hybrid';
let activeCategoryFilter = '';
let activeYearFilter = '';
let searchDebounce = null;
let suggestDebounce = null;
let activeSuggestIndex = -1;

const queryInput = document.getElementById('queryInput');
const omnibar = document.getElementById('omnibar');
const clearBtn = document.getElementById('clearBtn');
const suggestDropdown = document.getElementById('suggestDropdown');
const resultsList = document.getElementById('resultsList');
const statsLine = document.getElementById('statsLine');

// Focus shortcut
window.addEventListener('keydown', (e) => {
  if (e.key === '/' && document.activeElement !== queryInput && document.activeElement.tagName !== 'TEXTAREA') {
    e.preventDefault();
    queryInput.focus();
    queryInput.select();
  }
});

async function refreshEngineStatus() {
  try {
    const res = await fetch('/health');
    const data = await res.json();
    document.getElementById('headerDocCount').innerText = `${data.totalDocs} docs indexed`;
    document.getElementById('statDocs').innerText = data.totalDocs;
    document.getElementById('engineVersion').innerText = `v${data.version}`;
  } catch (e) {}
}
refreshEngineStatus();

// Navigation Tabs
document.querySelectorAll('.g-nav-tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.g-nav-tab').forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
    currentMode = tab.dataset.mode;
    document.getElementById('statMode').innerText = tab.innerText.split(' ')[0];
    executeSearch();
  });
});

// Category Filter Chips
document.querySelectorAll('.g-filter-pill[data-filter]').forEach(pill => {
  pill.addEventListener('click', () => {
    document.querySelectorAll('.g-filter-pill[data-filter]').forEach(p => p.classList.remove('active'));
    pill.classList.add('active');
    activeCategoryFilter = pill.dataset.filter;
    executeSearch();
  });
});

// Year Filter Chips
document.querySelectorAll('.g-filter-pill[data-filter-year]').forEach(pill => {
  pill.addEventListener('click', () => {
    if (pill.classList.contains('active')) {
      pill.classList.remove('active');
      activeYearFilter = '';
    } else {
      document.querySelectorAll('.g-filter-pill[data-filter-year]').forEach(p => p.classList.remove('active'));
      pill.classList.add('active');
      activeYearFilter = pill.dataset.filterYear;
    }
    executeSearch();
  });
});

// Omnibar events
queryInput.addEventListener('focus', () => {
  omnibar.classList.add('focused');
  if (queryInput.value.trim().length >= 2) fetchSuggestions();
});

queryInput.addEventListener('blur', () => {
  setTimeout(() => {
    omnibar.classList.remove('focused');
    suggestDropdown.style.display = 'none';
  }, 200);
});

queryInput.addEventListener('input', () => {
  const val = queryInput.value;
  clearBtn.style.display = val.length > 0 ? 'block' : 'none';

  clearTimeout(searchDebounce);
  searchDebounce = setTimeout(executeSearch, 120);

  clearTimeout(suggestDebounce);
  suggestDebounce = setTimeout(fetchSuggestions, 60);
});

queryInput.addEventListener('keydown', (e) => {
  const items = document.querySelectorAll('.g-suggest-item');
  if (e.key === 'ArrowDown') {
    e.preventDefault();
    if (items.length > 0) {
      activeSuggestIndex = (activeSuggestIndex + 1) % items.length;
      updateActiveSuggest(items);
    }
  } else if (e.key === 'ArrowUp') {
    e.preventDefault();
    if (items.length > 0) {
      activeSuggestIndex = (activeSuggestIndex - 1 + items.length) % items.length;
      updateActiveSuggest(items);
    }
  } else if (e.key === 'Enter') {
    if (activeSuggestIndex >= 0 && items[activeSuggestIndex]) {
      items[activeSuggestIndex].click();
    } else {
      suggestDropdown.style.display = 'none';
      executeSearch();
    }
  } else if (e.key === 'Escape') {
    suggestDropdown.style.display = 'none';
  }
});

function updateActiveSuggest(items) {
  items.forEach((item, idx) => {
    if (idx === activeSuggestIndex) {
      item.classList.add('active');
      queryInput.value = item.dataset.text;
    } else {
      item.classList.remove('active');
    }
  });
}

function clearSearch() {
  queryInput.value = '';
  clearBtn.style.display = 'none';
  suggestDropdown.style.display = 'none';
  executeSearch();
  queryInput.focus();
}

function resetSearch() {
  queryInput.value = '';
  clearBtn.style.display = 'none';
  executeSearch();
}

// Autocomplete fetch
async function fetchSuggestions() {
  const q = queryInput.value.trim();
  if (q.length < 2) {
    suggestDropdown.style.display = 'none';
    return;
  }
  try {
    const res = await fetch(`/suggest?q=${encodeURIComponent(q)}&k=6`);
    const data = await res.json();
    if (data.suggestions && data.suggestions.length > 0) {
      activeSuggestIndex = -1;
      suggestDropdown.innerHTML = data.suggestions.map(s => `
        <div class="g-suggest-item" data-text="${s.text}" onclick="chooseSuggestion('${s.text.replace(/'/g, "\\'")}')">
          <div class="g-suggest-left">
            <svg viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27C15.41 12.59 16 11.11 16 9.5 16 5.91 13.09 3 9.5 3S3 5.91 3 9.5 5.91 16 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/></svg>
            <span>${highlightPrefix(s.text, q)}</span>
          </div>
          <span class="g-suggest-freq">${s.frequency}</span>
        </div>
      `).join('');
      suggestDropdown.style.display = 'block';
    } else {
      suggestDropdown.style.display = 'none';
    }
  } catch (e) {
    suggestDropdown.style.display = 'none';
  }
}

function highlightPrefix(text, prefix) {
  if (text.toLowerCase().startsWith(prefix.toLowerCase())) {
    return `<b>${text.substring(0, prefix.length)}</b>${text.substring(prefix.length)}`;
  }
  return text;
}

function chooseSuggestion(text) {
  queryInput.value = text;
  suggestDropdown.style.display = 'none';
  executeSearch();
}

// Execute Multi-stage Search
async function executeSearch() {
  const q = queryInput.value.trim();
  if (!q) {
    resultsList.innerHTML = `
      <div class="g-empty-state">
        <div class="g-empty-title">Instant Multi-Stage IR Search</div>
        <p>Type keywords like <code>machine learning</code>, <code>cloud microservices</code>, <code>neural networks</code> to test real-time retrieval.</p>
      </div>`;
    statsLine.innerText = 'Type a query to search the indexed corpus';
    document.getElementById('statLatency').innerText = '—';
    document.getElementById('statClientLatency').innerText = '—';
    document.getElementById('telemetryLatency').innerText = '— µs';
    return;
  }

  // Combine filters
  const filters = [];
  if (activeCategoryFilter) filters.push(activeCategoryFilter);
  if (activeYearFilter) filters.push(activeYearFilter);
  const filterParam = filters.join(',');

  const t0 = performance.now();
  try {
    const url = `/search?q=${encodeURIComponent(q)}&mode=${currentMode}&k=10${filterParam ? `&filter=${encodeURIComponent(filterParam)}` : ''}`;
    const res = await fetch(url);
    const data = await res.json();
    const clientLatencyMs = (performance.now() - t0).toFixed(1);

    // Update telemetry sidebar
    document.getElementById('statLatency').innerText = `${data.latencyUs} µs`;
    document.getElementById('statClientLatency').innerText = `${clientLatencyMs} ms`;
    document.getElementById('telemetryLatency').innerText = `${data.latencyUs} µs`;

    document.getElementById('stepL2Status').innerText = currentMode === 'rerank' ? 'Active' : 'Bypassed';
    document.getElementById('stepFilterStatus').innerText = filterParam ? filterParam : 'None';

    const numResults = data.results ? data.results.length : 0;
    statsLine.innerText = `About ${numResults} results (${data.latencyUs} µs server latency | ${clientLatencyMs} ms roundtrip)`;

    if (!data.results || data.results.length === 0) {
      resultsList.innerHTML = `
        <div class="g-empty-state">
          <div class="g-empty-title">No matching documents found</div>
          <p>Try adjusting your query or clearing active category filters.</p>
        </div>`;
      return;
    }

    resultsList.innerHTML = data.results.map((r, idx) => {
      const category = r.category || 'General';
      const year = r.year || 2024;
      const title = extractTitle(r.content, r.docId);

      return `
        <article class="g-result-item">
          <div class="g-result-meta">
            <div class="g-result-favicon">${r.docId}</div>
            <div class="g-result-source">
              <span>corpus</span>
              <span>›</span>
              <span class="g-result-category">${category}</span>
              <span>›</span>
              <span>doc_${r.docId}</span>
            </div>
          </div>
          <a class="g-result-title" onclick="recordFeedback(${r.docId}, this)">${title}</a>
          <div class="g-result-snippet">${r.snippet || r.content}</div>
          <div class="g-chips-row">
            <span class="g-chip score">Rank Score: ${r.score}</span>
            ${r.bm25Score ? `<span class="g-chip">BM25: ${r.bm25Score}</span>` : ''}
            ${r.denseScore ? `<span class="g-chip">Dense: ${r.denseScore}</span>` : ''}
            ${r.l1Score ? `<span class="g-chip">L1 Score: ${r.l1Score}</span>` : ''}
            <span class="g-chip year">Year: ${year}</span>
            <button class="g-feedback-btn" onclick="recordFeedback(${r.docId}, this)">★ Relevant</button>
          </div>
        </article>
      `;
    }).join('');

  } catch (err) {
    statsLine.innerText = 'Error connecting to search engine';
  }
}

function extractTitle(content, docId) {
  if (!content) return `Document #${docId}`;
  const firstSentence = content.split('.')[0].trim();
  if (firstSentence.length > 0 && firstSentence.length < 90) {
    return firstSentence;
  }
  return content.substring(0, 75) + '...';
}

async function recordFeedback(docId, el) {
  try {
    await fetch('/click', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({docId})
    });
    const btn = el.closest('.g-result-item').querySelector('.g-feedback-btn');
    if (btn) {
      btn.classList.add('clicked');
      btn.innerText = '✓ Personalized';
    }
  } catch (e) {}
}

// Ingestion Form
document.getElementById('ingestForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const docId = parseInt(document.getElementById('inDocId').value);
  const content = document.getElementById('inDocText').value.trim();
  const category = document.getElementById('inDocCat').value.trim();

  if (!content) return;

  try {
    const res = await fetch('/document', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({docId, content})
    });
    document.getElementById('inDocText').value = '';
    document.getElementById('inDocId').value = docId + 1;
    refreshEngineStatus();
    executeSearch();
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

