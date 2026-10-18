#pragma once
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "db.hpp"
#include "scrubber.hpp"
#include "subsystems.hpp"

namespace devvault {

class McpServer {
public:
    static int run_stdio(DevVaultDb& db, const std::string& sources_md_path, const std::string& vault_root) {
        std::string line;
        while (std::getline(std::cin, line)) {
            line = Scrubber::trim(line);
            if (line.empty()) continue;

            std::string method = Scrubber::json_get_string(line, "method");
            std::string id_raw = extract_json_id(line);

            if (method == "notifications/initialized" || method.find("notifications/") == 0) {
                continue;
            }

            if (method == "initialize") {
                std::ostringstream res;
                res << "{\"jsonrpc\":\"2.0\",\"id\":" << id_raw
                    << ",\"result\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{},\"resources\":{}},"
                    << "\"serverInfo\":{\"name\":\"devvault\",\"version\":\"2.0.0-sandybridge-avx1\"}}}\n";
                std::cout << res.str() << std::flush;
                continue;
            }

            if (method == "tools/list") {
                std::ostringstream res;
                res << "{\"jsonrpc\":\"2.0\",\"id\":" << id_raw << ",\"result\":{\"tools\":["
                    << "{\"name\":\"devvault_search\",\"description\":\"Local-First Hybrid RAG Search (SQLite FTS5 BM25 + Sandy Bridge AVX1 384-dim Cosine Vector RRF) across Tom's coding sessions, hardware specs, road-warrior mobile Gemini notes, mirrored web pages, and categorized personal documents. ALWAYS query this tool BEFORE searching the web.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"Search query terms or question\"},\"top_k\":{\"type\":\"integer\",\"description\":\"Number of results (default 8)\"},\"category\":{\"type\":\"string\",\"description\":\"Optional category filter (e.g. 04_Computer_Hardware_and_Systems, 05_Computer_Programming_and_AI)\"}},\"required\":[\"query\"]}},"
                    << "{\"name\":\"devvault_store_note\",\"description\":\"Store a newly discovered coding solution, compiler flag, hardware finding, or owner preference directly into the local DevVault Vector + FTS5 RAG database.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"category\":{\"type\":\"string\",\"description\":\"Category (default 05_Computer_Programming_and_AI)\"},\"content\":{\"type\":\"string\"},\"source_uri\":{\"type\":\"string\"}},\"required\":[\"title\",\"content\"]}},"
                    << "{\"name\":\"devvault_storage_status\",\"description\":\"Inspect live HP Z420 drive capacities (2TB NVMe + 256GB swapfile, 8TB WD HDD, 2TB Hitachi HDD, Stage 2A 18TB Exos Mirror, Stage 2B 36TB Mirrored-Striped RAID 10, Stage 3 Minisforum N5 Air NAS) and 66% threshold alerts.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
                    << "{\"name\":\"devvault_classify_status\",\"description\":\"Return category breakdown and document counts in /mnt/storage_8tb_linux/Tom_Knowledge_Vault and the active RAG database.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
                    << "{\"name\":\"devvault_classify_image\",\"description\":\"Classify and OCR an image, photo, schematic, or scanned PDF using MiniCPM-V 4.6 under PolyStrata and store the result in DevVault.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path to the image (.jpg, .png, .webp, .tiff, .bmp) or scanned PDF (.pdf)\"},\"endpoint\":{\"type\":\"string\",\"description\":\"Optional PolyStrata vision endpoint override (default http://127.0.0.1:8080/v1/chat/completions)\"}},\"required\":[\"path\"]}},"
                    << "{\"name\":\"devvault_road_sync\",\"description\":\"Sync local SOURCES.md with Tom's cloud Google Docs and mirror watched URLs into the local RAG database.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
                    << "]}}\n";
                std::cout << res.str() << std::flush;
                continue;
            }

            if (method == "resources/list") {
                std::ostringstream res;
                res << "{\"jsonrpc\":\"2.0\",\"id\":" << id_raw << ",\"result\":{\"resources\":["
                    << "{\"uri\":\"devvault://owner/profile\",\"name\":\"Owner Profile & Hardware Context (Tom / HP Z420)\",\"mimeType\":\"text/markdown\"},"
                    << "{\"uri\":\"devvault://storage/status\",\"name\":\"HP Z420 Phased Storage & 66% Threshold Telemetry\",\"mimeType\":\"text/plain\"}"
                    << "]}}\n";
                std::cout << res.str() << std::flush;
                continue;
            }

            if (method == "resources/read") {
                std::string uri = Scrubber::json_get_string(line, "uri");
                std::string text;
                if (uri == "devvault://storage/status") {
                    text = StorageGuard::format_report(db);
                } else {
                    auto sigs = db.get_owner_signals();
                    std::ostringstream oss;
                    oss << "# DevVault Owner Profile (`devvault://owner/profile`)\n\n";
                    for (const auto& kv : sigs) {
                        oss << "- **" << kv.first << ":** " << kv.second << "\n";
                    }
                    text = oss.str();
                }
                std::ostringstream res;
                res << "{\"jsonrpc\":\"2.0\",\"id\":" << id_raw
                    << ",\"result\":{\"contents\":[{\"uri\":\"" << Scrubber::json_escape(uri)
                    << "\",\"mimeType\":\"text/markdown\",\"text\":\"" << Scrubber::json_escape(text) << "\"}]}}\n";
                std::cout << res.str() << std::flush;
                continue;
            }

            if (method == "tools/call") {
                std::string tool_name = Scrubber::json_get_string(line, "name");
                std::string tool_output;

                if (tool_name == "devvault_search") {
                    std::string q = Scrubber::json_get_string(line, "query");
                    int top_k = static_cast<int>(Scrubber::json_get_int(line, "top_k", 8));
                    std::string cat = Scrubber::json_get_string(line, "category");
                    if (top_k <= 0 || top_k > 30) top_k = 8;
                    auto hits = db.hybrid_search(q, top_k, cat);
                    std::ostringstream oss;
                    oss << "DevVault Hybrid RAG Search (`" << q << "`) — " << hits.size() << " results:\n\n";
                    for (size_t i = 0; i < hits.size(); ++i) {
                        const auto& h = hits[i];
                        oss << "[" << (i + 1) << "] **" << h.title << "** (`" << h.category << "` | `" << h.item_type << "`)\n"
                            << "    Source: `" << h.source_uri << "` | RRF: " << std::fixed << std::setprecision(4) << h.rrf_score
                            << " (Cosine: " << std::setprecision(3) << h.cosine_score << ", BM25: " << h.bm25_score << ")\n"
                            << "    Snippet: " << h.snippet << "\n\n";
                    }
                    if (hits.empty()) {
                        oss << "No local matches found in DevVault for `" << q << "`. You may proceed to online/external search.\n";
                    }
                    tool_output = oss.str();
                } else if (tool_name == "devvault_store_note") {
                    std::string title = Scrubber::json_get_string(line, "title");
                    std::string cat = Scrubber::json_get_string(line, "category");
                    if (cat.empty()) cat = "05_Computer_Programming_and_AI";
                    std::string content = Scrubber::json_get_string(line, "content");
                    std::string suri = Scrubber::json_get_string(line, "source_uri");
                    if (suri.empty()) {
                        suri = "devvault://notes/" + Sha256::hash_string(title + content).substr(0, 16);
                    }
                    int64_t id = db.upsert_item(title, suri, "agent_note", cat, content);
                    tool_output = "Stored note '" + title + "' in DevVault under category '" + cat + "' (URI: " + suri + ", item_id=" + std::to_string(id) + ").";
                } else if (tool_name == "devvault_storage_status") {
                    tool_output = StorageGuard::format_report(db);
                } else if (tool_name == "devvault_classify_status") {
                    auto stats = db.get_category_stats();
                    std::ostringstream oss;
                    oss << "DevVault Categorized Knowledge Base Summary (Vault: " << vault_root << "):\n\n";
                    for (const auto& s : stats) {
                        oss << "- **" << s.category << "**: " << s.file_count << " items ("
                            << std::fixed << std::setprecision(2) << (s.total_bytes / (1024.0 * 1024.0)) << " MiB)\n";
                    }
                    tool_output = oss.str();
                } else if (tool_name == "devvault_classify_image") {
                    std::string p = Scrubber::json_get_string(line, "path");
                    std::string ep = Scrubber::json_get_string(line, "endpoint");
                    auto vc = MiniCpmVision::classify_image(p, ep);
                    std::ostringstream oss;
                    oss << "MiniCPM-V 4.6 (PolyStrata Vision) Classification Result:\n"
                        << "- File: `" << p << "`\n"
                        << "- Status: " << (vc.success ? "SUCCESS" : "FALLBACK / ENDPOINT OFFLINE") << "\n"
                        << "- Model: `" << vc.model_used << "`\n"
                        << "- Latency: `" << std::fixed << std::setprecision(1) << vc.latency_ms << " ms`\n"
                        << "- Category: `" << vc.category << "` (Confidence: " << std::setprecision(2) << vc.confidence << ")\n"
                        << "- Title: " << vc.title << "\n"
                        << "- Summary: " << vc.summary << "\n"
                        << "- OCR Text: " << vc.ocr_text << "\n";
                    if (vc.success) {
                        db.upsert_item(vc.title, "file://" + p, "vision_classified_image", vc.category,
                                       "Title: " + vc.title + "\nSummary: " + vc.summary + "\nOCR: " + vc.ocr_text);
                    }
                    tool_output = oss.str();
                } else if (tool_name == "devvault_road_sync") {
                    std::string sync_log;
                    CodingAndRoadSync::run_road_and_web_sync(db, sources_md_path, true, &sync_log);
                    tool_output = "Completed Road-Warrior SOURCES.md + Google Docs + Web Mirror sync:\n" + sync_log;
                } else {
                    tool_output = "Unknown tool: " + tool_name;
                }

                std::ostringstream res;
                res << "{\"jsonrpc\":\"2.0\",\"id\":" << id_raw
                    << ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\""
                    << Scrubber::json_escape(tool_output) << "\"}]}}\n";
                std::cout << res.str() << std::flush;
                continue;
            }

            // Default JSON-RPC response
            std::ostringstream res;
            res << "{\"jsonrpc\":\"2.0\",\"id\":" << id_raw << ",\"result\":{}}\n";
            std::cout << res.str() << std::flush;
        }
        return 0;
    }

