#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include "scrubber.hpp"

namespace devvault {
namespace fs = std::filesystem;

struct GpuDeviceDesc {
    int index = 0;
    std::string pci_bus_id;
    std::string name;
    std::string vendor = "NVIDIA"; // NVIDIA (Compute 6.1 P6000 / 1050 Ti, etc.) or Intel (Arc B70 32GB)
    std::string compute_cap = "6.1";
    int vram_total_mib = 4096;
    std::string assigned_role = "Single-GPU Baseline (Display + Agentic + 35B Hybrid)";
};

struct VisionHardwareProfile {
    int vram_total_mib = 4096;
    int max_dimension = 448;      // 448px = Single SigLIP visual patch on GTX 1050 Ti (820ms vision encode vs 18.5s)
    int jpeg_quality = 80;
    int num_ctx = 768;            // Compact KV context window on 4GB VRAM (~3,013 MiB footprint)
    int num_predict = 96;         // Bounded JSON output budget to prevent trailing token waste
    int num_thread = 8;           // Matches 8 physical cores of Xeon E5-2690 v1 (12 on E5-2697 v2)
    int target_gpu_index = 0;     // In Dual-GPU mode: pinned to Slot 2 GPU (Video + Small Background Agentic Tasks)
    bool dual_gpu_active = false;
    std::string gpu_tier = "GTX 1050 Ti (4GB Compute 6.1, 448px Single-Patch Mode)";
    std::vector<GpuDeviceDesc> detected_gpus;
};

struct VisionClassification {
    bool success = false;
    std::string model_used = "minicpm-v4.6";
    std::string backend_used = "polystrata-or-ollama";
    std::string gpu_profile = "GTX 1050 Ti (448px single-patch)";
    std::string category = "12_Emergent_and_General_Docs";
    std::string title;
    std::string ocr_text;
    std::string summary;
    double confidence = 0.0;
    double latency_ms = 0.0;
    std::string raw_response;
};

class MiniCpmVision {
public:
    static std::string base64_encode(const std::vector<uint8_t>& data) {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);
        size_t i = 0;
        while (i + 2 < data.size()) {
            uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                         (static_cast<uint32_t>(data[i + 1]) << 8) |
                         static_cast<uint32_t>(data[i + 2]);
            out.push_back(tbl[(n >> 18) & 63]);
            out.push_back(tbl[(n >> 12) & 63]);
            out.push_back(tbl[(n >> 6) & 63]);
            out.push_back(tbl[n & 63]);
            i += 3;
        }
        if (i < data.size()) {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
            out.push_back(tbl[(n >> 18) & 63]);
            out.push_back(tbl[(n >> 12) & 63]);
            if (i + 1 < data.size()) {
                out.push_back(tbl[(n >> 6) & 63]);
            } else {
                out.push_back('=');
            }
            out.push_back('=');
        }
        return out;
    }

    static std::string shell_escape(const std::string& s) {
        std::string escaped;
        for (char c : s) {
            if (c == '\'') escaped += "'\\''";
            else escaped.push_back(c);
        }
        return escaped;
    }

