#pragma once
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>
#include "db.hpp"
#include "embed_avx.hpp"
#include "scrubber.hpp"
#include "sha256.hpp"
#include "vision_minicpm.hpp"

namespace devvault {
namespace fs = std::filesystem;

struct MountStat {
    std::string label;
    std::string path;
    std::string role;
    bool mounted = false;
    double total_gib = 0.0;
    double used_gib = 0.0;
    double free_gib = 0.0;
    double used_pct = 0.0;
    double alert_pct = 85.0;
    bool alert_triggered = false;
};

class StorageGuard {
public:
    static std::vector<MountStat> inspect_mounts() {
        struct Target {
            const char* label;
            const char* path;
            const char* role;
            double alert_pct;
        };
        static const Target targets[] = {
            {"2TB NVMe (Vectors, DB & 256GB Swap)", "/mnt/nvme_storage", "Hot Vector + FTS5 SQLite DB & Swapfile", 80.0},
            {"8TB WD HDD (Stage 1 Knowledge Vault)", "/mnt/storage_8tb_linux", "Active Tom_Knowledge_Vault & Backups", 80.0},
            {"8TB WD Partition 1 (Win7 Backup)", "/mnt/win7_backup", "Historical Windows 7 Document Source", 90.0},
            {"2TB Hitachi HDD (Cron & SSD Backups)", "/mnt/hitachi_3AC60", "PROTECTED: Cron staging_cache & system_snapshots", 85.0},
            {"500GB Samsung SATA SSD (Top ODD Bay)", "/mnt/samsung_storage", "Secondary Fast Scratch / SSD Storage", 85.0},
            {"Stage 2A/2B Exos Array (18TB Mirror / 36TB RAID10)", "/mnt/exos_mirror", "66% Alert Trigger (12TB on 18TB / 24TB on 36TB)", 66.0},
            {"Stage 3 Minisforum N5 Air AI NAS", "/mnt/nas_vault", "External 25GbE/10GbE/OCuLink AI NAS Tier", 80.0}
        };

        std::vector<MountStat> out;
        for (const auto& t : targets) {
            MountStat ms;
            ms.label = t.label;
            ms.path = t.path;
            ms.role = t.role;
            ms.alert_pct = t.alert_pct;

            struct statvfs vfs{};
            if (fs::exists(t.path) && statvfs(t.path, &vfs) == 0 && vfs.f_blocks > 0) {
                ms.mounted = true;
                uint64_t total_b = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
                uint64_t avail_b = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
                uint64_t used_b  = (total_b >= avail_b) ? (total_b - avail_b) : 0;
                ms.total_gib = static_cast<double>(total_b) / (1024.0 * 1024.0 * 1024.0);
                ms.free_gib  = static_cast<double>(avail_b) / (1024.0 * 1024.0 * 1024.0);
                ms.used_gib  = static_cast<double>(used_b)  / (1024.0 * 1024.0 * 1024.0);
                ms.used_pct  = (ms.total_gib > 0.0) ? (ms.used_gib * 100.0 / ms.total_gib) : 0.0;
                ms.alert_triggered = (ms.used_pct >= ms.alert_pct);
            }
            out.push_back(ms);
        }
        return out;
    }

