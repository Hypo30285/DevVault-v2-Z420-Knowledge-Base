# DevVault Sources Watchlist & Road-Warrior Mobile Sync (`SOURCES.md`)

> **How This File Works:**
> `devvault road-sync` and `devvault web-mirror` read this file (and synchronize it two-way with your **DevVault Road Inbox & Sources Watchlist** Google Doc on `supertruckertom@gmail.com`) so that every Google Doc, web page, and mobile **Gemini App** conversation you save while on the road 5 days a week is automatically pulled into your local Z420 RAG database (`/mnt/nvme_storage/devvault/devvault.db`) and mirrored weekly for changes.

---

## 1. How to Add Mobile Gemini Conversations & Links While on the Road (5 Days/Week)

While away from the HP Z420 on your cell phone, use any of these three methods:

1. **Method A — Gemini Mobile App "Export to Docs" (Best for Full Mobile Chats):**
   - In the Gemini app on your phone, tap **Share & export $\rightarrow$ Export to Docs** at the bottom of a conversation/response.
   - Rename or prefix the exported Google Doc with `[DevVault]` (e.g., `[DevVault] NAS PCIe Lane Planning`), or paste its link into your **DevVault Road Inbox** Google Doc.
2. **Method B — Paste Shared Gemini Links or Web URLs into Your "DevVault Road Inbox" Google Doc:**
   - Open your pinned **DevVault Road Inbox & Sources Watchlist** Google Doc in the Google Docs phone app.
   - Paste any **Gemini Share Link** (`https://gemini.google.com/share/...` or `https://g.co/gemini/share/...`), **Google Doc URL**, or **Web Page URL** (product specs, datasheets, articles, forums).
   - You can optionally add a short note or `#category` tag next to the link.
3. **Method C — Edit This Local `SOURCES.md` Directly (When at the Z420):**
   - Add any URL, Google Doc link, or local path under the sections below, or run:
     ```bash
     devvault source-add "https://example.com/article" --category "04_Computer_Hardware_and_Systems"
     ```

---

## 2. Pinned Google Docs (Synced via `gdocs` CLI)

- [DevVault Pre-Planning Architecture & Deployment Spec](https://docs.google.com/document/d/1DtMGkzhTK6f2MuQIxxKU2JwDRFiHMoLFabAlKoH5i8Y/edit?tab=t.0#heading=h.7cripsdeugq7) — `category: 05_Computer_Programming_and_AI` | `sync: nightly`

---

## 3. Mobile Gemini App Conversations (Captured on the Road)

<!-- Paste Gemini App share links (https://gemini.google.com/share/...) or Exported Google Doc links below.
     `devvault road-sync` also auto-appends links synced from your cloud Road Inbox Google Doc here. -->

---

## 4. Frequently Referenced Web Pages & Hardware Parts Watchlist (Cached in Local RAG + Mirrored Weekly)

### HP Z420 Internal Drive Expansion & OEM Part Numbers (`04_Computer_Hardware_and_Systems`)
- **HP Z420 5.25" Optical Bay to 3.5" HDD Carrier Assembly:**
  - **HP Spare Part Number:** **`683870-001`** (Tool-less green latch carrier for mounting a 3.5-inch HDD in the Z420 5.25-inch ODD bay; enables 4+ 3.5-inch HDDs inside the Z420 tower alongside the 3 internal 3.5" bays).
  - *(Related Dual 2.5" SSD Carrier for Z420 5.25" Bay: HP Spare **`660542-001`**)*
- **Seagate Exos 18TB SATA Power Adapter (Micro Center):**
  - **4-Pin Molex (LP4) to 15-Pin SATA Power Adapter / 4-Way Splitter** (e.g., StarTech `PYO4SATA` or Cable Matters crimped Molex-to-SATA splitter — bypasses SATA Pin 3 `+3.3V PWDIS` spin-up lock).

### Stage-3 PCIe Expansion & AI NAS Roadmap (`04_Computer_Hardware_and_Systems`)
- [MINISFORUM ENPBA PCIe Card (Intel E810 Dual 25GbE SFP28 + Dual M.2 NVMe — Z420 Slot 4 Target)](https://store.minisforum.com/products/minisforum-enpba) — `mirror: weekly`
- [MINISFORUM N5 Air AI NAS (Ryzen 7 255, 5-Bay up to 174TB, 10GbE+5GbE, PCIe x16 & OCuLink)](https://store.minisforum.com/products/minisforum-n5-air-ai-nas) — `mirror: weekly`
- [MINISFORUM ESP4B PCIe to 4x NVMe M.2 Expansion Card (PCIe 4.0 x4 + OCuLink Expansion)](https://store.minisforum.com/products/minisforum-esp4b-pcie-to-4-nvme-m-2-expansion-card) — `mirror: weekly`

### AI, Programming & Benchmarking (`05_Computer_Programming_and_AI`)
- [Hugging Face Lighteval Documentation](https://huggingface.co/docs/lighteval/en/index) — `mirror: weekly`
- [Google Antigravity Documentation](https://antigravity.google/docs) — `mirror: weekly`