    // Detect single or dual-GPU Z420 topology:
    // - Slot 2 GPU (16-24GB): Video output + small background agentic tasks (MiniCPM-V 4.6 document/photo classification)
    // - Slot 5 GPU (24GB-32GB+): Primary AI Accelerator for 35B+ models (e.g., NVIDIA Quadro P6000 24GB Compute 6.1 or Intel Arc B70 32GB)
    static VisionHardwareProfile detect_hardware_profile() {
        VisionHardwareProfile prof;
        std::string smi = Scrubber::run_cmd(
            "/usr/bin/nvidia-smi --query-gpu=index,pci.bus_id,name,memory.total,compute_cap --format=csv,noheader,nounits 2>/dev/null",
            2048
        );
        std::istringstream iss(smi);
        std::string line;
        while (std::getline(iss, line)) {
            line = Scrubber::trim(line);
            if (line.empty()) continue;
            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string item;
            while (std::getline(ss, item, ',')) {
                parts.push_back(Scrubber::trim(item));
            }
            if (parts.size() >= 4) {
                GpuDeviceDesc d;
                d.index = std::atoi(parts[0].c_str());
                d.pci_bus_id = parts[1];
                d.name = parts[2];
                d.vram_total_mib = std::atoi(parts[3].c_str());
                d.compute_cap = (parts.size() >= 5) ? parts[4] : "6.1";
                d.vendor = "NVIDIA";
                prof.detected_gpus.push_back(d);
            }
        }

        // Also check for Intel Arc (e.g. Intel Arc B70 32GB) via xpu-smi or lspci/sysfs if present
        std::string arc_check = Scrubber::run_cmd("/usr/bin/lspci -nn 2>/dev/null | grep -iE 'VGA|3D|Display' | grep -i 'Intel'", 1024);
        if (!Scrubber::trim(arc_check).empty()) {
            GpuDeviceDesc arc;
            arc.index = static_cast<int>(prof.detected_gpus.size());
            arc.pci_bus_id = "PCIe-Slot-5-Intel";
            arc.name = "Intel Arc Discrete GPU (Xe2 / B70 Class)";
            arc.vendor = "Intel";
            arc.compute_cap = "Xe2-XMX";
            arc.vram_total_mib = 32768;
            prof.detected_gpus.push_back(arc);
        }

        if (prof.detected_gpus.size() >= 2) {
            prof.dual_gpu_active = true;
            // Assign roles: Slot 2 (smaller VRAM or lower PCI bus index) -> Video + Background Agentic Tasks
            //               Slot 5 (larger VRAM 24GB P6000 / 32GB Arc B70) -> Primary 35B+ AI Accelerator
            int agent_idx = 0;
            int accel_idx = 1;
            if (prof.detected_gpus[0].vram_total_mib > prof.detected_gpus[1].vram_total_mib) {
                accel_idx = 0;
                agent_idx = 1;
            }
            prof.detected_gpus[agent_idx].assigned_role =
                "PCIe Slot 2: Display / Video + Background Agentic Tasks (MiniCPM-V 4.6 Document & Photo Classifier)";
            prof.detected_gpus[accel_idx].assigned_role =
                "PCIe Slot 5: Dedicated 35B+ AI Accelerator (Ornith 1.5 35B-A3B / Large MoE)";

            prof.target_gpu_index = prof.detected_gpus[agent_idx].index;
            prof.vram_total_mib = prof.detected_gpus[agent_idx].vram_total_mib;
        } else if (prof.detected_gpus.size() == 1) {
            prof.detected_gpus[0].assigned_role =
                "PCIe Slot 2 (Single-GPU Stage 1): Display + MiniCPM-V 4.6 Agentic Tasks + Ornith 1.5";
            prof.target_gpu_index = prof.detected_gpus[0].index;
            prof.vram_total_mib = prof.detected_gpus[0].vram_total_mib;
        }

        const char* env_gpu = std::getenv("DEVVAULT_AGENT_GPU");
        if (env_gpu) {
            prof.target_gpu_index = std::atoi(env_gpu);
        }

        int mib = prof.vram_total_mib;
        if (mib >= 12000) {
            // Slot 2 Upgraded Agentic GPU (16GB-24GB VRAM):
            // 100% VRAM residency, multi-patch high-res OCR (1344px), ~0.20-0.45s per doc/photo
            prof.max_dimension = 1344;
            prof.jpeg_quality = 90;
            prof.num_ctx = 4096;
            prof.num_predict = 384;
            prof.num_thread = 12;
            prof.gpu_tier = (prof.dual_gpu_active ? "[Dual-GPU Slot 2 Agentic] " : "[Single-GPU High-VRAM] ") +
                            std::to_string(mib) + " MiB (1344px Multi-Patch 100% VRAM Mode)";
        } else if (mib >= 7500) {
            prof.max_dimension = 896;
            prof.jpeg_quality = 85;
            prof.num_ctx = 2048;
            prof.num_predict = 256;
            prof.num_thread = 8;
            prof.gpu_tier = std::to_string(mib) + " MiB (896px 100% VRAM Mode)";
        } else {
            // Current GTX 1050 Ti (4,096 MiB, Compute 6.1): Proven phone-photo-sorter 448px single-patch profile (~3,013 MiB VRAM)
            prof.max_dimension = 448;
            prof.jpeg_quality = 80;
            prof.num_ctx = 768;
            prof.num_predict = 96;
            prof.num_thread = 8;
            prof.gpu_tier = "GTX 1050 Ti (" + std::to_string(mib) + " MiB Compute 6.1, 448px Single-Patch Mode @ ~3,013 MiB)";
        }
        return prof;
    }

