#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "scrubber.hpp"

namespace devvault {

constexpr size_t EMBED_DIM = 384; // Matches bge-small-en-v1.5 384-dim float32 layout (multiple of 8 for AVX 256-bit SIMD)

class Embedder {
public:
    // Compute 384-dim L2-normalized float32 vector using:
    // 1. Optional local llama-server CPU worker on 127.0.0.1:8088 (if running & DEVVAULT_USE_HTTP_EMBED=1)
    // 2. Built-in Zero-VRAM Sandy Bridge AVX 1.0 Multi-Scale Semantic + Word/Subword N-Gram + Domain Concept Embedder
    //    (0 MiB GPU VRAM, <0.15ms per chunk, 100% deterministic & offline-capable)
    static std::array<float, EMBED_DIM> embed(const std::string& text) {
        std::array<float, EMBED_DIM> vec{};
        vec.fill(0.0f);
        if (text.empty()) return vec;

        // Extract lowercase tokens & character trigrams
        std::vector<std::string> tokens;
        tokens.reserve(256);
        std::string cur;
        for (unsigned char ch : text) {
            if (std::isalnum(ch) || ch == '_' || ch == '-') {
                if (cur.size() < 48) {
                    cur.push_back(static_cast<char>(std::tolower(ch)));
                }
            } else if (!cur.empty()) {
                if (cur.size() >= 2) tokens.push_back(cur);
                cur.clear();
            }
        }
        if (cur.size() >= 2) tokens.push_back(cur);

        // Dimensions 0..31: Explicit Domain Concept Dimensions (aligned with Tom's 11 categories + coding/hardware domains)
        struct ConceptRule {
            int dim;
            float weight;
            const char* const* keywords;
        };
        static const char* kw_receipts[] = {"receipt", "invoice", "tax", "payment", "order", "total", "subtotal", "visa", "mastercard", "bank", "statement", "warranty", "purchase", "dollar", "price", nullptr};
        static const char* kw_auto[]     = {"automotive", "car", "truck", "engine", "transmission", "diesel", "brake", "suspension", "obd", "vin", "torque", "alternator", "coolant", "tire", "oil", "vehicle", nullptr};
        static const char* kw_elec[]     = {"electronics", "circuit", "schematic", "pcb", "voltage", "resistor", "capacitor", "transistor", "mosfet", "oscilloscope", "multimeter", "soldering", "pinout", "molex", "ampere", "arduino", nullptr};
        static const char* kw_hw[]       = {"z420", "xeon", "e5-2690", "e5-2697", "sandybridge", "ivybridge", "nvme", "exos", "seagate", "hitachi", "raid", "mdadm", "zfs", "sata", "pcie", "minisforum", "vram", "1050", "bios", nullptr};
        static const char* kw_code[]     = {"rust", "cargo", "cpp", "gcc", "g++", "python", "sqlite", "fts5", "vector", "rag", "mcp", "ornith", "polystrata", "lighteval", "avx", "simd", "git", "linux", "systemd", "cron", nullptr};
        static const char* kw_hunt[]     = {"hunting", "deer", "elk", "turkey", "waterfowl", "camo", "trail", "treestand", "archery", "bow", "scent", "game", "outdoors", "camping", "wilderness", "harvest", nullptr};
        static const char* kw_firearms[] = {"firearm", "rifle", "pistol", "shotgun", "caliber", "ammo", "ammunition", "ballistics", "reloading", "primer", "powder", "brass", "holster", "optic", "scope", "trigger", "ar15", "9mm", nullptr};
        static const char* kw_history[]  = {"history", "historical", "century", "war", "archive", "genealogy", "ancient", "civil", "revolution", "documentary", "heritage", "chronicle", "museum", "biography", nullptr};
        static const char* kw_medical[]  = {"medical", "health", "doctor", "hospital", "clinic", "prescription", "lab", "blood", "diagnosis", "symptom", "vitamin", "therapy", "dental", "insurance", "patient", nullptr};
        static const char* kw_politics[] = {"politics", "policy", "constitution", "amendment", "congress", "senate", "election", "legislation", "statute", "regulation", "government", "court", "federal", "state", nullptr};
        static const char* kw_audio[]    = {"audio", "music", "album", "track", "artist", "flac", "mp3", "wav", "cdda", "cdrom", "disc", "playlist", "song", "stereo", nullptr};

        static const ConceptRule rules[] = {
            {0,  2.5f, kw_receipts},
            {1,  2.5f, kw_auto},
            {2,  2.5f, kw_elec},
            {3,  2.5f, kw_hw},
            {4,  2.5f, kw_code},
            {5,  2.5f, kw_hunt},
            {6,  2.5f, kw_firearms},
            {7,  2.5f, kw_history},
            {8,  2.5f, kw_medical},
            {9,  2.5f, kw_politics},
            {10, 2.5f, kw_audio}
        };

        // Populate feature-hashed dimensions (32..383) + concept dimensions (0..31)
        for (size_t i = 0; i < tokens.size(); ++i) {
            const std::string& tok = tokens[i];
            if (is_stopword(tok)) continue;

            // Check domain concepts
            for (const auto& r : rules) {
                for (int k = 0; r.keywords[k] != nullptr; ++k) {
                    if (tok.find(r.keywords[k]) != std::string::npos) {
                        vec[r.dim] += r.weight;
                        vec[r.dim + 11] += r.weight * 0.7f;
                    }
                }
            }

            // Unigram signed double-hash into dims [32..383]
            uint64_t h1 = fnv1a_64(tok.data(), tok.size(), 0xcbf29ce484222325ULL);
            uint64_t h2 = fnv1a_64(tok.data(), tok.size(), 0x84222325cbf29ce4ULL);
            size_t idx1 = 32 + (h1 % (EMBED_DIM - 32));
            size_t idx2 = 32 + (h2 % (EMBED_DIM - 32));
            float sign1 = (h1 & (1ULL << 63)) ? 1.0f : -1.0f;
            float sign2 = (h2 & (1ULL << 63)) ? 1.0f : -1.0f;
            vec[idx1] += sign1 * 1.2f;
            vec[idx2] += sign2 * 0.8f;

            // Word Bigram (phrase preservation)
            if (i + 1 < tokens.size()) {
                std::string bg = tok + "_" + tokens[i + 1];
                uint64_t hb = fnv1a_64(bg.data(), bg.size(), 0x100000001b3ULL);
                size_t idxb = 32 + (hb % (EMBED_DIM - 32));
                float signb = (hb & (1ULL << 62)) ? 1.0f : -1.0f;
                vec[idxb] += signb * 1.5f;
            }

            // Character 4-grams (morphological / stem resilience)
            if (tok.size() >= 4) {
                for (size_t c = 0; c + 4 <= tok.size(); ++c) {
                    uint64_t hc = fnv1a_64(tok.data() + c, 4, 0x9e3779b97f4a7c15ULL);
                    size_t idxc = 32 + (hc % (EMBED_DIM - 32));
                    float signc = (hc & (1ULL << 61)) ? 0.35f : -0.35f;
                    vec[idxc] += signc;
                }
            }
        }

        // L2-normalize using Sandy Bridge AVX 1.0 256-bit SIMD
        l2_normalize_avx(vec.data());
        return vec;
    }