    static uint64_t file_or_dir_bytes(const std::string& p) {
        std::error_code ec;
        if (!fs::exists(p, ec)) return 0;
        if (fs::is_regular_file(p, ec)) {
            return fs::file_size(p, ec);
        }
        uint64_t total = 0;
        for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) continue;
            if (it->is_regular_file(ec) && !it->is_symlink(ec)) {
                total += it->file_size(ec);
            }
        }
        return total;
    }

    static std::string format_report(DevVaultDb& db) {
        auto mounts = inspect_mounts();
        uint64_t db_bytes = file_or_dir_bytes(db.db_path()) +
                            file_or_dir_bytes(db.db_path() + "-wal") +
                            file_or_dir_bytes(db.db_path() + "-shm");
        uint64_t swap_bytes = file_or_dir_bytes("/mnt/nvme_storage/swapfile");

        int64_t items_cnt = db.count_table("knowledge_items");
        int64_t chunks_cnt = db.count_table("chunks");
        int64_t vectors_cnt = db.count_table("chunk_vectors");
        int64_t ledger_cnt = db.count_table("file_move_ledger");
        int64_t web_cnt = db.count_table("web_snapshots");

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "================================================================================\n";
        oss << "  DEVVAULT v2.0 STORAGE-GUARD & Z420 CAPACITY TELEMETRY (" << Scrubber::iso8601_now() << ")\n";
        oss << "================================================================================\n";
        oss << "  Active Vector/FTS5 DB : " << db.db_path() << " (" << (db_bytes / (1024.0 * 1024.0)) << " MiB)\n";
        oss << "  NVMe Swapfile Status  : /mnt/nvme_storage/swapfile (" << (swap_bytes / (1024.0 * 1024.0 * 1024.0)) << " GiB active)\n";
        oss << "  Indexed RAG Corpus    : " << items_cnt << " items | " << chunks_cnt << " FTS5 chunks | "
            << vectors_cnt << " AVX1 384-dim vectors\n";
        oss << "  Vault Ledger & Web    : " << ledger_cnt << " classified files | " << web_cnt << " mirrored web pages\n";
        oss << "--------------------------------------------------------------------------------\n";
        oss << "  DRIVE / TIER                                  USED / TOTAL GiB    USE%   STATUS\n";
        oss << "--------------------------------------------------------------------------------\n";
        for (const auto& m : mounts) {
            oss << "  " << std::left << std::setw(44) << m.label.substr(0, 44);
            if (!m.mounted) {
                oss << "  [NOT YET MOUNTED - PLANNED TIER]\n";
            } else {
                std::ostringstream sz;
                sz << std::fixed << std::setprecision(1) << m.used_gib << " / " << m.total_gib << " GiB";
                oss << "  " << std::right << std::setw(18) << sz.str()
                    << "  " << std::setw(5) << std::setprecision(1) << m.used_pct << "%  "
                    << (m.alert_triggered ? "[ALERT: >= THRESHOLD!]" : "[OK - HEALTHY]") << "\n";
            }
        }
        oss << "--------------------------------------------------------------------------------\n";
        auto gprof = MiniCpmVision::detect_hardware_profile();
        oss << "  Z420 DUAL-GPU TOPOLOGY & ROLE ASSIGNMENT (Slot 2 Agentic + Slot 5 35B+ AI):\n";
        oss << "  • Active Vision Profile : " << gprof.gpu_tier << "\n";
        for (const auto& g : gprof.detected_gpus) {
            oss << "  • GPU #" << g.index << " (" << g.vendor << " " << g.name << ", " << g.vram_total_mib
                << " MiB, Compute " << g.compute_cap << ") -> " << g.assigned_role << "\n";
        }
        oss << "  • Final Target Topology : Slot 2 (16-24GB Video + Background Agentic / MiniCPM-V 4.6)\n"
            << "                          + Slot 5 (24GB Quadro P6000 [sm_61] or 32GB Intel Arc B70 [35B+ AI])\n";
        oss << "--------------------------------------------------------------------------------\n";
        oss << "  STAGE 2A/2B/3 66% CAPACITY TRIGGER RULES:\n";
        oss << "  • Stage 2A (2x 18TB Exos Mirror = 18.0 TB): Alert at 66% (12.0 TB) -> Add Exos #3 + #4\n";
        oss << "  • Stage 2B (4x 18TB Exos Mirrored-Striped RAID 10 = 36.0 TB):\n";
        oss << "      1. Keep 18TB Mirror (Exos #1+#2) untouched; bring Exos #3+#4 online as 36TB Stripe\n";
        oss << "      2. Copy data to 36TB Stripe & run `devvault verify-array` (100% SHA-256 check)\n";
        oss << "      3. Attach Exos #1+#2 to mirror the 36TB Stripe (RAID 10) | Alert at 66% (24.0 TB)\n";
        oss << "  • Stage 3 (Triggered at 66% / 24.0 TB): Slot 4 Minisforum ENPBA/ESP4B -> N5 Air AI NAS\n";
        oss << "================================================================================\n";
        return oss.str();
    }

    // Stage 2B Step 3 Verifier: Verify 100% SHA-256 accuracy between primary 18TB mirror and new 36TB striped volume
    static int verify_array(const std::string& source_dir, const std::string& target_dir) {
        std::cout << "[devvault verify-array] Starting cryptographic SHA-256 verification:\n"
                  << "  Source (Primary 18TB Mirror) : " << source_dir << "\n"
                  << "  Target (New 36TB Stripe)     : " << target_dir << "\n";
        std::error_code ec;
        if (!fs::exists(source_dir, ec) || !fs::exists(target_dir, ec)) {
            std::cerr << "[ERROR] Both source and target directories must exist.\n";
            return 1;
        }
        uint64_t files_checked = 0;
        uint64_t bytes_checked = 0;
        uint64_t mismatches = 0;

        for (auto it = fs::recursive_directory_iterator(source_dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec) || it->is_symlink(ec)) continue;
            fs::path rel = fs::relative(it->path(), source_dir, ec);
            if (ec) continue;
            fs::path tgt = fs::path(target_dir) / rel;
            if (!fs::exists(tgt, ec)) {
                std::cerr << "  [MISSING] " << tgt.string() << "\n";
                mismatches++;
                continue;
            }
            uint64_t s_sz = it->file_size(ec);
            uint64_t t_sz = fs::file_size(tgt, ec);
            if (s_sz != t_sz) {
                std::cerr << "  [SIZE MISMATCH] " << rel.string() << " (" << s_sz << " vs " << t_sz << ")\n";
                mismatches++;
                continue;
            }
            std::string s_sha = Sha256::hash_file(it->path().string());
            std::string t_sha = Sha256::hash_file(tgt.string());
            if (s_sha != t_sha) {
                std::cerr << "  [SHA-256 MISMATCH] " << rel.string() << "\n";
                mismatches++;
                continue;
            }
            files_checked++;
            bytes_checked += s_sz;
        }

        std::cout << "[devvault verify-array] Checked " << files_checked << " files ("
                  << (bytes_checked / (1024.0 * 1024.0)) << " MiB) | Mismatches: " << mismatches << "\n";
        if (mismatches == 0) {
            std::cout << "[PASS] 100% SHA-256 accuracy confirmed. Safe to proceed to Step 4 (attach Exos #1+#2 as mirrors of the 36TB stripe).\n";
            return 0;
        } else {
            std::cerr << "[FAIL] Verification detected " << mismatches << " errors. DO NOT touch the primary 18TB mirror!\n";
            return 2;
        }
    }
};

