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
      startTime_(std::chrono::steady_clock::now()) {}

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

    running_.store(true);
    std::cout << "[SearchServer] HTTP Server listening on port " << port_ << " ...\n";

    if (background) {
        workerThread_ = std::thread(&SearchServer::serverLoop, this, serverFd_);
    } else {
        serverLoop(serverFd_);
    }
    return true;
}

void SearchServer::stop() {
    if (running_.exchange(false)) {
        if (serverFd_ >= 0) {
            shutdown(serverFd_, SHUT_RDWR);
            close(serverFd_);
            serverFd_ = -1;
        }
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
        std::cout << "[SearchServer] Server stopped.\n";
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

        // Process request in a worker thread or synchronously
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
    std::string method, path, version;
    iss >> method >> path >> version;

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

    if (endpoint == "/health" || endpoint == "/") {
        responseBody = handleHealth();
    } else if (endpoint == "/metrics") {
        contentType = "text/plain; version=0.0.4";
        responseBody = handleMetrics();
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

    if (mode == "hybrid" && hybridPossible) {
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