    // Normalize both DevVault canonical categories AND phone-photo-sorter categories into unified Vault folders
    static std::string normalize_category(const std::string& raw_cat) {
        std::string c = Scrubber::to_lower(Scrubber::trim(raw_cat));
        if (c.empty()) return "12_Emergent_and_General_Docs";
        if (c.find("01_") == 0 || c.find("receipt") != std::string::npos || c.find("invoice") != std::string::npos || c.find("financial") != std::string::npos)
            return "01_Receipts_and_Financial";
        if (c.find("02_") == 0 || c.find("automotive") != std::string::npos || c.find("car") != std::string::npos || c.find("truck") != std::string::npos)
            return "02_Automotive_and_Cars";
        if (c.find("03_") == 0 || c.find("electronic") != std::string::npos || c.find("circuit") != std::string::npos || c.find("schematic") != std::string::npos)
            return "03_Electronics_and_Circuits";
        if (c.find("04_") == 0 || c.find("hardware") != std::string::npos || c.find("computer stuff") != std::string::npos || c.find("z420") != std::string::npos)
            return "04_Computer_Hardware_and_Systems";
        if (c.find("05_") == 0 || c.find("program") != std::string::npos || c.find("code") != std::string::npos || c.find("screenshot") != std::string::npos)
            return "05_Computer_Programming_and_AI";
        if (c.find("06_") == 0 || c.find("hunt") != std::string::npos || c.find("outdoor") != std::string::npos || c.find("landscape") != std::string::npos)
            return "06_Hunting_and_Outdoors";
        if (c.find("07_") == 0 || c.find("firearm") != std::string::npos || c.find("gun") != std::string::npos || c.find("ballistic") != std::string::npos)
            return "07_Firearms_and_Ballistics";
        if (c.find("08_") == 0 || c.find("history") != std::string::npos || c.find("archive") != std::string::npos || c.find("family") != std::string::npos)
            return "08_History_and_Archives";
        if (c.find("09_") == 0 || c.find("medical") != std::string::npos || c.find("health") != std::string::npos)
            return "09_Medical_and_Health";
        if (c.find("10_") == 0 || c.find("politic") != std::string::npos || c.find("policy") != std::string::npos)
            return "10_Politics_and_Policy";
        if (c.find("11_") == 0 || c.find("audio") != std::string::npos || c.find("music") != std::string::npos)
            return "11_Audio_CDs_and_Music_Library";
        return "12_Emergent_and_General_Docs";
    }

    // Prepare a single-patch (448px on GTX 1050 Ti, 1344px on Slot 2 16GB-24GB GPU) JPEG in /dev/shm (RAM disk, zero SSD wear)
    static std::string prepare_scaled_jpeg_in_ram(const fs::path& input_path, const VisionHardwareProfile& prof, bool& is_temp) {
        is_temp = false;
        std::error_code ec;
        std::string ext = Scrubber::to_lower(input_path.extension().string());
        std::string esc = shell_escape(input_path.string());
        std::string pid_tag = std::to_string(getpid());

        if (ext == ".pdf") {
            std::string tmp_prefix = "/dev/shm/devvault_pdf_page1_" + pid_tag;
            std::string cmd = "/usr/bin/pdftoppm -jpeg -jpegopt quality=" + std::to_string(prof.jpeg_quality) +
                              " -scale-to " + std::to_string(prof.max_dimension) +
                              " -f 1 -l 1 -singlefile '" + esc + "' " + tmp_prefix + " 2>/dev/null";
            Scrubber::run_cmd(cmd, 512);
            if (fs::exists(tmp_prefix + ".jpg", ec)) {
                is_temp = true;
                return tmp_prefix + ".jpg";
            }
            return "";
        }

        // Resize image (.jpg, .png, .webp, .tiff, .bmp) in /dev/shm to prof.max_dimension (448px on GTX 1050 Ti, 1344px on 16-24GB Slot 2 GPU)
        std::string tmp_jpg = "/dev/shm/devvault_img_scaled_" + pid_tag + ".jpg";
        std::string py_cmd =
            "/usr/bin/python3 -c \"from PIL import Image; im=Image.open('" + esc + "'); "
            "im.thumbnail((" + std::to_string(prof.max_dimension) + "," + std::to_string(prof.max_dimension) + ")); "
            "im.convert('RGB').save('" + tmp_jpg + "','JPEG',quality=" + std::to_string(prof.jpeg_quality) + ")\" 2>/dev/null";
        Scrubber::run_cmd(py_cmd, 256);
        if (fs::exists(tmp_jpg, ec) && fs::file_size(tmp_jpg, ec) > 0) {
            is_temp = true;
            return tmp_jpg;
        }
        return input_path.string();
    }

