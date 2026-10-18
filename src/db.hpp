#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "embed_avx.hpp"
#include "scrubber.hpp"
#include "sha256.hpp"
#include "sqlite_dl.hpp"

namespace devvault {

struct SearchResult {
    int64_t chunk_id = 0;
    int64_t item_id = 0;
    std::string title;
    std::string source_uri;
    std::string item_type;
    std::string category;
    std::string snippet;
    double bm25_score = 0.0;
    double cosine_score = 0.0;
    double rrf_score = 0.0;
};

struct CategoryStat {
    std::string category;
    int64_t file_count = 0;
    int64_t total_bytes = 0;
};

class DevVaultDb {
public:
    explicit DevVaultDb(const std::string& db_path) : db_path_(db_path), api_(SqliteApi::instance()) {
        std::filesystem::path p(db_path_);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        if (api_.open(db_path_.c_str(), &db_) != SQLITE_OK) {
            std::string err = db_ ? api_.errmsg(db_) : "unknown error";
            throw std::runtime_error("Failed to open SQLite DB at " + db_path_ + ": " + err);
        }
        api_.busy_timeout(db_, 10000);
        init_schema();
    }

    ~DevVaultDb() {
        if (db_) {
            api_.close(db_);
            db_ = nullptr;
        }
    }

    const std::string& db_path() const { return db_path_; }

    void exec_sql(const std::string& sql) {
        char* err_msg = nullptr;
        int rc = api_.exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            std::string err = err_msg ? err_msg : "SQL error";
            if (err_msg) api_.free_mem(err_msg);
            throw std::runtime_error("SQLite exec failed: " + err + " | SQL: " + sql);
        }
    }