class DocumentClassifier {
public:
    struct ClassifyResult {
        std::string category;
        double confidence;
    };

    static const std::vector<std::string>& seeded_categories() {
        static const std::vector<std::string> cats = {
            "01_Receipts_and_Financial",
            "02_Automotive_and_Cars",
            "03_Electronics_and_Circuits",
            "04_Computer_Hardware_and_Systems",
            "05_Computer_Programming_and_AI",
            "06_Hunting_and_Outdoors",
            "07_Firearms_and_Ballistics",
            "08_History_and_Archives",
            "09_Medical_and_Health",
            "10_Politics_and_Policy",
            "11_Audio_CDs_and_Music_Library"
        };
        return cats;
    }

    static void ensure_vault_directories(const std::string& vault_root) {
        std::error_code ec;
        fs::create_directories(vault_root, ec);
        for (const auto& c : seeded_categories()) {
            fs::create_directories(fs::path(vault_root) / c, ec);
        }
        fs::create_directories(fs::path(vault_root) / "12_Emergent_and_General_Docs", ec);
    }

    // Video Guardrail: Explicitly block DVD/Blu-Ray video files so Z420 never becomes a bloated video server
    static bool is_excluded_video_or_system_path(const fs::path& p) {
        std::string s = p.string();
        // Protect 2TB Hitachi cron & SSD snapshot directories
        if (s.find("/mnt/hitachi_3AC60/staging_cache") == 0 ||
            s.find("/mnt/hitachi_3AC60/system_snapshots") == 0) {
            return true;
        }
        // Skip system/cache/venv/git/build directories and the vault itself
        if (s.find("/Tom_Knowledge_Vault") != std::string::npos ||
            s.find("/.git/") != std::string::npos ||
            s.find("/.venv/") != std::string::npos ||
            s.find("/node_modules/") != std::string::npos ||
            s.find("/__pycache__/") != std::string::npos ||
            s.find("/huggingface_cache/") != std::string::npos ||
            s.find("/ollama_models/") != std::string::npos ||
            s.find("/SteamLibrary/") != std::string::npos ||
            s.find("/lost+found") != std::string::npos ||
            s.find("/.cache/") != std::string::npos ||
            s.find("/.local/share/Trash") != std::string::npos ||
            s.find("/system_backups/") != std::string::npos ||
            s.find("/VIDEO_TS/") != std::string::npos ||
            s.find("/BDMV/") != std::string::npos) {
            return true;
        }
        std::string ext = Scrubber::to_lower(p.extension().string());
        static const char* video_exts[] = {
            ".vob", ".m2ts", ".mts", ".mkv", ".mp4", ".avi", ".mov", ".wmv", ".iso", ".img", ".bin", ".gguf", ".safetensors", ".pt", ".pth", ".Swap", nullptr
        };
        for (int i = 0; video_exts[i] != nullptr; ++i) {
            if (ext == video_exts[i]) return true;
        }
        return false;
    }

