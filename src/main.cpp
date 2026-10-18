#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "db.hpp"
#include "embed_avx.hpp"
#include "mcp_server.hpp"
#include "scrubber.hpp"
#include "sha256.hpp"
#include "subsystems.hpp"

using namespace devvault;

static void print_help() {
    std::cout << "DevVault v2.0.0 (HP Z420 Native C++17 | Sandy Bridge AVX 1.0 | Zero GPU VRAM)\n"
              << "Usage: devvault <subcommand> [options]\n\n"
              << "Subcommands:\n"
              << "  search <query> [--top-k N] [--category CAT]   Run Hybrid FTS5 BM25 + AVX1 384-dim Cosine RRF search\n"
              << "  store --title T --content C [--category CAT]  Store a coding note or hardware finding in DevVault\n"
              << "  coding-sync                                   Index Antigravity transcripts, artifacts & Z420 project repos\n"
              << "  road-sync                                     Sync SOURCES.md + Google Docs & mirror watched web pages\n"
              << "  web-mirror                                    Check watched URLs for weekly SHA-256 changes & log diffs\n"
              << "  night-classify [--mode symlink-vault|move-to-vault] [--limit N] [--dry-run]\n"
              << "                                                Low-priority background personal document crawler & classifier\n"
              << "  test-vision <image_or_pdf> [--endpoint URL]   Test MiniCPM-V 4.6 image/OCR classification under PolyStrata\n"
              << "  storage-guard                                 Live statvfs drive telemetry & 66% Exos/NAS threshold check\n"
              << "  verify-array --source <dir1> --target <dir2>  Verify 100% SHA-256 accuracy for Stage 2B 18TB->36TB Stripe\n"
              << "  mcp                                           Run stdio JSON-RPC 2.0 Model Context Protocol (MCP) server\n"
              << "  serve [--port 8089]                           Run native HTTP/JSON RAG gateway on 127.0.0.1:8089\n"
              << "  --version                                     Print binary architecture & ISA build info\n";
}

