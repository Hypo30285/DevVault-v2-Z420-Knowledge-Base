# DevVault v2.0 — Native C++17 Local-First Vector + FTS5 Knowledge Base, `MiniCPM-V 4.6` Vision Classifier & Phased 36TB Exos `RAID 10` Vault for the HP Z420 Workstation

**Author / Owner:** Tom (`Hypo30285` / `supertruckertom@gmail.com`)  
**Engineering Partner:** Bailey (`Bburnsed1995@gmail.com`)  
**Platform:** HP Z420 Workstation (Ubuntu 26.04 LTS, Intel Xeon E5-2690 v1 Sandy Bridge $\rightarrow$ E5-2697 v2 Ivy Bridge, 64 GB ECC DDR3, 2 TB NVMe + 256 GiB Swapfile, Dual-GPU Slot 2 + Slot 5 Architecture)

---

## Repository Contents

| File / Directory | Description |
| :--- | :--- |
| [`IMPLEMENTATION_PLAN.md`](./IMPLEMENTATION_PLAN.md) | **Project Design Notes & Implementation Plan** — Dual-GPU PCIe Slot 2/Slot 5 architecture, `MiniCPM-V 4.6` single-patch (`448px`) $\rightarrow$ multi-patch (`1344px`) scaling, and Phased Storage Roadmap (`Stage 1` NVMe + 8TB WD $\rightarrow$ `Stage 2A` 18TB Exos Mirror $\rightarrow$ `Stage 2B` 4-Step Verified 36TB `RAID 10` $\rightarrow$ `Stage 3` Minisforum ENPBA/ESP4B + N5 Air AI NAS). |
| [`ENGINEERING_REPORT.md`](./ENGINEERING_REPORT.md) | **Complete Engineering Dossier, Walkthrough & Live `output.txt` Logs** — Full `./bin/devvault init-all` output, `devvault_search` hybrid BM25 + 384-dim AVX1 vector results, `MiniCPM-V 4.6` live PDF/image classification benchmarks (`1.37s–2.82s` warm on GTX 1050 Ti at `2,309 MiB` VRAM, `31°C`), and cross-system notes for Bailey's Ryzen 9 5900XT + RTX 2060 + Intel Arc Pro B70 32GB rig. |
| [`CHAT_PROMPTS_AND_RESPONSES.md`](./CHAT_PROMPTS_AND_RESPONSES.md) | **Dedicated Session Transcript** — Complete chronological user prompts and agent engineering responses from the design and implementation session. |
| [`Makefile`](./Makefile) | Native C++17 build system with `make sandybridge` (`E5-2690 v1`, `-march=sandybridge -mavx -msse4.2 -mno-avx2 -mno-fma -mno-f16c`) and `make ivybridge` (`E5-2697 v2`, `-march=ivybridge -mf16c`) targets. |
| [`AGENTS.md`](./AGENTS.md) | Local-First RAG query rule, Z420 CPU ISA compilation rules, Phased Storage protection rules, and the mandatory 6-Part End-of-Day Engineering Dossier reporting standard. |
| [`SOURCES.md`](./SOURCES.md) | Road-Warrior watchlist (`supertruckertom@gmail.com` Google Docs, 5-days/week mobile Gemini App share links, HP Spare `683870-001`, Molex-to-SATA `PWDIS` adapters, and Minisforum Stage-3 hardware URLs). |
| [`src/main.cpp`](./src/main.cpp) | CLI router (`init-all`, `search`, `store`, `test-vision`, `night-classify`, `storage-guard`, `verify-array`, `mcp`, `serve`). |
| [`src/sqlite_dl.hpp`](./src/sqlite_dl.hpp) | Zero-dependency runtime `dlopen`/`dlsym` loader for `libsqlite3.so.0` (compiles cleanly without `-dev` header packages). |
| [`src/sha256.hpp`](./src/sha256.hpp) | Standalone RFC 6234 `SHA-256` file and string cryptographic hasher for deduplication and `verify-array` checks. |
| [`src/scrubber.hpp`](./src/scrubber.hpp) | Regex secret/API key redactor (`AIza...`, `sk-...`, tokens) and lightweight JSON parser/escaper. |
| [`src/embed_avx.hpp`](./src/embed_avx.hpp) | 256-bit Sandy Bridge `AVX 1.0` SIMD (`_mm256_loadu_ps`, `_mm256_mul_ps`, `_mm256_add_ps`) 384-dim `float32` vectorizer and cosine similarity engine (`0 MiB` GPU VRAM). |
| [`src/vision_minicpm.hpp`](./src/vision_minicpm.hpp) | Dual-GPU aware **`MiniCPM-V 4.6`** document & image classifier (`448px` single-patch mode in `/dev/shm` RAM disk on GTX 1050 Ti $\rightarrow$ auto-scales to `1344px` on Slot 2 `16–24GB` Agentic GPU). |
| [`src/db.hpp`](./src/db.hpp) | SQLite WAL + FTS5 BM25 + 384-dim Vector BLOB Reciprocal Rank Fusion (RRF) engine, `file_move_ledger`, and `web_snapshots`. |
| [`src/subsystems.hpp`](./src/subsystems.hpp) | `StorageGuard` (`statvfs` capacity telemetry + `verify-array`), `DocumentClassifier` (11 seeded categories + emergent clustering + Video Guardrail), and `CodingAndRoadSync`. |
| [`src/mcp_server.hpp`](./src/mcp_server.hpp) | Native stdio JSON-RPC 2.0 MCP server (`devvault_search`, `devvault_store_note`, `devvault_storage_status`, `devvault_classify_status`, `devvault_classify_image`, `devvault_road_sync`) + POSIX HTTP gateway (`:8089`). |

---

## Quick Start & Build Instructions

```bash
# 1. Compile for current Intel Xeon E5-2690 v1 (8C/16T Sandy Bridge, AVX 1.0):
make sandybridge

# (Or after installing Intel Xeon E5-2697 v2 12C/24T Ivy Bridge):
# make ivybridge

# 2. Run the full 5-stage initial ingestion & verification pass:
./bin/devvault init-all

# 3. Search the Hybrid FTS5 BM25 + 384-Dim AVX1 Vector RAG Database:
./bin/devvault search "Dual GPU Slot 2 Slot 5 P6000 Intel Arc B70 MiniCPM-V 4.6"

# 4. Classify a PDF datasheet, schematic, receipt, or photo with MiniCPM-V 4.6:
./bin/devvault test-vision /path/to/document.pdf

# 5. Check live Z420 drive & Dual-GPU telemetry (with 66% Stage 2A/2B/3 capacity alerts):
./bin/devvault storage-guard
```