    static bool is_supported_document_ext(const std::string& ext) {
        static const char* exts[] = {
            ".pdf", ".docx", ".odt", ".txt", ".md", ".rst", ".csv", ".tsv",
            ".json", ".yaml", ".yml", ".toml", ".ini", ".conf", ".html", ".htm",
            ".xml", ".sh", ".py", ".rs", ".c", ".cpp", ".h", ".hpp", ".go", ".sql",
            ".jpg", ".jpeg", ".png", ".webp", ".tiff", ".tif", ".bmp",
            ".flac", ".mp3", ".wav", ".ogg", ".m4a", ".cue", ".m3u", nullptr
        };
        for (int i = 0; exts[i] != nullptr; ++i) {
            if (ext == exts[i]) return true;
        }
        return false;
    }

    static bool is_image_ext(const std::string& ext) {
        return (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".tiff" || ext == ".tif" || ext == ".bmp");
    }

    static std::string extract_document_text(const fs::path& p, size_t max_chars = 24000, bool use_vision = false) {
        std::string ext = Scrubber::to_lower(p.extension().string());
        std::string path_str = p.string();

        // Audio CD / Music Library files: index path + track metadata without reading raw PCM bytes
        if (ext == ".flac" || ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".m4a" || ext == ".cue" || ext == ".m3u") {
            return "Audio CD / Music Track Metadata: " + p.filename().string() + " | Album/Folder: " + p.parent_path().filename().string() + " | Path: " + path_str;
        }

        // Image files (.jpg, .png, .webp, .tiff, .bmp): route to MiniCPM-V 4.6 under PolyStrata when vision is enabled
        if (is_image_ext(ext)) {
            if (use_vision) {
                auto vc = MiniCpmVision::classify_image(p);
                if (vc.success) {
                    return "MiniCPM-V 4.6 Vision Classification [" + vc.category + "]: " + vc.title +
                           "\nSummary: " + vc.summary + "\nOCR Text: " + vc.ocr_text;
                }
            }
            return "Image / Scanned Document: " + p.filename().string() + " | Folder: " + p.parent_path().filename().string() + " | Path: " + path_str;
        }

        // PDF extraction via /usr/bin/pdftotext + automatic MiniCPM-V 4.6 fallback for scanned PDFs
        if (ext == ".pdf") {
            std::string escaped;
            for (char c : path_str) {
                if (c == '\'') escaped += "'\\''";
                else escaped.push_back(c);
            }
            std::string cmd = "/usr/bin/pdftotext -l 15 -q '" + escaped + "' -";
            std::string txt = Scrubber::run_cmd(cmd, max_chars);
            if (Scrubber::trim(txt).size() > 20) {
                return txt.substr(0, max_chars);
            }
            if (use_vision) {
                auto vc = MiniCpmVision::classify_image(p);
                if (vc.success) {
                    return "MiniCPM-V 4.6 Scanned PDF OCR [" + vc.category + "]: " + vc.title +
                           "\nSummary: " + vc.summary + "\nOCR Text: " + vc.ocr_text;
                }
            }
            return "PDF Document: " + p.filename().string() + " in " + p.parent_path().string();
        }

        // DOCX / ODT extraction via unzip XML stream
        if (ext == ".docx" || ext == ".odt") {
            std::string escaped;
            for (char c : path_str) {
                if (c == '\'') escaped += "'\\''";
                else escaped.push_back(c);
            }
            std::string xml_entry = (ext == ".docx") ? "word/document.xml" : "content.xml";
            std::string cmd = "/usr/bin/unzip -p '" + escaped + "' " + xml_entry;
            std::string raw_xml = Scrubber::run_cmd(cmd, max_chars * 2);
            if (!raw_xml.empty()) {
                return strip_tags(raw_xml).substr(0, max_chars);
            }
        }

        // Plain text / code / markdown / HTML / JSON / CSV
        std::ifstream ifs(path_str, std::ios::binary);
        if (!ifs) return "";
        std::string buf(max_chars, '\0');
        ifs.read(&buf[0], static_cast<std::streamsize>(max_chars));
        buf.resize(static_cast<size_t>(ifs.gcount()));

        // Reject binary blobs posing as text
        size_t nul_count = 0;
        for (unsigned char c : buf) {
            if (c == 0) nul_count++;
        }
        if (nul_count > 4) {
            return "Binary/Structured File: " + p.filename().string();
        }
        if (ext == ".html" || ext == ".htm" || ext == ".xml") {
            return strip_tags(buf);
        }
        return buf;
    }

