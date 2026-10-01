// ===============================================================
//  MINI SEARCH ENGINE  —  web server (Level 6: UI)
//
//  A tiny HTTP server written with plain sockets (Winsock on Windows,
//  BSD sockets on Linux/macOS). No external libraries.
//
//  The C++ SearchEngine is the backend; the browser page in web/index.html
//  is the front-end and talks to it through a small JSON API:
//
//    GET  /                      -> web/index.html
//    GET  /api/search?q=...      -> ranked results
//    GET  /api/suggest?q=...     -> autocomplete words (Trie)
//    GET  /api/doc?id=..&terms=  -> full document with highlights
//    GET  /api/index?term=...    -> inverted-index entry (df, idf, postings)
//    GET  /api/stats             -> collection statistics
//    POST /api/reload            -> re-read the documents folder
//    POST /api/add?name=...      -> save request body as a new .txt and re-index
//
//  Usage:  web_server [documents_folder] [--port 8080] [--no-browser]
// ===============================================================
#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX          // stop windows.h from defining min/max macros
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  using socket_t = SOCKET;
  #define CLOSE_SOCKET closesocket
  #define SEND_FLAGS 0
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
  #define INVALID_SOCKET (-1)
  #define CLOSE_SOCKET close
  #ifdef MSG_NOSIGNAL
    #define SEND_FLAGS MSG_NOSIGNAL
  #else
    #define SEND_FLAGS 0
  #endif
#endif

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "SearchEngine.h"

namespace fs = std::filesystem;

namespace {

// Highlight markers used internally; turned into <mark> tags when converted to HTML.
const std::string MARK_OPEN = "\x01";
const std::string MARK_CLOSE = "\x02";

// ---------------------------------------------------------------
// Small helpers: URL decoding, JSON and HTML escaping
// ---------------------------------------------------------------

std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size() &&
                 std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    return out;
}

std::string json(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string num(double v, int precision = 4) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v;
    return ss.str();
}

// Escape text for HTML and turn the highlight markers into <mark>…</mark>.
std::string toHtml(const std::string& marked) {
    std::string out;
    bool inMark = false;
    for (char c : marked) {
        if (c == MARK_OPEN[0])       { out += "<mark>";  inMark = true; }
        else if (c == MARK_CLOSE[0]) { out += "</mark>"; inMark = false; }
        else if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c == '\r') continue;
        else out += c;
    }
    if (inMark) out += "</mark>";  // snippet may have been cut inside a highlight
    return out;
}

std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(s);
    while (std::getline(ss, cur, ',')) if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------
// HTTP request / response
// ---------------------------------------------------------------

struct Request {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> params;
    std::string body;
    std::string param(const std::string& key) const {
        auto it = params.find(key);
        return it == params.end() ? "" : it->second;
    }
};

struct Response {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

bool readRequest(socket_t client, Request& req) {
    std::string data;
    char buf[4096];
    size_t headerEnd = std::string::npos;

    while (headerEnd == std::string::npos) {
        int n = recv(client, buf, sizeof buf, 0);
        if (n <= 0) return false;
        data.append(buf, n);
        headerEnd = data.find("\r\n\r\n");
        if (data.size() > 64 * 1024 && headerEnd == std::string::npos) return false;
    }

    // Request line: METHOD /path?query HTTP/1.1
    std::istringstream head(data.substr(0, headerEnd));
    std::string target, version, line;
    head >> req.method >> target >> version;

    size_t contentLength = 0;
    std::getline(head, line);
    while (std::getline(head, line)) {
        std::string lower;
        for (char c : line) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.rfind("content-length:", 0) == 0) contentLength = std::strtoul(line.c_str() + 15, nullptr, 10);
    }
    if (contentLength > 2 * 1024 * 1024) return false;  // 2 MB limit