int main(int argc, char** argv) {
    try {
        std::error_code ec;
        std::filesystem::create_directories("/mnt/nvme_storage/devvault", ec);
        std::filesystem::create_directories("/mnt/storage_8tb_linux/Tom_Knowledge_Vault", ec);
        std::filesystem::create_directories("/home/tom/.local/bin", ec);
        if (!std::filesystem::exists("/home/tom/.local/bin/devvault", ec)) {
            std::filesystem::create_symlink(
                "/mnt/nvme_storage/ai_projects/Local knowledge base./bin/devvault",
                "/home/tom/.local/bin/devvault",
                ec
            );
        }

        const char* env_db = std::getenv("DEVVAULT_DB_PATH");
        std::string db_path = env_db ? env_db : "/mnt/nvme_storage/devvault/devvault.db";
        std::string sources_md = "/mnt/nvme_storage/ai_projects/Local knowledge base./SOURCES.md";
        std::string vault_root = "/mnt/storage_8tb_linux/Tom_Knowledge_Vault";

        if (argc < 2) {
            print_help();
            return 0;
        }

        std::string cmd = argv[1];
        if (cmd == "--version" || cmd == "-v" || cmd == "version") {
            std::cout << "devvault 2.0.0 (HP Z420 Native C++17 ELF | ISA: Sandy Bridge AVX 1.0 -march=sandybridge -mavx -msse4.2 -mno-avx2 -mno-fma -mno-f16c | 0 MiB GPU VRAM)\n";
            return 0;
        }
        if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            print_help();
            return 0;
        }

        if (cmd == "verify-array") {
            std::string src_dir, tgt_dir;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--source" && i + 1 < argc) src_dir = argv[++i];
                else if (a == "--target" && i + 1 < argc) tgt_dir = argv[++i];
            }
            return StorageGuard::verify_array(src_dir, tgt_dir);
        }

        DevVaultDb db(db_path);

        if (cmd == "mcp") {
            return McpServer::run_stdio(db, sources_md, vault_root);
        }
        if (cmd == "serve") {
            int port = 8089;
            for (int i = 2; i < argc; ++i) {
                if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                    port = std::atoi(argv[++i]);
                }
            }
            return McpServer::run_http_gateway(db, port);
        }
        if (cmd == "storage-guard" || cmd == "status") {
            std::cout << StorageGuard::format_report(db);
            auto cats = db.get_category_stats();
            if (!cats.empty()) {
                std::cout << "  INDEXED KNOWLEDGE BASE CATEGORIES:\n";
                for (const auto& c : cats) {
                    std::cout << "  • " << std::left << std::setw(38) << c.category
                              << " : " << std::right << std::setw(6) << c.file_count << " items ("
                              << std::fixed << std::setprecision(2) << (c.total_bytes / (1024.0 * 1024.0)) << " MiB)\n";
                }
                std::cout << "================================================================================\n";
            }
            return 0;
        }
        if (cmd == "init-all") {
            std::cout << "=== [1/5] Running devvault coding-sync (Antigravity Transcripts & Z420 Projects) ===\n";
            CodingAndRoadSync::run_coding_sync(db);

            std::cout << "\n=== [2/5] Running devvault road-sync & web-mirror (SOURCES.md + Google Docs + Web Cache) ===\n";
            CodingAndRoadSync::run_road_and_web_sync(db, sources_md, true);

            std::cout << "\n=== [3/5] Running devvault night-classify (Personal Document Vault on 8TB WD; Hitachi Protected) ===\n";
            std::vector<std::string> roots = {
                "/mnt/storage_8tb_linux/documents",
                "/mnt/storage_8tb_linux/downloads",
                "/mnt/storage_8tb_linux/Computer stuff",
                "/mnt/storage_8tb_linux/music",
                "/home/tom/Documents",
                "/home/tom/Downloads",
                "/mnt/win7_backup",
                "/mnt/samsung_storage"
            };
            DocumentClassifier::run_night_classify(db, roots, vault_root, "symlink-vault", 2000, false);

            std::cout << "\n=== [4/5] Running devvault storage-guard (Live Z420 Drive & Vector DB Telemetry) ===\n";
            std::cout << StorageGuard::format_report(db);
            auto cats = db.get_category_stats();
            if (!cats.empty()) {
                std::cout << "  INDEXED KNOWLEDGE BASE CATEGORIES:\n";
                for (const auto& c : cats) {
                    std::cout << "  • " << std::left << std::setw(38) << c.category
                              << " : " << std::right << std::setw(6) << c.file_count << " items ("
                              << std::fixed << std::setprecision(2) << (c.total_bytes / (1024.0 * 1024.0)) << " MiB)\n";
                }
                std::cout << "================================================================================\n";
            }

            std::cout << "\n=== [5/5] Verifying Hybrid FTS5 BM25 + Sandy Bridge AVX1 384-Dim Vector RRF Search ===\n";
            auto hits = db.hybrid_search("Minisforum N5 Air Slot 4 Exos RAID 10", 5);
            for (size_t i = 0; i < hits.size(); ++i) {
                const auto& h = hits[i];
                std::cout << "[" << (i + 1) << "] " << h.title << " (" << h.category << " | RRF="
                          << std::fixed << std::setprecision(4) << h.rrf_score
                          << ", Cosine=" << std::setprecision(3) << h.cosine_score << ")\n"
                          << "    URI: " << h.source_uri << "\n"
                          << "    Snippet: " << h.snippet << "\n";
            }
            return 0;
        }
        if (cmd == "test-vision" || cmd == "classify-image") {
            std::string img_path;
            std::string endpoint;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--endpoint" && i + 1 < argc) endpoint = argv[++i];
                else if (img_path.empty()) img_path = a;
            }
            if (img_path.empty()) {
                std::cerr << "Usage: devvault test-vision <image_or_pdf_path> [--endpoint http://127.0.0.1:8080/v1/chat/completions]\n";
                return 1;
            }
            std::cout << "================================================================================\n";
            std::cout << "  DEVVAULT VISION TEST: MiniCPM-V 4.6 (GTX 1050 Ti Single-Patch -> Future GPU Auto-Scale)\n";
            std::cout << "  Target File: " << img_path << "\n";
            std::cout << "================================================================================\n";
            auto vc = MiniCpmVision::classify_image(img_path, endpoint);
            std::cout << "  Status     : " << (vc.success ? "ONLINE / CLASSIFIED" : "OFFLINE / FALLBACK") << "\n"
                      << "  GPU Profile: " << vc.gpu_profile << "\n"
                      << "  Backend    : " << vc.backend_used << "\n"
                      << "  Model      : " << vc.model_used << "\n"
                      << "  Latency    : " << std::fixed << std::setprecision(1) << vc.latency_ms << " ms\n"
                      << "  Category   : " << vc.category << " (conf=" << std::setprecision(2) << vc.confidence << ")\n"
                      << "  Title      : " << vc.title << "\n"
                      << "  Summary    : " << vc.summary << "\n"
                      << "  OCR Text   : " << vc.ocr_text << "\n";
            if (vc.success) {
                int64_t id = db.upsert_item(vc.title, "file://" + img_path, "vision_classified_image", vc.category,
                                            "Title: " + vc.title + "\nSummary: " + vc.summary + "\nOCR: " + vc.ocr_text);
                std::cout << "  RAG DB ID  : " << id << " (indexed into /mnt/nvme_storage/devvault/devvault.db)\n";
            }
            std::cout << "================================================================================\n";
            return vc.success ? 0 : 2;
        }
        if (cmd == "coding-sync") {
            return CodingAndRoadSync::run_coding_sync(db);
        }
        if (cmd == "road-sync" || cmd == "web-mirror") {
            return CodingAndRoadSync::run_road_and_web_sync(db, sources_md, true);
        }
        if (cmd == "night-classify") {
            std::string mode = "symlink-vault";
            int limit = 500;
            bool dry_run = false;
            std::vector<std::string> roots;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--mode" && i + 1 < argc) mode = argv[++i];
                else if (a == "--limit" && i + 1 < argc) limit = std::atoi(argv[++i]);
                else if (a == "--dry-run") dry_run = true;
                else if (a == "--root" && i + 1 < argc) roots.push_back(argv[++i]);
            }
            if (roots.empty()) {
                roots = {
                    "/mnt/storage_8tb_linux/documents",
                    "/mnt/storage_8tb_linux/downloads",
                    "/mnt/storage_8tb_linux/Computer stuff",
                    "/mnt/storage_8tb_linux/music",
                    "/home/tom/Documents",
                    "/home/tom/Downloads",
                    "/mnt/win7_backup",
                    "/mnt/samsung_storage"
                };
            }
            return DocumentClassifier::run_night_classify(db, roots, vault_root, mode, limit, dry_run);
        }
        if (cmd == "store") {
            std::string title, content, cat = "05_Computer_Programming_and_AI", suri;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--title" && i + 1 < argc) title = argv[++i];
                else if (a == "--content" && i + 1 < argc) content = argv[++i];
                else if (a == "--category" && i + 1 < argc) cat = argv[++i];
                else if (a == "--uri" && i + 1 < argc) suri = argv[++i];
            }
            if (title.empty() || content.empty()) {
                std::cerr << "Usage: devvault store --title \"...\" --content \"...\" [--category CAT]\n";
                return 1;
            }
            if (suri.empty()) suri = "devvault://notes/" + Sha256::hash_string(title + content).substr(0, 16);
            int64_t id = db.upsert_item(title, suri, "manual_note", cat, content);
            std::cout << "[devvault store] Stored item_id=" << id << " (" << title << ") in " << cat << "\n";
            return 0;
        }
        if (cmd == "search") {
            std::string query;
            int top_k = 8;
            std::string cat;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--top-k" && i + 1 < argc) top_k = std::atoi(argv[++i]);
                else if (a == "--category" && i + 1 < argc) cat = argv[++i];
                else {
                    if (!query.empty()) query += " ";
                    query += a;
                }
            }
            auto hits = db.hybrid_search(query, top_k, cat);
            std::cout << "================================================================================\n";
            std::cout << "  DEVVAULT HYBRID RAG SEARCH: \"" << query << "\" (" << hits.size() << " results)\n";
            std::cout << "================================================================================\n";
            for (size_t i = 0; i < hits.size(); ++i) {
                const auto& h = hits[i];
                std::cout << "[" << (i + 1) << "] " << h.title << " (" << h.category << " | " << h.item_type << ")\n"
                          << "    URI    : " << h.source_uri << "\n"
                          << "    Scores : RRF=" << std::fixed << std::setprecision(4) << h.rrf_score
                          << " | AVX1 Cosine=" << std::setprecision(3) << h.cosine_score
                          << " | FTS5 BM25=" << h.bm25_score << "\n"
                          << "    Snippet: " << h.snippet << "\n"
                          << "--------------------------------------------------------------------------------\n";
            }
            return 0;
        }

        print_help();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[devvault fatal] " << ex.what() << "\n";
        return 1;
    }
}