    static ClassifyResult classify(const fs::path& p, const std::string& content, std::map<std::string, int>& emergent_counter) {
        std::string ext = Scrubber::to_lower(p.extension().string());
        if (ext == ".flac" || ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".m4a" || ext == ".cue" || ext == ".m3u") {
            return {"11_Audio_CDs_and_Music_Library", 0.98};
        }

        std::string combined = Scrubber::to_lower(p.string() + " " + content.substr(0, 6000));
        auto vec = Embedder::embed(combined);

        // Compare concept dimensions 0..10 populated by Embedder
        const auto& cats = seeded_categories();
        int best_idx = -1;
        float best_score = 0.05f;
        for (int i = 0; i < 11; ++i) {
            float sc = vec[i] + vec[i + 11];
            if (sc > best_score) {
                best_score = sc;
                best_idx = i;
            }
        }

        // Extension heuristics for programming/hardware/receipts
        if (best_idx < 0) {
            if (ext == ".rs" || ext == ".cpp" || ext == ".hpp" || ext == ".c" || ext == ".h" || ext == ".py" || ext == ".sh" || ext == ".go" || ext == ".sql") {
                return {"05_Computer_Programming_and_AI", 0.88};
            }
            if (combined.find("z420") != std::string::npos || combined.find("xeon") != std::string::npos || combined.find("nvme") != std::string::npos || combined.find("raid") != std::string::npos) {
                return {"04_Computer_Hardware_and_Systems", 0.85};
            }
            if (combined.find("invoice") != std::string::npos || combined.find("receipt") != std::string::npos || combined.find("order") != std::string::npos) {
                return {"01_Receipts_and_Financial", 0.85};
            }
        }

        if (best_idx >= 0 && best_idx < static_cast<int>(cats.size())) {
            double conf = std::min(0.99, 0.65 + static_cast<double>(best_score) * 0.4);
            return {cats[best_idx], conf};
        }

        // Tier B — Emergent Pattern Discovery based on parent folder motif
        std::string parent_name = Scrubber::to_lower(p.parent_path().filename().string());
        std::string clean_motif;
        for (unsigned char c : parent_name) {
            if (std::isalnum(c)) clean_motif.push_back(static_cast<char>(c));
            else if (!clean_motif.empty() && clean_motif.back() != '_') clean_motif.push_back('_');
        }
        if (clean_motif.size() >= 3 && clean_motif != "tom" && clean_motif != "documents" && clean_motif != "downloads") {
            emergent_counter[clean_motif]++;
            if (emergent_counter[clean_motif] >= 5) {
                return {"12_Emergent_" + clean_motif, 0.78};
            }
        }
        return {"12_Emergent_and_General_Docs", 0.60};
    }

    static int run_night_classify(
        DevVaultDb& db,
        const std::vector<std::string>& scan_roots,
        const std::string& vault_root,
        const std::string& mode,
        int max_files,
        bool dry_run
    ) {
        // Low-priority background scheduling (nice 19) + single-instance flock
        nice(19);
        int lock_fd = open("/tmp/devvault_night_classify.lock", O_CREAT | O_RDWR, 0666);
        if (lock_fd >= 0) {
            if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
                std::cerr << "[night-classify] Another night-classify instance is already running. Exiting cleanly.\n";
                close(lock_fd);
                return 0;
            }
        }

        if (!dry_run) {
            ensure_vault_directories(vault_root);
        }

        int processed = 0;
        int indexed_new = 0;
        int dedup_skipped = 0;
        std::map<std::string, int> cat_counts;
        std::map<std::string, int> emergent_counter;

        std::cout << "[devvault night-classify] Starting scan (mode=" << mode
                  << ", max_files=" << max_files << ", dry_run=" << (dry_run ? "true" : "false") << ")\n"
                  << "  Protected paths : /mnt/hitachi_3AC60/staging_cache, /mnt/hitachi_3AC60/system_snapshots\n"
                  << "  Target Vault    : " << vault_root << "\n";

        for (const auto& root : scan_roots) {
            std::error_code ec;
            if (!fs::exists(root, ec)) continue;
            for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) continue;
                const fs::path p = it->path();
                if (is_excluded_video_or_system_path(p)) {
                    if (it->is_directory(ec)) it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(ec) || it->is_symlink(ec)) continue;

                std::string ext = Scrubber::to_lower(p.extension().string());
                if (!is_supported_document_ext(ext)) continue;

                uint64_t fsize = it->file_size(ec);
                if (ec || fsize == 0 || fsize > 100ULL * 1024 * 1024) continue; // Skip empty or >100MB single blobs

                std::string sha = Sha256::hash_file(p.string(), 4 * 1024 * 1024); // Fast 4MB prefix + content SHA-256
                if (sha.empty()) continue;

                std::string text = extract_document_text(p);
                if (text.empty()) continue;

                auto cr = classify(p, text, emergent_counter);
                cat_counts[cr.category]++;
                processed++;

                fs::path cat_dir = fs::path(vault_root) / cr.category;
                std::string safe_name = sha.substr(0, 8) + "_" + p.filename().string();
                fs::path vault_link = cat_dir / safe_name;