    // Native POSIX TCP HTTP Server (:8089) for online/authorized AI agents
    static int run_http_gateway(DevVaultDb& db, int port = 8089) {
        int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (srv_fd < 0) {
            std::cerr << "[devvault serve] Failed to create socket\n";
            return 1;
        }
        int opt = 1;
        setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (bind(srv_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[devvault serve] Port " << port << " is already in use.\n";
            close(srv_fd);
            return 1;
        }
        listen(srv_fd, 16);
        std::cout << "[devvault serve] Native C++17 RAG HTTP Gateway listening on http://127.0.0.1:" << port << "\n";

        while (true) {
            sockaddr_in cli{};
            socklen_t clen = sizeof(cli);
            int cfd = accept(srv_fd, reinterpret_cast<sockaddr*>(&cli), &clen);
            if (cfd < 0) continue;

            char buf[8192];
            ssize_t n = recv(cfd, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                std::string req(buf);
                std::string body_json;

                if (req.find("GET /v1/status") == 0 || req.find("GET /health") == 0) {
                    std::string rep = StorageGuard::format_report(db);
                    body_json = "{\"status\":\"ok\",\"engine\":\"devvault-2.0-sandybridge-avx1\",\"report\":\"" + Scrubber::json_escape(rep) + "\"}";
                } else {
                    // Extract query parameter ?q=... or JSON body {"query":"..."}
                    std::string q = Scrubber::json_get_string(req, "query");
                    if (q.empty()) {
                        size_t qpos = req.find("?q=");
                        if (qpos != std::string::npos) {
                            size_t qend = req.find_first_of(" &\r\n", qpos + 3);
                            q = req.substr(qpos + 3, (qend == std::string::npos) ? std::string::npos : (qend - (qpos + 3)));
                        }
                    }
                    auto hits = db.hybrid_search(q.empty() ? "Z420 Exos" : q, 8);
                    std::ostringstream oss;
                    oss << "{\"status\":\"ok\",\"query\":\"" << Scrubber::json_escape(q) << "\",\"results\":[";
                    for (size_t i = 0; i < hits.size(); ++i) {
                        if (i > 0) oss << ",";
                        oss << "{\"title\":\"" << Scrubber::json_escape(hits[i].title)
                            << "\",\"category\":\"" << Scrubber::json_escape(hits[i].category)
                            << "\",\"source_uri\":\"" << Scrubber::json_escape(hits[i].source_uri)
                            << "\",\"rrf_score\":" << hits[i].rrf_score
                            << ",\"cosine_score\":" << hits[i].cosine_score
                            << ",\"snippet\":\"" << Scrubber::json_escape(hits[i].snippet) << "\"}";
                    }
                    oss << "]}";
                    body_json = oss.str();
                }

                std::ostringstream http;
                http << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body_json.size() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body_json;
                std::string resp = http.str();
                send(cfd, resp.data(), resp.size(), 0);
            }
            close(cfd);
        }
        close(srv_fd);
        return 0;
    }

    // Idle-aware continuous background daemon: runs until Owner arrives home and inspects process
    static int run_daemon(const std::string& db_path, const std::string& sources_md_path, const std::string& vault_root, int http_port = 8089) {
        if (nice(19) == -1) {}
        int lock_fd = open("/mnt/nvme_storage/devvault/daemon.lock", O_CREAT | O_RDWR, 0666);
        if (lock_fd >= 0) {
            if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
                std::cerr << "[devvault daemon] Daemon is already running. Inspect /mnt/nvme_storage/devvault/daemon_status.txt\n";
                close(lock_fd);
                return 0;
            }
        }
        {
            std::ofstream pid_f("/mnt/nvme_storage/devvault/daemon.pid", std::ios::trunc);
            if (pid_f) pid_f << getpid() << "\n";
        }

        // Launch HTTP RAG Gateway (:8089) on a detached worker thread with its own WAL connection
        std::thread http_thread([db_path, http_port]() {
            try {
                DevVaultDb http_db(db_path);
                run_http_gateway(http_db, http_port);
            } catch (...) {}
        });
        http_thread.detach();

        DevVaultDb db(db_path);
        std::vector<std::string> roots = {
            "/mnt/storage_8tb_linux/documents",
            "/mnt/storage_8tb_linux/downloads",
            "/mnt/storage_8tb_linux/Computer stuff",
            "/mnt/storage_8tb_linux/music",
            "/mnt/storage_8tb_linux/python_projects",
            "/home/tom/Documents",
            "/home/tom/Downloads",
            "/mnt/win7_backup",
            "/mnt/samsung_storage",
            "/mnt/nvme_storage/ai_projects"
        };

        int cycle = 1;
        int64_t total_new_session = 0;
        bool system_fully_categorized = false;
        std::string started_at = Scrubber::iso8601_now();

        std::cout << "[devvault daemon] Started PID " << getpid() << " at " << started_at << "\n"
                  << "  Status File : /mnt/nvme_storage/devvault/daemon_status.txt\n"
                  << "  Daemon Log  : /mnt/nvme_storage/devvault/daemon.log\n"
                  << "  HTTP Gateway: http://127.0.0.1:" << http_port << "\n" << std::flush;

        while (true) {
            // Check 1-min loadavg to ensure we only run during periods of inactivity / low load
            double load_1m = 0.0;
            {
                std::ifstream lf("/proc/loadavg");
                if (lf) lf >> load_1m;
            }
            if (load_1m > 6.5) {
                std::ofstream logf("/mnt/nvme_storage/devvault/daemon.log", std::ios::app);
                logf << "[" << Scrubber::iso8601_now() << "] Interactive load (" << load_1m
                     << ") detected; yielding for 30s...\n";
                std::this_thread::sleep_for(std::chrono::seconds(30));
                continue;
            }

            std::string cycle_ts = Scrubber::iso8601_now();
            CodingAndRoadSync::run_coding_sync(db);
            std::string road_log;
            CodingAndRoadSync::run_road_and_web_sync(db, sources_md_path, (cycle % 6 == 1), &road_log);

            int newly_indexed = 0;
            DocumentClassifier::run_night_classify(db, roots, vault_root, "symlink-vault", 1000, false, &newly_indexed);
            total_new_session += newly_indexed;
            system_fully_categorized = (newly_indexed == 0);

            std::string gpu_telem = Scrubber::run_cmd(
                "/usr/bin/nvidia-smi --query-gpu=name,temperature.gpu,utilization.gpu,memory.used,memory.total --format=csv,noheader 2>/dev/null",
                512
            );

            // Write live Owner inspection dashboard to /mnt/nvme_storage/devvault/daemon_status.txt
            {
                std::ofstream sf("/mnt/nvme_storage/devvault/daemon_status.txt", std::ios::trunc);
                sf << "================================================================================\n"
                   << "  DEVVAULT v2.0 AUTONOMOUS BACKGROUND DAEMON — OWNER INSPECTION DASHBOARD\n"
                   << "================================================================================\n"
                   << "  Daemon PID          : " << getpid() << "\n"
                   << "  Started At          : " << started_at << "\n"
                   << "  Last Completed Pass : " << cycle_ts << " (Cycle #" << cycle << ")\n"
                   << "  1-Min System Load   : " << std::fixed << std::setprecision(2) << load_1m << " (Idle-Aware Threshold: <= 6.50)\n"
                   << "  Newly Indexed (Pass): " << newly_indexed << " files (Session Total: " << total_new_session << ")\n"
                   << "  System State        : "
                   << (system_fully_categorized
                       ? "100% CATEGORIZED & UP TO DATE (Monitoring for new changes every 300s)"
                       : "ACTIVELY CATEGORIZING SYSTEM IN BACKGROUND (Next batch in 15s)")
                   << "\n"
                   << "  GPU Telemetry       : " << Scrubber::trim(gpu_telem) << "\n"
                   << StorageGuard::format_report(db);
                auto cats = db.get_category_stats();
                if (!cats.empty()) {
                    sf << "  INDEXED KNOWLEDGE BASE CATEGORIES:\n";
                    for (const auto& c : cats) {
                        sf << "  • " << std::left << std::setw(38) << c.category
                           << " : " << std::right << std::setw(6) << c.file_count << " items ("
                           << std::fixed << std::setprecision(2) << (c.total_bytes / (1024.0 * 1024.0)) << " MiB)\n";
                    }
                    sf << "================================================================================\n";
                }
            }

            // Append cycle summary to /mnt/nvme_storage/devvault/daemon.log
            {
                std::ofstream lf("/mnt/nvme_storage/devvault/daemon.log", std::ios::app);
                lf << "[" << Scrubber::iso8601_now() << "] Cycle #" << cycle
                   << " complete | newly_indexed=" << newly_indexed
                   << " | session_total=" << total_new_session
                   << " | total_items=" << db.count_table("knowledge_items")
                   << " | total_chunks=" << db.count_table("chunks")
                   << " | state=" << (system_fully_categorized ? "UP_TO_DATE" : "CATEGORIZING")
                   << " | gpu=" << Scrubber::trim(gpu_telem) << "\n";
            }

            cycle++;
            int sleep_sec = system_fully_categorized ? 300 : 15;
            std::this_thread::sleep_for(std::chrono::seconds(sleep_sec));
        }
        return 0;
    }

private:
    static std::string extract_json_id(const std::string& json) {
        size_t pos = json.find("\"id\"");
        if (pos == std::string::npos) return "null";
        pos = json.find(':', pos + 4);
        if (pos == std::string::npos) return "null";
        pos++;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
        if (pos >= json.size()) return "null";
        if (json[pos] == '"') {
            size_t end = json.find('"', pos + 1);
            if (end != std::string::npos) return json.substr(pos, end - pos + 1);
        }
        size_t end = json.find_first_of(",}\r\n ", pos);
        return (end == std::string::npos) ? json.substr(pos) : json.substr(pos, end - pos);
    }
};

} // namespace devvault