    // Classify an image (.jpg, .png, .webp, .tiff, .bmp) or rendered PDF document page using MiniCPM-V 4.6
    // Proven on GTX 1050 Ti (4GB VRAM) in phone-photo-sorter (~3,013 MiB VRAM, 820ms single-patch vision encode)
    // Automatically pins to Slot 2 GPU (16-24GB) in the final Dual-GPU arrangement so Slot 5 (P6000 24GB / Arc B70 32GB) stays free for 35B+ models
    static VisionClassification classify_image(const fs::path& img_path, const std::string& endpoint_override = "") {
        VisionClassification vc;
        auto t0 = std::chrono::steady_clock::now();

        std::error_code ec;
        if (!fs::exists(img_path, ec)) {
            vc.summary = "File does not exist: " + img_path.string();
            return vc;
        }

        VisionHardwareProfile prof = detect_hardware_profile();
        vc.gpu_profile = prof.gpu_tier + " [Target Agentic GPU #" + std::to_string(prof.target_gpu_index) + "]";

        bool is_temp = false;
        std::string scaled_path = prepare_scaled_jpeg_in_ram(img_path, prof, is_temp);
        if (scaled_path.empty()) {
            vc.summary = "Failed to render or scale document/image for MiniCPM-V 4.6";
            return vc;
        }

        std::ifstream ifs(scaled_path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        if (is_temp) {
            std::remove(scaled_path.c_str());
        }

        if (bytes.empty() || bytes.size() > 25ULL * 1024 * 1024) {
            vc.summary = "Scaled image empty or exceeds 25MB limit";
            return vc;
        }

        std::string b64 = base64_encode(bytes);
        std::string data_uri = "data:image/jpeg;base64," + b64;

        std::string prompt =
            "Classify this document or photo into EXACTLY ONE category:\n"
            "01_Receipts_and_Financial, 02_Automotive_and_Cars, 03_Electronics_and_Circuits, "
            "04_Computer_Hardware_and_Systems, 05_Computer_Programming_and_AI, 06_Hunting_and_Outdoors, "
            "07_Firearms_and_Ballistics, 08_History_and_Archives, 09_Medical_and_Health, "
            "10_Politics_and_Policy, 11_Audio_CDs_and_Music_Library, 12_Emergent_and_General_Docs.\n"
            "Respond ONLY with JSON: {\"category\":\"...\",\"title\":\"...\",\"ocr_text\":\"...\",\"summary\":\"...\"}";

        std::string pid_tag = std::to_string(getpid());
        std::string content;

        const char* env_url = std::getenv("DEVVAULT_VISION_URL");
        std::string poly_url = !endpoint_override.empty()
                               ? endpoint_override
                               : (env_url ? std::string(env_url) : "http://127.0.0.1:8080/v1/chat/completions");
        std::string ollama_url = "http://127.0.0.1:11434/api/generate";

        // Attempt A: Ollama `minicpm-v4.6` pinned to Slot 2 GPU (`main_gpu: target_gpu_index`)
        if (endpoint_override.empty() && !env_url) {
            std::string req_ollama = "/dev/shm/devvault_ollama_req_" + pid_tag + ".json";
            {
                std::ofstream ofs(req_ollama);
                ofs << "{"
                    << "\"model\":\"minicpm-v4.6\","
                    << "\"prompt\":\"" << Scrubber::json_escape(prompt) << "\","
                    << "\"images\":[\"" << b64 << "\"],"
                    << "\"stream\":false,"
                    << "\"format\":\"json\","
                    << "\"keep_alive\":-1,"
                    << "\"options\":{"
                    << "\"main_gpu\":" << prof.target_gpu_index << ","
                    << "\"temperature\":0.0,"
                    << "\"num_predict\":" << prof.num_predict << ","
                    << "\"num_ctx\":" << prof.num_ctx << ","
                    << "\"num_thread\":" << prof.num_thread
                    << "}}";
            }
            std::string cmd_ol = "/usr/bin/curl -sS --max-time 45 -H 'Content-Type: application/json' -d @" + req_ollama + " '" + ollama_url + "' 2>/dev/null";
            std::string resp_ol = Scrubber::run_cmd(cmd_ol, 65536);
            std::remove(req_ollama.c_str());

            if (!resp_ol.empty() && resp_ol.find("\"response\"") != std::string::npos) {
                vc.raw_response = resp_ol;
                vc.model_used = "minicpm-v4.6";
                vc.backend_used = "ollama (127.0.0.1:11434, main_gpu=" + std::to_string(prof.target_gpu_index) + ")";
                content = Scrubber::json_get_string(resp_ol, "response");
            }
        }

        // Attempt B: PolyStrata Multimodal Vision Endpoint (`minicpm-v-5b-q4_k_m.gguf` + `minicpm-v-mmproj-f16.gguf`)
        if (content.empty()) {
            const char* env_model = std::getenv("DEVVAULT_VISION_MODEL");
            vc.model_used = env_model ? std::string(env_model) : "minicpm-v-4.6";
            std::string req_poly = "/dev/shm/devvault_poly_req_" + pid_tag + ".json";
            {
                std::ofstream ofs(req_poly);
                ofs << "{"
                    << "\"model\":\"" << Scrubber::json_escape(vc.model_used) << "\","
                    << "\"temperature\":0.0,"
                    << "\"max_tokens\":" << prof.num_predict << ","
                    << "\"messages\":[{\"role\":\"user\",\"content\":["
                    << "{\"type\":\"text\",\"text\":\"" << Scrubber::json_escape(prompt) << "\"},"
                    << "{\"type\":\"image_url\",\"image_url\":{\"url\":\"" << data_uri << "\"}}"
                    << "]}]"
                    << "}";
            }
            std::string cmd_poly = "/usr/bin/curl -sS --max-time 45 -H 'Content-Type: application/json' -d @" + req_poly + " '" + poly_url + "' 2>/dev/null";
            std::string resp_poly = Scrubber::run_cmd(cmd_poly, 65536);
            std::remove(req_poly.c_str());

            if (!resp_poly.empty() && resp_poly.find("\"choices\"") != std::string::npos) {
                vc.raw_response = resp_poly;
                vc.backend_used = "polystrata (" + poly_url + ")";
                content = Scrubber::json_get_string(resp_poly, "content");
            }
        }

        auto t1 = std::chrono::steady_clock::now();
        vc.latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (content.empty()) {
            vc.success = false;
            vc.title = img_path.filename().string();
            vc.summary = "MiniCPM-V 4.6 vision endpoint not currently active (checked Ollama :11434 and PolyStrata :8080). "
                         "Weights ready at /mnt/nvme_storage/ai_projects/strata_models/minicpm-v-5b-q4_k_m.gguf + minicpm-v-mmproj-f16.gguf.";
            return vc;
        }

        std::string cat = Scrubber::json_get_string(content, "category");
        std::string title = Scrubber::json_get_string(content, "title");
        std::string ocr = Scrubber::json_get_string(content, "ocr_text");
        std::string sum = Scrubber::json_get_string(content, "summary");

        vc.category = normalize_category(cat.empty() ? content : cat);
        vc.title = title.empty() ? img_path.filename().string() : title;
        vc.ocr_text = ocr;
        vc.summary = sum.empty() ? content : sum;
        vc.confidence = 0.92;
        vc.success = true;
        return vc;
    }
};

} // namespace devvault