                if (!dry_run) {
                    fs::create_directories(cat_dir, ec);
                    if (mode == "symlink-vault") {
                        if (!fs::exists(vault_link, ec)) {
                            fs::create_symlink(p, vault_link, ec);
                        }
                    } else if (mode == "move-to-vault") {
                        if (!fs::exists(vault_link, ec)) {
                            fs::rename(p, vault_link, ec);
                            if (ec) {
                                fs::copy_file(p, vault_link, fs::copy_options::overwrite_existing, ec);
                                if (!ec) fs::remove(p, ec);
                            }
                        }
                    }
                    db.record_file_ledger(p.string(), vault_link.string(), cr.category, cr.confidence, mode, sha, static_cast<int64_t>(fsize));
                    int64_t id = db.upsert_item(
                        p.filename().string(),
                        "file://" + p.string(),
                        "personal_doc",
                        cr.category,
                        "File: " + p.string() + "\nCategory: " + cr.category + "\n\n" + text,
                        static_cast<int64_t>(fsize),
                        sha
                    );
                    if (id > 0) indexed_new++;
                    else dedup_skipped++;
                }

                if (max_files > 0 && processed >= max_files) break;
            }
            if (max_files > 0 && processed >= max_files) break;
        }

        std::cout << "[devvault night-classify] Completed pass: " << processed << " files classified ("
                  << indexed_new << " newly embedded, " << dedup_skipped << " unchanged/deduplicated).\n";
        for (const auto& kv : cat_counts) {
            std::cout << "  • " << std::left << std::setw(36) << kv.first << " : " << kv.second << " files\n";
        }
        if (lock_fd >= 0) {
            flock(lock_fd, LOCK_UN);
            close(lock_fd);
        }
        return 0;
    }

    static std::string strip_tags(const std::string& html) {
        std::string out;
        out.reserve(html.size());
        bool in_tag = false;
        bool in_script = false;
        bool in_style = false;
        for (size_t i = 0; i < html.size(); ++i) {
            char c = html[i];
            if (c == '<') {
                if (i + 7 < html.size()) {
                    std::string tag = Scrubber::to_lower(html.substr(i, 8));
                    if (tag.find("<script") == 0) in_script = true;
                    else if (tag.find("</script") == 0) in_script = false;
                    else if (tag.find("<style") == 0) in_style = true;
                    else if (tag.find("</style") == 0) in_style = false;
                }
                in_tag = true;
                out.push_back(' ');
            } else if (c == '>') {
                in_tag = false;
            } else if (!in_tag && !in_script && !in_style) {
                out.push_back(c);
            }
        }
        // Collapse whitespace
        std::string clean;
        clean.reserve(out.size());
        bool last_ws = false;
        for (char c : out) {
            if (c == '\r' || c == '\t') c = ' ';
            if (c == ' ') {
                if (!last_ws) clean.push_back(' ');
                last_ws = true;
            } else {
                clean.push_back(c);
                last_ws = false;
            }
        }
        return clean;
    }
};