    req.body = data.substr(headerEnd + 4);
    while (req.body.size() < contentLength) {
        int n = recv(client, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.body.append(buf, n);
    }

    size_t q = target.find('?');
    req.path = urlDecode(target.substr(0, q));
    if (q != std::string::npos) {
        std::istringstream qs(target.substr(q + 1));
        std::string pair;
        while (std::getline(qs, pair, '&')) {
            size_t eq = pair.find('=');
            if (eq == std::string::npos) req.params[urlDecode(pair)] = "";
            else req.params[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        }
    }
    return true;
}

void sendResponse(socket_t client, const Response& res) {
    const char* reason = res.status == 200 ? "OK" : res.status == 404 ? "Not Found"
                       : res.status == 400 ? "Bad Request" : "Error";
    std::ostringstream out;
    out << "HTTP/1.1 " << res.status << " " << reason << "\r\n"
        << "Content-Type: " << res.contentType << "\r\n"
        << "Content-Length: " << res.body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n"
        << res.body;
    std::string s = out.str();
    size_t sent = 0;
    while (sent < s.size()) {
        int n = send(client, s.data() + sent, static_cast<int>(s.size() - sent), SEND_FLAGS);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

Response error(int status, const std::string& message) {
    Response r;
    r.status = status;
    r.body = "{\"error\":" + json(message) + "}";
    return r;
}

// ---------------------------------------------------------------
// API handlers
// ---------------------------------------------------------------

class App {
public:
    App(std::string docsFolder, std::string webFolder)
        : docsFolder(std::move(docsFolder)), webFolder(std::move(webFolder)) {}

    size_t load() { return engine.loadDirectory(docsFolder); }

    Response handle(const Request& req) {
        if (req.path == "/" || req.path == "/index.html") return serveFile("index.html");
        if (req.path == "/api/search")  return search(req);
        if (req.path == "/api/suggest") return suggest(req);
        if (req.path == "/api/doc")     return document(req);
        if (req.path == "/api/index")   return indexEntry(req);
        if (req.path == "/api/stats")   return stats();
        if (req.path == "/api/reload" && req.method == "POST") { load(); return stats(); }
        if (req.path == "/api/add" && req.method == "POST")    return addDocument(req);
        if (req.path.rfind("/api/", 0) == 0) return error(404, "Unknown API endpoint");
        return serveFile(req.path.substr(1));
    }

private:
    Response serveFile(const std::string& name) {
        if (name.find("..") != std::string::npos) return error(400, "Bad path");
        fs::path p = fs::path(webFolder) / name;
        std::ifstream in(p, std::ios::binary);
        if (!in) return error(404, "Not found: " + name);
        Response r;
        std::stringstream ss;
        ss << in.rdbuf();
        r.body = ss.str();
        std::string ext = p.extension().string();
        r.contentType = ext == ".html" ? "text/html; charset=utf-8"
                      : ext == ".css"  ? "text/css; charset=utf-8"
                      : ext == ".js"   ? "application/javascript; charset=utf-8"
                      : ext == ".svg"  ? "image/svg+xml"
                      : "application/octet-stream";
        return r;
    }

    // Describe how the query was understood, for the "Interpreted as" line in the UI.
    std::string describeQuery(const ParsedQuery& pq) {
        std::string out = "{\"mode\":" + json(pq.booleanMode ? "boolean" : "ranked") + ",\"groups\":[";
        for (size_t g = 0; g < pq.groups.size(); ++g) {
            out += (g ? ",[" : "[");
            for (size_t i = 0; i < pq.groups[g].size(); ++i) {
                const auto& item = pq.groups[g][i];
                std::string text;
                for (size_t t = 0; t < item.terms.size(); ++t) text += (t ? " " : "") + item.terms[t];
                out += std::string(i ? "," : "") + "{\"text\":" + json(text) +
                       ",\"phrase\":" + (item.isPhrase() ? "true" : "false") + "}";
            }
            out += "]";
        }
        return out + "]}";
    }

    Response search(const Request& req) {
        std::string q = req.param("q");
        auto start = std::chrono::steady_clock::now();
        auto results = engine.search(q, 20);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        std::ostringstream out;
        out << "{\"query\":" << json(q)
            << ",\"timeMs\":" << num(ms, 3)
            << ",\"interpretation\":" << describeQuery(engine.parse(q))
            << ",\"results\":[";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            const Document& doc = engine.document(r.docId);
            out << (i ? "," : "") << "{\"id\":" << r.docId
                << ",\"name\":" << json(doc.name)
                << ",\"score\":" << num(r.score)
                << ",\"length\":" << doc.terms.size()
                << ",\"snippet\":" << json(toHtml(engine.snippet(r.docId, r.matchedTerms, 180, MARK_OPEN, MARK_CLOSE)))
                << ",\"matched\":[";
            for (size_t t = 0; t < r.matchedTerms.size(); ++t) {
                const auto& term = r.matchedTerms[t];
                out << (t ? "," : "") << "{\"term\":" << json(term)
                    << ",\"tf\":" << doc.termFreq.at(term)
                    << ",\"idf\":" << num(engine.idf(term), 3) << "}";
            }
            out << "]}";
        }
        out << "]";

        // When nothing matches, offer completions for the last word typed.
        out << ",\"didYouMean\":[";
        if (results.empty()) {
            size_t cut = q.find_last_of(" \"");
            std::string last = cut == std::string::npos ? q : q.substr(cut + 1);
            auto sugg = last.size() >= 2 ? engine.suggest(last, 5) : std::vector<std::pair<std::string, int>>{};
            for (size_t i = 0; i < sugg.size(); ++i) out << (i ? "," : "") << json(sugg[i].first);
        }
        out << "]}";

        Response r;
        r.body = out.str();
        return r;
    }

    Response suggest(const Request& req) {
        auto sugg = engine.suggest(req.param("q"), 8);
        std::string out = "[";
        for (size_t i = 0; i < sugg.size(); ++i)
            out += (i ? "," : "") + std::string("{\"word\":") + json(sugg[i].first) +
                   ",\"count\":" + std::to_string(sugg[i].second) + "}";
        Response r;
        r.body = out + "]";
        return r;
    }

    Response document(const Request& req) {
        int id = std::atoi(req.param("id").c_str());
        if (id < 0 || id >= static_cast<int>(engine.documentCount())) return error(404, "No such document");
        const Document& doc = engine.document(id);
        auto terms = splitComma(req.param("terms"));
        Response r;
        r.body = "{\"id\":" + std::to_string(id) + ",\"name\":" + json(doc.name) +
                 ",\"terms\":" + std::to_string(doc.terms.size()) +
                 ",\"unique\":" + std::to_string(doc.termFreq.size()) +
                 ",\"html\":" + json(toHtml(engine.highlight(doc.content, terms, MARK_OPEN, MARK_CLOSE))) + "}";
        return r;
    }

    Response indexEntry(const Request& req) {
        std::string word = req.param("term");
        auto terms = engine.processor().process(word);
        std::ostringstream out;
        out << "{\"input\":" << json(word);
        if (terms.empty()) {
            out << ",\"stopWord\":true,\"postings\":[]}";
        } else {
            const std::string& term = terms[0];
            const auto* list = engine.index().postings(term);
            out << ",\"stopWord\":false,\"term\":" << json(term)
                << ",\"df\":" << (list ? list->size() : 0)
                << ",\"idf\":" << num(engine.idf(term), 3)
                << ",\"postings\":[";
            if (list) {
                bool first = true;
                for (const auto& [docId, positions] : *list) {
                    out << (first ? "" : ",") << "{\"id\":" << docId
                        << ",\"name\":" << json(engine.document(docId).name)
                        << ",\"tf\":" << positions.size() << ",\"positions\":[";
                    for (size_t i = 0; i < positions.size(); ++i) out << (i ? "," : "") << positions[i];
                    out << "]}";
                    first = false;
                }
            }
            out << "]}";
        }
        Response r;
        r.body = out.str();
        return r;
    }

    Response stats() {
        std::ostringstream out;
        out << "{\"documents\":" << engine.documentCount()
            << ",\"vocabulary\":" << engine.index().vocabularySize()
            << ",\"totalTerms\":" << engine.totalTerms()
            << ",\"indexMs\":" << num(engine.indexTimeMs(), 2)
            << ",\"folder\":" << json(fs::absolute(docsFolder).string())
            << ",\"docs\":[";
        for (size_t i = 0; i < engine.documentCount(); ++i) {
            const Document& d = engine.document(static_cast<int>(i));
            out << (i ? "," : "") << "{\"id\":" << i << ",\"name\":" << json(d.name)
                << ",\"terms\":" << d.terms.size() << "}";
        }
        out << "]}";
        Response r;
        r.body = out.str();
        return r;
    }

    Response addDocument(const Request& req) {
        std::string name;
        for (char c : req.param("name"))
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') name += c;
        if (name.empty()) return error(400, "Give the document a name (letters, digits, _ or -)");
        if (req.body.find_first_not_of(" \t\r\n") == std::string::npos) return error(400, "Document text is empty");

        std::ofstream outFile(fs::path(docsFolder) / (name + ".txt"), std::ios::binary);
        if (!outFile) return error(500, "Could not write file");
        outFile << req.body;
        outFile.close();
        load();
        return stats();
    }

    std::string docsFolder;
    std::string webFolder;
    SearchEngine engine;
};

std::string findFolder(const std::string& name) {
    for (const char* prefix : {"", "../", "../../"})
        if (fs::is_directory(prefix + name)) return prefix + name;
    return name;
}

void openBrowser(const std::string& url) {
#ifdef _WIN32
    std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    std::string cmd = "open \"" + url + "\" >/dev/null 2>&1 &";
#else
    std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
#endif
    if (std::system(cmd.c_str()) != 0) { /* browser could not be opened; user opens the URL manually */ }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string docsFolder;
    int port = 8080;
    bool browser = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--no-browser") browser = false;
        else docsFolder = a;
    }
    if (docsFolder.empty()) docsFolder = findFolder("documents");
    std::string webFolder = findFolder("web");

    if (!fs::exists(fs::path(webFolder) / "index.html")) {
        std::cerr << "Error: cannot find web/index.html. Run from the project folder.\n";
        return 1;
    }

    App app(docsFolder, webFolder);
    try {
        size_t n = app.load();
        std::cout << "Indexed " << n << " documents from '" << docsFolder << "'.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::cerr << "WSAStartup failed\n"; return 1; }
#endif

    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) { std::cerr << "Could not create socket\n"; return 1; }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only: not visible to other machines

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(server, 16) != 0) {
        std::cerr << "Could not listen on port " << port
                  << ". Is it already in use? Try: web_server --port 8081\n";
        CLOSE_SOCKET(server);
        return 1;
    }

    std::string url = "http://localhost:" + std::to_string(port);
    std::cout << "=================================\n"
              << "   MINI SEARCH ENGINE  (web UI)\n"
              << "=================================\n"
              << "Open " << url << " in your browser.\n"
              << "Press Ctrl+C to stop the server.\n\n";
    if (browser) openBrowser(url);

    while (true) {
        socket_t client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        Request req;
        if (readRequest(client, req)) {
            Response res;
            try {
                res = app.handle(req);
            } catch (const std::exception& e) {
                res = error(500, e.what());
            }
            if (req.path.rfind("/api/", 0) == 0)
                std::cout << req.method << " " << req.path
                          << (req.params.count("q") ? "  q=" + req.param("q") : "") << "\n";
            sendResponse(client, res);
        }
        CLOSE_SOCKET(client);
    }
}