    void init_schema() {
        exec_sql("PRAGMA journal_mode=WAL;");
        exec_sql("PRAGMA synchronous=NORMAL;");
        exec_sql("PRAGMA temp_store=MEMORY;");
        exec_sql("PRAGMA cache_size=-65536;");       // 64 MiB page cache in ECC RAM
        exec_sql("PRAGMA mmap_size=2147483648;");   // 2 GiB memory-mapped I/O on NVMe

        exec_sql(R"SQL(
            CREATE TABLE IF NOT EXISTS knowledge_items (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                source_uri TEXT NOT NULL UNIQUE,
                item_type TEXT NOT NULL,
                category TEXT NOT NULL,
                sha256 TEXT NOT NULL,
                byte_size INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_items_category ON knowledge_items(category);
            CREATE INDEX IF NOT EXISTS idx_items_sha256 ON knowledge_items(sha256);
            CREATE INDEX IF NOT EXISTS idx_items_type ON knowledge_items(item_type);

            CREATE TABLE IF NOT EXISTS chunks (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                item_id INTEGER NOT NULL,
                chunk_index INTEGER NOT NULL,
                content TEXT NOT NULL,
                FOREIGN KEY(item_id) REFERENCES knowledge_items(id) ON DELETE CASCADE
            );
            CREATE INDEX IF NOT EXISTS idx_chunks_item ON chunks(item_id);

            CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(
                title,
                category,
                content
            );

            CREATE TABLE IF NOT EXISTS chunk_vectors (
                chunk_id INTEGER PRIMARY KEY,
                item_id INTEGER NOT NULL,
                embedding BLOB NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_vectors_item ON chunk_vectors(item_id);

            CREATE TABLE IF NOT EXISTS file_move_ledger (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                source_path TEXT NOT NULL UNIQUE,
                vault_path TEXT NOT NULL,
                category TEXT NOT NULL,
                confidence REAL NOT NULL,
                mode TEXT NOT NULL,
                sha256 TEXT NOT NULL,
                byte_size INTEGER NOT NULL,
                moved_at TEXT NOT NULL,
                rolled_back INTEGER NOT NULL DEFAULT 0
            );
            CREATE INDEX IF NOT EXISTS idx_ledger_sha256 ON file_move_ledger(sha256);

            CREATE TABLE IF NOT EXISTS web_snapshots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT NOT NULL UNIQUE,
                title TEXT NOT NULL,
                markdown_body TEXT NOT NULL,
                content_sha256 TEXT NOT NULL,
                fetched_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS web_diff_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT NOT NULL,
                old_sha256 TEXT NOT NULL,
                new_sha256 TEXT NOT NULL,
                diff_summary TEXT NOT NULL,
                detected_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS owner_profile_signals (
                signal_key TEXT PRIMARY KEY,
                signal_value TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
        )SQL");
    }

    // Check if an item with the same source_uri and sha256 is already indexed
    bool is_up_to_date(const std::string& source_uri, const std::string& sha256) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT sha256 FROM knowledge_items WHERE source_uri = ?1 LIMIT 1;";
        if (api_.prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        api_.bind_text(stmt, 1, source_uri.c_str(), -1, SQLITE_TRANSIENT_PTR);
        bool match = false;
        if (api_.step(stmt) == SQLITE_ROW) {
            const unsigned char* s = api_.column_text(stmt, 0);
            if (s && sha256 == reinterpret_cast<const char*>(s)) {
                match = true;
            }
        }
        api_.finalize(stmt);
        return match;
    }

    // Check if any file in the knowledge base or ledger already has this SHA-256
    std::string find_existing_by_sha256(const std::string& sha256) {
        if (sha256.empty()) return "";
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT source_uri FROM knowledge_items WHERE sha256 = ?1 LIMIT 1;";
        if (api_.prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return "";
        api_.bind_text(stmt, 1, sha256.c_str(), -1, SQLITE_TRANSIENT_PTR);
        std::string found;
        if (api_.step(stmt) == SQLITE_ROW) {
            const unsigned char* s = api_.column_text(stmt, 0);
            if (s) found = reinterpret_cast<const char*>(s);
        }
        api_.finalize(stmt);
        return found;
    }

    // Ingest or update a document/note/transcript, chunk it, populate FTS5 & AVX1 384-dim vectors
    int64_t upsert_item(
        const std::string& title,
        const std::string& source_uri,
        const std::string& item_type,
        const std::string& category,
        const std::string& raw_content,
        int64_t byte_size = -1,
        const std::string& precomputed_sha256 = ""
    ) {
        std::string scrubbed = Scrubber::scrub_secrets(raw_content);
        std::string sha = precomputed_sha256.empty() ? Sha256::hash_string(scrubbed) : precomputed_sha256;
        if (byte_size < 0) byte_size = static_cast<int64_t>(scrubbed.size());

        if (is_up_to_date(source_uri, sha)) {
            return 0; // Already indexed with identical SHA-256
        }

        std::string now = Scrubber::iso8601_now();
        exec_sql("BEGIN IMMEDIATE TRANSACTION;");
        try {
            // Delete existing chunks & vectors if updating
            int64_t existing_id = 0;
            {
                sqlite3_stmt* st = nullptr;
                if (api_.prepare_v2(db_, "SELECT id FROM knowledge_items WHERE source_uri = ?1;", -1, &st, nullptr) == SQLITE_OK) {
                    api_.bind_text(st, 1, source_uri.c_str(), -1, SQLITE_TRANSIENT_PTR);
                    if (api_.step(st) == SQLITE_ROW) {
                        existing_id = api_.column_int64(st, 0);
                    }
                    api_.finalize(st);
                }
            }

            if (existing_id > 0) {
                sqlite3_stmt* del_fts = nullptr;
                if (api_.prepare_v2(db_, "DELETE FROM chunks_fts WHERE rowid IN (SELECT id FROM chunks WHERE item_id = ?1);", -1, &del_fts, nullptr) == SQLITE_OK) {
                    api_.bind_int64(del_fts, 1, existing_id);
                    api_.step(del_fts);
                    api_.finalize(del_fts);
                }
                sqlite3_stmt* del_vec = nullptr;
                if (api_.prepare_v2(db_, "DELETE FROM chunk_vectors WHERE item_id = ?1;", -1, &del_vec, nullptr) == SQLITE_OK) {
                    api_.bind_int64(del_vec, 1, existing_id);
                    api_.step(del_vec);
                    api_.finalize(del_vec);
                }
                sqlite3_stmt* del_chk = nullptr;
                if (api_.prepare_v2(db_, "DELETE FROM chunks WHERE item_id = ?1;", -1, &del_chk, nullptr) == SQLITE_OK) {
                    api_.bind_int64(del_chk, 1, existing_id);
                    api_.step(del_chk);
                    api_.finalize(del_chk);
                }
                sqlite3_stmt* upd = nullptr;
                const char* upd_sql = "UPDATE knowledge_items SET title=?1, item_type=?2, category=?3, sha256=?4, byte_size=?5, updated_at=?6 WHERE id=?7;";
                api_.prepare_v2(db_, upd_sql, -1, &upd, nullptr);
                api_.bind_text(upd, 1, title.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(upd, 2, item_type.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(upd, 3, category.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(upd, 4, sha.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_int64(upd, 5, byte_size);
                api_.bind_text(upd, 6, now.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_int64(upd, 7, existing_id);
                api_.step(upd);
                api_.finalize(upd);
            } else {
                sqlite3_stmt* ins = nullptr;
                const char* ins_sql = "INSERT INTO knowledge_items (title, source_uri, item_type, category, sha256, byte_size, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";
                api_.prepare_v2(db_, ins_sql, -1, &ins, nullptr);
                api_.bind_text(ins, 1, title.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(ins, 2, source_uri.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(ins, 3, item_type.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(ins, 4, category.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(ins, 5, sha.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_int64(ins, 6, byte_size);
                api_.bind_text(ins, 7, now.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(ins, 8, now.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.step(ins);
                api_.finalize(ins);
                existing_id = api_.last_insert_rowid(db_);
            }

            // Split content into semantic chunks (~1,200 chars with 150-char overlap, max 24 chunks per file)
            std::vector<std::string> chunk_list = split_into_chunks(title + "\n" + scrubbed, 1200, 150, 24);

            sqlite3_stmt* st_chk = nullptr;
            sqlite3_stmt* st_fts = nullptr;
            sqlite3_stmt* st_vec = nullptr;
            api_.prepare_v2(db_, "INSERT INTO chunks (item_id, chunk_index, content) VALUES (?1, ?2, ?3);", -1, &st_chk, nullptr);
            api_.prepare_v2(db_, "INSERT INTO chunks_fts (rowid, title, category, content) VALUES (?1, ?2, ?3, ?4);", -1, &st_fts, nullptr);
            api_.prepare_v2(db_, "INSERT INTO chunk_vectors (chunk_id, item_id, embedding) VALUES (?1, ?2, ?3);", -1, &st_vec, nullptr);

            for (size_t idx = 0; idx < chunk_list.size(); ++idx) {
                const std::string& ctext = chunk_list[idx];
                api_.reset(st_chk);
                api_.bind_int64(st_chk, 1, existing_id);
                api_.bind_int64(st_chk, 2, static_cast<int64_t>(idx));
                api_.bind_text(st_chk, 3, ctext.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.step(st_chk);
                int64_t chunk_id = api_.last_insert_rowid(db_);

                api_.reset(st_fts);
                api_.bind_int64(st_fts, 1, chunk_id);
                api_.bind_text(st_fts, 2, title.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(st_fts, 3, category.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(st_fts, 4, ctext.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.step(st_fts);

                auto vec = Embedder::embed(title + " " + category + " " + ctext);
                api_.reset(st_vec);
                api_.bind_int64(st_vec, 1, chunk_id);
                api_.bind_int64(st_vec, 2, existing_id);
                api_.bind_blob(st_vec, 3, vec.data(), static_cast<int>(EMBED_DIM * sizeof(float)), SQLITE_TRANSIENT_PTR);
                api_.step(st_vec);
            }

            api_.finalize(st_chk);
            api_.finalize(st_fts);
            api_.finalize(st_vec);
            exec_sql("COMMIT;");
            return existing_id;
        } catch (...) {
            exec_sql("ROLLBACK;");
            throw;
        }
    }

    // Hybrid Search: Reciprocal Rank Fusion (RRF) combining SQLite FTS5 (BM25) + Sandy Bridge AVX1 384-dim Cosine Vector Search
    std::vector<SearchResult> hybrid_search(const std::string& query, int top_k = 8, const std::string& category_filter = "") {
        std::unordered_map<int64_t, SearchResult> candidates;
        std::unordered_map<int64_t, int> fts_rank;
        std::unordered_map<int64_t, int> vec_rank;

        // 1. FTS5 BM25 Keyword Search
        std::string fts_q = build_fts_query(query);
        if (!fts_q.empty()) {
            sqlite3_stmt* st = nullptr;
            const char* sql = R"SQL(
                SELECT c.id, c.item_id, k.title, k.source_uri, k.item_type, k.category, c.content, bm25(chunks_fts) AS score
                FROM chunks_fts
                JOIN chunks c ON chunks_fts.rowid = c.id
                JOIN knowledge_items k ON c.item_id = k.id
                WHERE chunks_fts MATCH ?1
                ORDER BY score ASC
                LIMIT 50;
            )SQL";
            if (api_.prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
                api_.bind_text(st, 1, fts_q.c_str(), -1, SQLITE_TRANSIENT_PTR);
                int r = 1;
                while (api_.step(st) == SQLITE_ROW) {
                    int64_t cid = api_.column_int64(st, 0);
                    std::string cat = col_str(st, 5);
                    if (!category_filter.empty() && cat != category_filter) continue;
                    SearchResult sr;
                    sr.chunk_id = cid;
                    sr.item_id = api_.column_int64(st, 1);
                    sr.title = col_str(st, 2);
                    sr.source_uri = col_str(st, 3);
                    sr.item_type = col_str(st, 4);
                    sr.category = cat;
                    sr.snippet = make_snippet(col_str(st, 6), 420);
                    sr.bm25_score = -api_.column_double(st, 7);
                    candidates[cid] = sr;
                    fts_rank[cid] = r++;
                }
                api_.finalize(st);
            }
        }

        // 2. Sandy Bridge AVX1 384-Dim Cosine Vector Scan
        auto qvec = Embedder::embed(query);
        struct VecHit {
            int64_t chunk_id;
            float sim;
        };
        std::vector<VecHit> vhits;
        vhits.reserve(2048);

        {
            sqlite3_stmt* st = nullptr;
            const char* sql = "SELECT chunk_id, embedding FROM chunk_vectors;";
            if (api_.prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
                while (api_.step(st) == SQLITE_ROW) {
                    int64_t cid = api_.column_int64(st, 0);
                    int bytes = api_.column_bytes(st, 1);
                    const void* blob = api_.column_blob(st, 1);
                    if (blob && bytes == static_cast<int>(EMBED_DIM * sizeof(float))) {
                        float sim = Embedder::cosine_similarity_avx(qvec.data(), reinterpret_cast<const float*>(blob));
                        if (sim > 0.05f) {
                            vhits.push_back({cid, sim});
                        }
                    }
                }
                api_.finalize(st);
            }
        }

        std::sort(vhits.begin(), vhits.end(), [](const VecHit& a, const VecHit& b) {
            return a.sim > b.sim;
        });
        if (vhits.size() > 50) vhits.resize(50);

        for (size_t i = 0; i < vhits.size(); ++i) {
            int64_t cid = vhits[i].chunk_id;
            vec_rank[cid] = static_cast<int>(i + 1);
            if (candidates.find(cid) == candidates.end()) {
                sqlite3_stmt* st = nullptr;
                const char* sql = R"SQL(
                    SELECT c.id, c.item_id, k.title, k.source_uri, k.item_type, k.category, c.content
                    FROM chunks c
                    JOIN knowledge_items k ON c.item_id = k.id
                    WHERE c.id = ?1;
                )SQL";
                if (api_.prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
                    api_.bind_int64(st, 1, cid);
                    if (api_.step(st) == SQLITE_ROW) {
                        std::string cat = col_str(st, 5);
                        if (category_filter.empty() || cat == category_filter) {
                            SearchResult sr;
                            sr.chunk_id = cid;
                            sr.item_id = api_.column_int64(st, 1);
                            sr.title = col_str(st, 2);
                            sr.source_uri = col_str(st, 3);
                            sr.item_type = col_str(st, 4);
                            sr.category = cat;
                            sr.snippet = make_snippet(col_str(st, 6), 420);
                            sr.cosine_score = vhits[i].sim;
                            candidates[cid] = sr;
                        }
                    }
                    api_.finalize(st);
                }
            } else {
                candidates[cid].cosine_score = vhits[i].sim;
            }
        }

        // 3. Reciprocal Rank Fusion (RRF, k = 60)
        std::vector<SearchResult> results;
        results.reserve(candidates.size());
        for (auto& kv : candidates) {
            int64_t cid = kv.first;
            SearchResult& sr = kv.second;
            double rrf = 0.0;
            if (fts_rank.count(cid)) {
                rrf += 1.0 / (60.0 + fts_rank[cid]);
            }
            if (vec_rank.count(cid)) {
                rrf += 1.0 / (60.0 + vec_rank[cid]) + (0.05 * sr.cosine_score);
            }
            sr.rrf_score = rrf;
            results.push_back(sr);
        }

        std::sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
            return a.rrf_score > b.rrf_score;
        });

        // Deduplicate by item_id so top results span distinct documents unless few exist
        std::vector<SearchResult> deduped;
        std::unordered_map<int64_t, int> item_counts;
        for (const auto& r : results) {
            if (item_counts[r.item_id] < 2) {
                deduped.push_back(r);
                item_counts[r.item_id]++;
                if (static_cast<int>(deduped.size()) >= top_k) break;
            }
        }
        return deduped;
    }

    void record_file_ledger(
        const std::string& source_path,
        const std::string& vault_path,
        const std::string& category,
        double confidence,
        const std::string& mode,
        const std::string& sha256,
        int64_t byte_size
    ) {
        sqlite3_stmt* st = nullptr;
        const char* sql = R"SQL(
            INSERT INTO file_move_ledger (source_path, vault_path, category, confidence, mode, sha256, byte_size, moved_at, rolled_back)
            VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 0)
            ON CONFLICT(source_path) DO UPDATE SET
                vault_path=excluded.vault_path,
                category=excluded.category,
                confidence=excluded.confidence,
                mode=excluded.mode,
                sha256=excluded.sha256,
                byte_size=excluded.byte_size,
                moved_at=excluded.moved_at,
                rolled_back=0;
        )SQL";
        if (api_.prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
            std::string now = Scrubber::iso8601_now();
            api_.bind_text(st, 1, source_path.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st, 2, vault_path.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st, 3, category.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_double(st, 4, confidence);
            api_.bind_text(st, 5, mode.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st, 6, sha256.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_int64(st, 7, byte_size);
            api_.bind_text(st, 8, now.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.step(st);
            api_.finalize(st);
        }
    }

    void set_owner_signal(const std::string& key, const std::string& value) {
        sqlite3_stmt* st = nullptr;
        const char* sql = R"SQL(
            INSERT INTO owner_profile_signals (signal_key, signal_value, updated_at)
            VALUES (?1, ?2, ?3)
            ON CONFLICT(signal_key) DO UPDATE SET
                signal_value=excluded.signal_value,
                updated_at=excluded.updated_at;
        )SQL";
        if (api_.prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
            std::string now = Scrubber::iso8601_now();
            api_.bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st, 2, value.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st, 3, now.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.step(st);
            api_.finalize(st);
        }
    }

    std::map<std::string, std::string> get_owner_signals() {
        std::map<std::string, std::string> out;
        sqlite3_stmt* st = nullptr;
        if (api_.prepare_v2(db_, "SELECT signal_key, signal_value FROM owner_profile_signals ORDER BY signal_key;", -1, &st, nullptr) == SQLITE_OK) {
            while (api_.step(st) == SQLITE_ROW) {
                out[col_str(st, 0)] = col_str(st, 1);
            }
            api_.finalize(st);
        }
        return out;
    }

    // Returns true if the URL is new or changed (and logs a diff if changed)
    bool upsert_web_snapshot(const std::string& url, const std::string& title, const std::string& markdown_body, std::string& out_diff_summary) {
        std::string new_sha = Sha256::hash_string(markdown_body);
        std::string old_sha;
        std::string old_body;
        {
            sqlite3_stmt* st = nullptr;
            if (api_.prepare_v2(db_, "SELECT content_sha256, markdown_body FROM web_snapshots WHERE url = ?1;", -1, &st, nullptr) == SQLITE_OK) {
                api_.bind_text(st, 1, url.c_str(), -1, SQLITE_TRANSIENT_PTR);
                if (api_.step(st) == SQLITE_ROW) {
                    old_sha = col_str(st, 0);
                    old_body = col_str(st, 1);
                }
                api_.finalize(st);
            }
        }

        if (!old_sha.empty() && old_sha == new_sha) {
            out_diff_summary = "UNCHANGED";
            return false;
        }

        std::string now = Scrubber::iso8601_now();
        if (!old_sha.empty() && old_sha != new_sha) {
            int64_t delta_bytes = static_cast<int64_t>(markdown_body.size()) - static_cast<int64_t>(old_body.size());
            std::ostringstream ds;
            ds << "Content updated (old SHA-256: " << old_sha.substr(0, 12)
               << " -> new SHA-256: " << new_sha.substr(0, 12)
               << ", delta bytes: " << (delta_bytes >= 0 ? "+" : "") << delta_bytes << ")";
            out_diff_summary = ds.str();

            sqlite3_stmt* st_diff = nullptr;
            const char* dsql = "INSERT INTO web_diff_log (url, old_sha256, new_sha256, diff_summary, detected_at) VALUES (?1, ?2, ?3, ?4, ?5);";
            if (api_.prepare_v2(db_, dsql, -1, &st_diff, nullptr) == SQLITE_OK) {
                api_.bind_text(st_diff, 1, url.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(st_diff, 2, old_sha.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(st_diff, 3, new_sha.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(st_diff, 4, out_diff_summary.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.bind_text(st_diff, 5, now.c_str(), -1, SQLITE_TRANSIENT_PTR);
                api_.step(st_diff);
                api_.finalize(st_diff);
            }
        } else {
            out_diff_summary = "INITIAL_MIRROR (SHA-256: " + new_sha.substr(0, 12) + ")";
        }

        sqlite3_stmt* st_up = nullptr;
        const char* usql = R"SQL(
            INSERT INTO web_snapshots (url, title, markdown_body, content_sha256, fetched_at)
            VALUES (?1, ?2, ?3, ?4, ?5)
            ON CONFLICT(url) DO UPDATE SET
                title=excluded.title,
                markdown_body=excluded.markdown_body,
                content_sha256=excluded.content_sha256,
                fetched_at=excluded.fetched_at;
        )SQL";
        if (api_.prepare_v2(db_, usql, -1, &st_up, nullptr) == SQLITE_OK) {
            api_.bind_text(st_up, 1, url.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st_up, 2, title.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st_up, 3, markdown_body.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st_up, 4, new_sha.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.bind_text(st_up, 5, now.c_str(), -1, SQLITE_TRANSIENT_PTR);
            api_.step(st_up);
            api_.finalize(st_up);
        }

        upsert_item(title, url, "web_mirror", "05_Computer_Programming_and_AI", markdown_body, static_cast<int64_t>(markdown_body.size()), new_sha);
        return true;
    }

    int64_t count_table(const std::string& table_name) {
        sqlite3_stmt* st = nullptr;
        std::string sql = "SELECT COUNT(*) FROM " + table_name + ";";
        int64_t cnt = 0;
        if (api_.prepare_v2(db_, sql.c_str(), -1, &st, nullptr) == SQLITE_OK) {
            if (api_.step(st) == SQLITE_ROW) {
                cnt = api_.column_int64(st, 0);
            }
            api_.finalize(st);
        }
        return cnt;
    }

    std::vector<CategoryStat> get_category_stats() {
        std::vector<CategoryStat> out;
        sqlite3_stmt* st = nullptr;
        const char* sql = "SELECT category, COUNT(*), COALESCE(SUM(byte_size), 0) FROM knowledge_items GROUP BY category ORDER BY COUNT(*) DESC;";
        if (api_.prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
            while (api_.step(st) == SQLITE_ROW) {
                CategoryStat cs;
                cs.category = col_str(st, 0);
                cs.file_count = api_.column_int64(st, 1);
                cs.total_bytes = api_.column_int64(st, 2);
                out.push_back(cs);
            }
            api_.finalize(st);
        }
        return out;
    }

private:
    std::string db_path_;
    const SqliteApi& api_;
    sqlite3* db_ = nullptr;

    std::string col_str(sqlite3_stmt* st, int col) {
        const unsigned char* s = api_.column_text(st, col);
        return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
    }

    static std::string make_snippet(const std::string& s, size_t max_len) {
        std::string clean;
        clean.reserve(std::min(s.size(), max_len + 32));
        bool last_space = false;
        for (char c : s) {
            if (c == '\r' || c == '\t') c = ' ';
            if (c == ' ') {
                if (!last_space) clean.push_back(' ');
                last_space = true;
            } else {
                clean.push_back(c);
                last_space = false;
            }
            if (clean.size() >= max_len) break;
        }
        if (s.size() > max_len) clean += "...";
        return clean;
    }

    static std::vector<std::string> split_into_chunks(const std::string& text, size_t chunk_chars, size_t overlap, size_t max_chunks) {
        std::vector<std::string> out;
        if (text.empty()) return out;
        if (text.size() <= chunk_chars) {
            out.push_back(text);
            return out;
        }
        size_t pos = 0;
        size_t step = (chunk_chars > overlap) ? (chunk_chars - overlap) : chunk_chars;
        while (pos < text.size() && out.size() < max_chunks) {
            size_t len = std::min(chunk_chars, text.size() - pos);
            out.push_back(text.substr(pos, len));
            if (pos + len >= text.size()) break;
            pos += step;
        }
        return out;
    }

    static std::string build_fts_query(const std::string& raw) {
        std::vector<std::string> terms;
        std::string cur;
        for (unsigned char c : raw) {
            if (std::isalnum(c) || c == '_') {
                cur.push_back(static_cast<char>(std::tolower(c)));
            } else if (!cur.empty()) {
                if (cur.size() >= 2) terms.push_back("\"" + cur + "\"");
                cur.clear();
            }
        }
        if (cur.size() >= 2) terms.push_back("\"" + cur + "\"");
        if (terms.empty()) return "";
        std::ostringstream oss;
        for (size_t i = 0; i < terms.size(); ++i) {
            if (i > 0) oss << " OR ";
            oss << terms[i];
        }
        return oss.str();
    }
};

} // namespace devvault