class CodingAndRoadSync {
public:
    // Index Antigravity conversation transcripts, artifacts, and Z420 project documentation
    static int run_coding_sync(DevVaultDb& db) {
        int indexed = 0;
        std::error_code ec;

        // 1. Index Antigravity brain artifacts & transcripts
        std::string brain_root = "/home/tom/.gemini/antigravity/brain";
        if (fs::exists(brain_root, ec)) {
            for (const auto& conv_entry : fs::directory_iterator(brain_root, ec)) {
                if (!conv_entry.is_directory(ec)) continue;
                std::string conv_id = conv_entry.path().filename().string();

                // Index markdown artifacts in each conversation
                for (const auto& f : fs::directory_iterator(conv_entry.path(), ec)) {
                    if (!f.is_regular_file(ec)) continue;
                    std::string ext = f.path().extension().string();
                    if (ext == ".md") {
                        std::string txt = DocumentClassifier::extract_document_text(f.path(), 32000);
                        if (txt.size() > 40) {
                            int64_t id = db.upsert_item(
                                "Antigravity Session Artifact (" + conv_id.substr(0, 8) + "): " + f.path().filename().string(),
                                "file://" + f.path().string(),
                                "coding_artifact",
                                "05_Computer_Programming_and_AI",
                                txt,
                                static_cast<int64_t>(f.file_size(ec))
                            );
                            if (id > 0) indexed++;
                        }
                    }
                }

                // Index USER_INPUT and PLANNER_RESPONSE steps from transcript.jsonl
                fs::path tpath = conv_entry.path() / ".system_generated" / "logs" / "transcript.jsonl";
                if (fs::exists(tpath, ec)) {
                    std::ifstream ifs(tpath.string());
                    std::string line;
                    std::ostringstream digest;
                    digest << "Antigravity Coding Session Transcript (" << conv_id << "):\n";
                    int lines_added = 0;
                    while (std::getline(ifs, line)) {
                        if (line.find("\"USER_INPUT\"") != std::string::npos ||
                            line.find("\"PLANNER_RESPONSE\"") != std::string::npos) {
                            std::string role = (line.find("\"USER_INPUT\"") != std::string::npos) ? "Owner (Tom)" : "Antigravity";
                            std::string content = Scrubber::json_get_string(line, "content");
                            if (content.size() > 25) {
                                digest << "\n[" << role << "]: " << content.substr(0, 1500) << "\n";
                                lines_added++;
                            }
                        }
                    }
                    std::string full_digest = digest.str();
                    if (lines_added > 0 && full_digest.size() > 80) {
                        int64_t id = db.upsert_item(
                            "Coding Session Transcript " + conv_id.substr(0, 8),
                            "conversation://" + conv_id,
                            "coding_transcript",
                            "05_Computer_Programming_and_AI",
                            full_digest,
                            static_cast<int64_t>(full_digest.size())
                        );
                        if (id > 0) indexed++;
                    }
                }
            }
        }

        // 2. Index local AI project docs (/mnt/nvme_storage/ai_projects & /mnt/storage_8tb_linux/python_projects)
        const char* proj_roots[] = {
            "/mnt/nvme_storage/ai_projects/Local knowledge base.",
            "/mnt/nvme_storage/ai_projects/polystrata/docs",
            "/mnt/nvme_storage/ai_projects/polystrata/bench",
            "/mnt/storage_8tb_linux/python_projects/PolyStrata_Z420/docs",
            nullptr
        };
        for (int i = 0; proj_roots[i] != nullptr; ++i) {
            if (!fs::exists(proj_roots[i], ec)) continue;
            for (auto it = fs::recursive_directory_iterator(proj_roots[i], fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec || !it->is_regular_file(ec)) continue;
                std::string ext = Scrubber::to_lower(it->path().extension().string());
                if (ext == ".md" || ext == ".json" || ext == ".hpp" || ext == ".cpp" || ext == ".py" || ext == ".sh") {
                    std::string txt = DocumentClassifier::extract_document_text(it->path(), 32000);
                    if (txt.size() > 30) {
                        std::string cat = (txt.find("Exos") != std::string::npos || txt.find("Z420") != std::string::npos)
                                          ? "04_Computer_Hardware_and_Systems"
                                          : "05_Computer_Programming_and_AI";
                        int64_t id = db.upsert_item(
                            it->path().filename().string(),
                            "file://" + it->path().string(),
                            "project_code_or_doc",
                            cat,
                            txt,
                            static_cast<int64_t>(it->file_size(ec))
                        );
                        if (id > 0) indexed++;
                    }
                }
            }
        }

        // 3. Update Owner Profile Signals (devvault://owner/profile)
        db.set_owner_signal("owner_name", "Tom (@Your Majesty)");
        db.set_owner_signal("workstation_hardware", "HP Z420 Workstation: Intel Xeon E5-2690 v1 (8C/16T Sandy Bridge AVX1; upgrading to Xeon E5-2697 v2 12C/24T Ivy Bridge), 64 GB ECC DDR3 RAM, GTX 1050 Ti 4GB VRAM, 2TB NVMe (/mnt/nvme_storage + 256GB swapfile), 8TB WD HDD (/mnt/storage_8tb_linux), 2TB Hitachi HDD (/mnt/hitachi_3AC60 protected cron/snapshots), 2x 2.5\" SATA SSDs in Top ODD Bay, DVD Drive in Middle ODD Bay (Audio CDs & data discs only; no video bloat), $35 HP 683870-001 3.5\" Carrier in Bottom ODD Bay.");
        db.set_owner_signal("storage_roadmap", "Stage 1: 2TB NVMe (Vectors/DB) + 8TB WD (Tom_Knowledge_Vault) + 2TB Hitachi (Untouched). Stage 2A: 2x 18TB Seagate Exos Mirror (18TB usable, powered via 4-pin Molex to 15-pin SATA adapter bypassing Pin 3 3.3V PWDIS). Stage 2B: Bring Exos #3+#4 online as 36TB Stripe -> copy from 18TB Mirror -> verify 100% SHA-256 accuracy -> attach Exos #1+#2 to mirror the 36TB Stripe (RAID 10). Stage 3 (at 66% / 24TB): Z420 Slot 4 Minisforum ENPBA/ESP4B -> Minisforum N5 Air AI NAS.");
        db.set_owner_signal("coding_preferences", "Compiled native C/C++/Rust/Go binaries only on HP Z420 (-O3 -s -march=sandybridge -mavx -msse4.2 -mno-avx2 -mno-fma -mno-f16c; switch to -march=ivybridge -mf16c after E5-2697 v2 install). Zero GPU VRAM for background daemons. Local-First RAG priority before web search.");
        db.set_owner_signal("road_schedule", "On the road 5 days/week; captures Gemini Mobile App chats & links via 'DevVault Road Inbox & Sources Watchlist' Google Doc synced with local SOURCES.md.");

        std::cout << "[devvault coding-sync] Indexed/updated " << indexed << " coding artifacts, transcripts & project files.\n";
        return 0;
    }

