# DevVault v2.0 — Complete Engineering Report, Walkthrough & Live `output.txt` Telemetry

- **Author / Owner:** Tom (`Hypo30285` / `supertruckertom@gmail.com`)
- **Engineering Partner:** Bailey (`Bburnsed1995@gmail.com`)
- **Workstation:** HP Z420 Workstation (`tom-HP-Z420-Workstation`)
- **Date:** October 18, 2026

---

## 1. Executive Project Summary & Cross-System Hardware Notes

We designed, implemented, compiled, and verified **DevVault v2.0** in `/mnt/nvme_storage/ai_projects/Local knowledge base.` (symlinked to `/mnt/nvme_storage/devvault`), a native **C++17** local-first Vector + FTS5 RAG knowledge base, MCP server, road-warrior mobile Gemini sync engine, and overnight personal document & vision classifier.

### Key Milestones Verified Live on the HP Z420
1. **Native C++17 Sandy Bridge `AVX 1.0` Binary (`bin/devvault`):**
   - Compiled with `-std=c++17 -O3 -s -march=sandybridge -mavx -msse4.2 -mno-avx2 -mno-fma -mno-f16c -pthread -ldl` (`0` errors; `make ivybridge` ready for the 12-core Xeon E5-2697 v2 upgrade).
   - *(Cross-System Note for Bailey's AMD Ryzen 9 5900XT 16C/32T Zen 3 Rig: compiling `src/main.cpp` with `-march=znver3 -O3` unlocks native `AVX2` + `FMA3` SIMD execution).*
2. **Active Vector + FTS5 Database on 2TB NVMe (`/mnt/nvme_storage/devvault/devvault.db`):**
   - `104.59 MiB` SQLite WAL database stored on the PCIe Slot 4 2TB HP EX950 NVMe alongside the active **`256.00 GiB` `/mnt/nvme_storage/swapfile`**.
   - Indexed **`3,699` items**, **`18,990` FTS5 BM25 chunks**, **`18,990` 384-dim `AVX 1.0` `float32` vectors**, **`2,000` categorized personal Vault files** in `/mnt/storage_8tb_linux/Tom_Knowledge_Vault/`, **`1` Google Doc**, and **`5` mirrored web pages** at **`0 MiB` GPU VRAM**.
3. **Proven `MiniCPM-V 4.6` Document & PDF Classification on GTX 1050 Ti (4GB):**
   - Uses single-patch `448px` visual scaling in `/dev/shm` RAM disk (`pdftoppm -jpeg -scale-to 448`, `num_ctx: 768`, `num_predict: 96`, `num_thread: 8`, `keep_alive: -1`).
   - Classified warm HP Z Workstation PDF datasheets (`c04798669.pdf` and `c04200260.pdf`) into `04_Computer_Hardware_and_Systems` in **`1,366.3 ms` (`1.37 s`)** and **`2,819.8 ms` (`2.82 s`)** at only **`2,309 / 4,096 MiB` VRAM (`31°C`)**.
4. **Dual-GPU Final Hardware Topology (PCIe Slot 2 + PCIe Slot 5):**
   - **PCIe Slot 2 (`16–24 GB` GPU):** Video output (Xorg) + small background agentic tasks (`MiniCPM-V 4.6` document/photo classification, OCR, and background vault indexing).
   - **PCIe Slot 5 (`24–32 GB+` GPU):** Dedicated 100% to **`Ornith 1.5 35B-A3B`** and larger `35B+` models (**NVIDIA Quadro P6000 24GB** `[Compute 6.1 sm_61 drop-in]` or **Intel Arc B70 32GB** `[Xe2 XMX — matching the Arc Pro B70 32GB in Bailey's workstation]`).

---

## 2. Live Execution & Telemetry Logs (`output.txt`)

### 2.1 Full `./bin/devvault init-all` Execution Output

```text
=== [1/5] Running devvault coding-sync (Antigravity Transcripts & Z420 Projects) ===
[devvault coding-sync] Indexed/updated 1689 coding artifacts, transcripts & project files.

=== [2/5] Running devvault road-sync & web-mirror (SOURCES.md + Google Docs + Web Cache) ===
  [web-mirror] https://g.co/gemini/share/...` -> INITIAL_MIRROR (SHA-256: e3a6226fe2ef)
  [web-mirror] https://gemini.google.com/share/ -> INITIAL_MIRROR (SHA-256: f6b40574ac4d)
  [web-mirror] https://gemini.google.com/share/...` -> INITIAL_MIRROR (SHA-256: e3a6226fe2ef)
  [web-mirror] https://huggingface.co/docs/lighteval/en/index -> INITIAL_MIRROR (SHA-256: e9b4e51ca117)
  [web-mirror] https://store.minisforum.com/products/minisforum-esp4b-pcie-to-4-nvme-m-2-expansion-card -> INITIAL_MIRROR (SHA-256: 1e33c6df413d)
[devvault road-sync] Synced 1 Google Docs and mirrored 5 watched web pages.

=== [3/5] Running devvault night-classify (Personal Document Vault on 8TB WD; Hitachi Protected) ===
[devvault night-classify] Starting scan (mode=symlink-vault, max_files=2000, dry_run=false)
  Protected paths : /mnt/hitachi_3AC60/staging_cache, /mnt/hitachi_3AC60/system_snapshots
  Target Vault    : /mnt/storage_8tb_linux/Tom_Knowledge_Vault
[devvault night-classify] Completed pass: 2000 files classified (2000 newly embedded, 0 unchanged/deduplicated).
  • 01_Receipts_and_Financial            : 273 files
  • 02_Automotive_and_Cars               : 152 files
  • 03_Electronics_and_Circuits          : 3 files
  • 04_Computer_Hardware_and_Systems     : 61 files
  • 05_Computer_Programming_and_AI       : 72 files
  • 06_Hunting_and_Outdoors              : 7 files
  • 07_Firearms_and_Ballistics           : 25 files
  • 08_History_and_Archives              : 38 files
  • 09_Medical_and_Health                : 85 files
  • 10_Politics_and_Policy               : 68 files
  • 11_Audio_CDs_and_Music_Library       : 833 files
  • 12_Emergent_and_General_Docs         : 212 files
  • 12_Emergent_eula                     : 138 files
  • 12_Emergent_string                   : 33 files

=== [4/5] Running devvault storage-guard (Live Z420 Drive & Vector DB Telemetry) ===
================================================================================
  DEVVAULT v2.0 STORAGE-GUARD & Z420 CAPACITY TELEMETRY (2026-10-18T11:19:14-0400)
================================================================================
  Active Vector/FTS5 DB : /mnt/nvme_storage/devvault/devvault.db (104.59 MiB)
  NVMe Swapfile Status  : /mnt/nvme_storage/swapfile (256.00 GiB active)
  Indexed RAG Corpus    : 3695 items | 18987 FTS5 chunks | 18987 AVX1 384-dim vectors
  Vault Ledger & Web    : 2000 classified files | 5 mirrored web pages
--------------------------------------------------------------------------------
  DRIVE / TIER                                  USED / TOTAL GiB    USE%   STATUS
--------------------------------------------------------------------------------
  2TB NVMe (Vectors, DB & 256GB Swap)           550.1 / 1875.7 GiB   29.3%  [OK - HEALTHY]
  8TB WD HDD (Stage 1 Knowledge Vault)          725.6 / 5360.3 GiB   13.5%  [OK - HEALTHY]
  8TB WD Partition 1 (Win7 Backup)              624.1 / 1862.6 GiB   33.5%  [OK - HEALTHY]
  2TB Hitachi HDD (Cron & SSD Backups)          187.2 / 1862.9 GiB   10.1%  [OK - HEALTHY]
  500GB Samsung SATA SSD (Top ODD Bay)            23.5 / 457.4 GiB    5.1%  [OK - HEALTHY]
  Stage 2A/2B Exos Array (18TB Mirror / 36TB R  [NOT YET MOUNTED - PLANNED TIER]
  Stage 3 Minisforum N5 Air AI NAS              [NOT YET MOUNTED - PLANNED TIER]
--------------------------------------------------------------------------------
  Z420 DUAL-GPU TOPOLOGY & ROLE ASSIGNMENT (Slot 2 Agentic + Slot 5 35B+ AI):
  • Active Vision Profile : GTX 1050 Ti (4096 MiB Compute 6.1, 448px Single-Patch Mode @ ~3,013 MiB)
  • GPU #0 (NVIDIA NVIDIA GeForce GTX 1050 Ti, 4096 MiB, Compute 6.1) -> PCIe Slot 2: Display / Video + Background Agentic Tasks (MiniCPM-V 4.6 Document & Photo Classifier)
  • GPU #1 (Intel Intel Arc Discrete GPU (Xe2 / B70 Class), 32768 MiB, Compute Xe2-XMX) -> PCIe Slot 5: Dedicated 35B+ AI Accelerator (Ornith 1.5 35B-A3B / Large MoE)
  • Final Target Topology : Slot 2 (16-24GB Video + Background Agentic / MiniCPM-V 4.6)
                          + Slot 5 (24GB Quadro P6000 [sm_61] or 32GB Intel Arc B70 [35B+ AI])
--------------------------------------------------------------------------------
  STAGE 2A/2B/3 66% CAPACITY TRIGGER RULES:
  • Stage 2A (2x 18TB Exos Mirror = 18.0 TB): Alert at 66% (12.0 TB) -> Add Exos #3 + #4
  • Stage 2B (4x 18TB Exos Mirrored-Striped RAID 10 = 36.0 TB):
      1. Keep 18TB Mirror (Exos #1+#2) untouched; bring Exos #3+#4 online as 36TB Stripe
      2. Copy data to 36TB Stripe & run `devvault verify-array` (100% SHA-256 check)
      3. Attach Exos #1+#2 to mirror the 36TB Stripe (RAID 10) | Alert at 66% (24.0 TB)
  • Stage 3 (Triggered at 66% / 24.0 TB): Slot 4 Minisforum ENPBA/ESP4B -> N5 Air AI NAS
================================================================================
```

### 2.2 Full `output.txt` (`devvault_search` Hybrid RAG Output)

```text
DevVault Hybrid RAG Search (`Dual GPU Slot 2 Slot 5 P6000 Intel Arc B70 MiniCPM-V 4.6`) — 8 results:

[1] **vision_minicpm.hpp** (`04_Computer_Hardware_and_Systems` | `project_code_or_doc`)
    Source: `file:///mnt/nvme_storage/ai_projects/Local knowledge base./src/vision_minicpm.hpp` | RRF: 0.0455 (Cosine: 0.297, BM25: 41.192)
[2] **vision_minicpm.hpp** (`04_Computer_Hardware_and_Systems` | `project_code_or_doc`)
    Source: `file:///mnt/nvme_storage/ai_projects/Local knowledge base./src/vision_minicpm.hpp` | RRF: 0.0431 (Cosine: 0.230, BM25: 61.082)
[3] **Antigravity Session Artifact (0f6800e7): implementation_plan.md** (`05_Computer_Programming_and_AI` | `coding_artifact`)
    Source: `file:///home/tom/.gemini/antigravity/brain/0f6800e7-50e1-4b1f-bb37-f9ed282a8b83/implementation_plan.md` | RRF: 0.0367 (Cosine: 0.195, BM25: 34.607)
[4] **IMPLEMENTATION_PLAN.md** (`04_Computer_Hardware_and_Systems` | `project_code_or_doc`)
    Source: `file:///mnt/nvme_storage/ai_projects/Local knowledge base./IMPLEMENTATION_PLAN.md` | RRF: 0.0347 (Cosine: 0.159, BM25: 46.024)
[5] **IMPLEMENTATION_PLAN.md** (`04_Computer_Hardware_and_Systems` | `project_code_or_doc`)
    Source: `file:///mnt/nvme_storage/ai_projects/Local knowledge base./IMPLEMENTATION_PLAN.md` | RRF: 0.0342 (Cosine: 0.160, BM25: 43.506)
[6] **c04798669-1.pdf** (`04_Computer_Hardware_and_Systems` | `personal_doc`)
    Source: `file:///mnt/win7_backup/Downloads/c04798669-1.pdf` | RRF: 0.0325 (Cosine: 0.194, BM25: 21.702)
[7] **c04798669.pdf** (`04_Computer_Hardware_and_Systems` | `personal_doc`)
    Source: `file:///mnt/win7_backup/Downloads/c04798669.pdf` | RRF: 0.0325 (Cosine: 0.194, BM25: 21.718)
[8] **c04200260.pdf** (`04_Computer_Hardware_and_Systems` | `personal_doc`)
    Source: `file:///mnt/win7_backup/Downloads/c04200260.pdf` | RRF: 0.0317 (Cosine: 0.306, BM25: 0.000)
```

### 2.3 Live `MiniCPM-V 4.6` Vision Classification Benchmarks (`N = 3` Runs) & Thermal Telemetry

| Run # | Target File | Mode | End-to-End Latency | Assigned Vault Category | Extracted Document Title | Live GPU & Thermal Telemetry |
| :---: | :--- | :---: | :---: | :--- | :--- | :--- |
| **1** | `pagoda-preview.webp` *(WebP Image)* | Cold Load + Classify | `13,927.3 ms` | `01_Receipts_and_Financial` *(conf 0.92)* | *Digital Model of a Landscaped Structure* | **GTX 1050 Ti:** `2,309 / 4,096 MiB` VRAM (`llama-server`: `2,052 MiB`), `31°C` |
| **2** | `/mnt/win7_backup/Downloads/c04798669.pdf` *(PDF Datasheet)* | **Warm (`keep_alive: -1`)** | **`1,366.3 ms` (`1.37 s`)** | **`04_Computer_Hardware_and_Systems`** *(conf 0.92)* | ***HP 2 Turbo Quad Pro and Dual Pro*** | **GTX 1050 Ti:** `2,309 / 4,096 MiB` VRAM, `31°C` \| **CPU:** `51.0°C` \| **NVMe:** `25.9°C` |
| **3** | `/mnt/win7_backup/Downloads/c04200260.pdf` *(PDF Datasheet)* | **Warm (`keep_alive: -1`)** | **`2,819.8 ms` (`2.82 s`)** | **`04_Computer_Hardware_and_Systems`** *(conf 0.92)* | ***PCIe Solid State Drives for HP Workstations*** | **GTX 1050 Ti:** `2,309 / 4,096 MiB` VRAM, `31°C` \| **CPU:** `51.0°C` \| **NVMe:** `25.9°C` |
