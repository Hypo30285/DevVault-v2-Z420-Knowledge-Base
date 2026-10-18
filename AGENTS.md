# DevVault v2.0 — Local-First RAG & HP Z420 Hardware Rules

1. **Local-First RAG Query Rule (Mandatory Before Web Search):**
   - Before searching the web (`search_web` / `read_url_content`), all local agents (`Ornith 1.5`) and online agents (`Gemini`, `Claude`, subagents) **MUST** query the local DevVault Vector + FTS5 knowledge base first:
     - Via MCP tool: `devvault_search` (registered under MCP server `devvault`)
     - Or via CLI: `/mnt/nvme_storage/ai_projects/Local\ knowledge\ base./bin/devvault search "<query>"`
   - When new coding solutions, compiler flags, hardware specs, or bug fixes are discovered during a session, store them immediately via `devvault_store_note` or `devvault store --title "..." --content "..."`.

2. **HP Z420 Processor ISA Compilation Rule:**
   - **Current CPU (Intel Xeon E5-2690 v1, 8C/16T Sandy Bridge):**
     Compile with `make sandybridge` (`-std=c++17 -O3 -s -march=sandybridge -mavx -msse4.2 -mno-avx2 -mno-fma -mno-f16c`).
   - **Planned CPU Upgrade (Intel Xeon E5-2697 v2, 12C/24T Ivy Bridge):**
     Once installed, recompile with `make ivybridge` (`-std=c++17 -O3 -s -march=ivybridge -mavx -msse4.2 -mf16c -mno-avx2 -mno-fma`).

3. **Phased Storage Protection Rules:**
   - **2TB NVMe (`/mnt/nvme_storage`):** Hosts `/mnt/nvme_storage/devvault/devvault.db` (SQLite WAL + FTS5 + 384-dim AVX1 vectors) and the active `256 GiB` `/mnt/nvme_storage/swapfile`.
   - **8TB WD HDD (`/mnt/storage_8tb_linux`):** Hosts `/mnt/storage_8tb_linux/Tom_Knowledge_Vault/<Category>/` (11 seeded categories + emergent clusters).
   - **2TB Hitachi HDD (`/mnt/hitachi_3AC60`):** `staging_cache` and `system_snapshots` are **PROTECTED** in place for cron jobs and SSD backups until the Stage 2A/2B Exos array and `/etc/fstab` bind mounts are active.
   - **Optical Drive Policy:** Middle ODD Bay DVD drive is for **Audio CDs** (`11_Audio_CDs_and_Music_Library`) and **data discs** only — never ingest DVD/Blu-Ray video files (`VIDEO_TS`, `BDMV`, `.vob`, `.m2ts`, `.mkv`, `.mp4`) into Z420 storage.

4. **Standard End-of-Day (EOD) Engineering Dossier Reporting Format to Bailey & Owner:**
   - Whenever generating or sending an End-of-Day (EOD) or project summary report to **Bailey** (`Bburnsed1995@gmail.com`) and **Owner / Tom** (`supertruckertom@gmail.com`), **ALWAYS** compile a comprehensive 6-Part Engineering Dossier artifact and dispatch it via `/home/tom/.local/bin/send_mail --to Bburnsed1995@gmail.com --cc supertruckertom@gmail.com --subject "..." --file <artifact_path>`:
     - **Header:** `To: Bailey (Bburnsed1995@gmail.com)`, `CC (Owner): Tom (supertruckertom@gmail.com)`, `From: Tom (tom-HP-Z420-Workstation)`, `Date`, `Subject`, and linked Table of Contents.
     - **Part 1 — Executive Project Summary:** Key milestones, hardware/software telemetry, and cross-system applicability notes for both Tom's HP Z420 (Xeon + Dual-GPU Slot 2/Slot 5) and Bailey's workstation (AMD Ryzen 9 5900XT + RTX 2060 + Intel Arc Pro B70 32GB).
     - **Part 2 — Complete Implementation Plan (`implementation_plan.md`):** Architecture diagrams, hardware/storage/PCIe tables, and phased execution plan.
     - **Part 3 — Complete Execution & Telemetry Logs (`output.txt`):** Full command outputs, benchmark tables, RAG search outputs, and live `nvidia-smi` + `lm-sensors` thermal/VRAM telemetry.
     - **Part 4 — Project Walkthrough (`walkthrough.md`) & Task Checklist (`task.md`):** Completed verification steps and status checklist.
     - **Part 5 — Complete Engineering Documentation:** Build files (`Makefile`), `AGENTS.md`, `SOURCES.md`, CLI/MCP reference, and source module breakdown.
     - **Part 6 — Complete Chronological Chat Prompts & Agent Outputs:** Every user prompt and agent output from the session in chronological order.