    // Parse SOURCES.md, pull Google Docs via gdocs CLI, and mirror watched URLs
    static int run_road_and_web_sync(DevVaultDb& db, const std::string& sources_md_path, bool mirror_web = true) {
        std::ifstream ifs(sources_md_path);
        if (!ifs) {
            std::cerr << "[road-sync] Could not open " << sources_md_path << "\n";
            return 1;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        db.upsert_item("DevVault Master Sources Watchlist (SOURCES.md)", "file://" + sources_md_path, "watchlist", "05_Computer_Programming_and_AI", content, static_cast<int64_t>(content.size()));

        // Extract all https:// URLs from SOURCES.md
        static const std::regex url_re(R"(https://[^\s\)\]>\"']+)");
        std::set<std::string> urls;
        auto begin = std::sregex_iterator(content.begin(), content.end(), url_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string u = it->str();
            while (!u.empty() && (u.back() == '.' || u.back() == ',' || u.back() == ';')) u.pop_back();
            urls.insert(u);
        }

        int docs_synced = 0;
        int web_synced = 0;
        for (const auto& u : urls) {
            // Check if it's a Google Doc URL: https://docs.google.com/document/d/<DOC_ID>/...
            size_t dpos = u.find("docs.google.com/document/d/");
            if (dpos != std::string::npos) {
                size_t id_start = dpos + std::strlen("docs.google.com/document/d/");
                size_t id_end = u.find_first_of("/?#", id_start);
                std::string doc_id = (id_end == std::string::npos) ? u.substr(id_start) : u.substr(id_start, id_end - id_start);
                if (doc_id.size() >= 15) {
                    std::string tmp_md = "/tmp/devvault_gdoc_" + doc_id + ".md";
                    std::string cmd = "/home/tom/.gemini/config/plugins/gdocs/skills/gdocs/scripts/gdocs export " + doc_id + " --format md -o " + tmp_md + " 2>/dev/null";
                    Scrubber::run_cmd(cmd, 4096);
                    if (fs::exists(tmp_md)) {
                        std::string gdoc_txt = DocumentClassifier::extract_document_text(tmp_md, 64000);
                        if (gdoc_txt.size() > 20) {
                            db.upsert_item("Google Doc (" + doc_id.substr(0, 10) + ")", u, "google_doc", "05_Computer_Programming_and_AI", gdoc_txt, static_cast<int64_t>(gdoc_txt.size()));
                            docs_synced++;
                        }
                    }
                }
                continue;
            }

            if (mirror_web && u.find("example") == std::string::npos) {
                std::string escaped;
                for (char c : u) {
                    if (c == '\'') escaped += "'\\''";
                    else escaped.push_back(c);
                }
                std::string cmd = "/usr/bin/curl -sL --max-time 15 -A 'Mozilla/5.0 (X11; Linux x86_64) DevVault/2.0' '" + escaped + "'";
                std::string raw_html = Scrubber::run_cmd(cmd, 262144);
                if (raw_html.size() > 100) {
                    std::string title = extract_html_title(raw_html);
                    if (title.empty()) title = u;
                    std::string md_body = DocumentClassifier::strip_tags(raw_html).substr(0, 24000);
                    std::string diff_summary;
                    db.upsert_web_snapshot(u, title, md_body, diff_summary);
                    std::cout << "  [web-mirror] " << u << " -> " << diff_summary << "\n";
                    web_synced++;
                }
            }
        }
        std::cout << "[devvault road-sync] Synced " << docs_synced << " Google Docs and mirrored " << web_synced << " watched web pages.\n";
        return 0;
    }

private:
    static std::string extract_html_title(const std::string& html) {
        std::string low = Scrubber::to_lower(html.substr(0, 16384));
        size_t b = low.find("<title");
        if (b == std::string::npos) return "";
        b = low.find('>', b);
        if (b == std::string::npos) return "";
        b++;
        size_t e = low.find("</title>", b);
        if (e == std::string::npos || e <= b) return "";
        return Scrubber::trim(html.substr(b, std::min<size_t>(e - b, 140)));
    }
};

} // namespace devvault