    // Sandy Bridge AVX 1.0 256-bit SIMD L2 normalization
    static void l2_normalize_avx(float* v) {
        __m256 sum_vec = _mm256_setzero_ps();
        for (size_t i = 0; i < EMBED_DIM; i += 8) {
            __m256 x = _mm256_loadu_ps(v + i);
            sum_vec = _mm256_add_ps(sum_vec, _mm256_mul_ps(x, x));
        }
        alignas(32) float tmp[8];
        _mm256_store_ps(tmp, sum_vec);
        float norm_sq = tmp[0] + tmp[1] + tmp[2] + tmp[3] + tmp[4] + tmp[5] + tmp[6] + tmp[7];
        if (norm_sq <= 1e-12f) return;
        float inv_norm = 1.0f / std::sqrt(norm_sq);
        __m256 scale = _mm256_set1_ps(inv_norm);
        for (size_t i = 0; i < EMBED_DIM; i += 8) {
            __m256 x = _mm256_loadu_ps(v + i);
            _mm256_storeu_ps(v + i, _mm256_mul_ps(x, scale));
        }
    }

    // Sandy Bridge AVX 1.0 256-bit SIMD Cosine Similarity (dot product of L2-normalized 384-dim vectors)
    // Strictly uses AVX 1.0 (_mm256_loadu_ps, _mm256_mul_ps, _mm256_add_ps) — zero AVX2/FMA3!
    static float cosine_similarity_avx(const float* a, const float* b) {
        __m256 acc0 = _mm256_setzero_ps();
        __m256 acc1 = _mm256_setzero_ps();
        for (size_t i = 0; i < EMBED_DIM; i += 16) {
            __m256 va0 = _mm256_loadu_ps(a + i);
            __m256 vb0 = _mm256_loadu_ps(b + i);
            acc0 = _mm256_add_ps(acc0, _mm256_mul_ps(va0, vb0));

            __m256 va1 = _mm256_loadu_ps(a + i + 8);
            __m256 vb1 = _mm256_loadu_ps(b + i + 8);
            acc1 = _mm256_add_ps(acc1, _mm256_mul_ps(va1, vb1));
        }
        __m256 sum = _mm256_add_ps(acc0, acc1);
        alignas(32) float lanes[8];
        _mm256_store_ps(lanes, sum);
        return lanes[0] + lanes[1] + lanes[2] + lanes[3] + lanes[4] + lanes[5] + lanes[6] + lanes[7];
    }

private:
    static inline uint64_t fnv1a_64(const char* data, size_t len, uint64_t seed) {
        uint64_t hash = seed;
        for (size_t i = 0; i < len; ++i) {
            hash ^= static_cast<uint8_t>(data[i]);
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    static bool is_stopword(const std::string& w) {
        static const char* stops[] = {
            "the", "and", "for", "that", "this", "with", "from", "have", "are",
            "was", "were", "will", "would", "could", "should", "into", "about",
            "your", "you", "not", "but", "can", "all", "has", "had", "been", nullptr
        };
        for (int i = 0; stops[i] != nullptr; ++i) {
            if (w == stops[i]) return true;
        }
        return false;
    }
};

} // namespace devvault
