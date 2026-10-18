#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace devvault {

class Scrubber {
public:
    static std::string scrub_secrets(const std::string& input) {
        if (input.empty()) return input;
        std::string out = input;

        // Fast substring guard before running regexes
        bool might_have_secret =
            (out.find("AIza") != std::string::npos) ||
            (out.find("sk-") != std::string::npos) ||
            (out.find("ghp_") != std::string::npos) ||
            (out.find("github_pat_") != std::string::npos) ||
            (out.find("hf_") != std::string::npos) ||
            (out.find("AKIA") != std::string::npos) ||
            (out.find("Bearer ") != std::string::npos) ||
            (out.find("PRIVATE KEY-----") != std::string::npos) ||
            (out.find("password") != std::string::npos) ||
            (out.find("api_key") != std::string::npos) ||
            (out.find("secret") != std::string::npos);

        if (!might_have_secret) return out;

        static const std::regex re_google(R"(AIza[0-9A-Za-z\-_]{30,45})");
        static const std::regex re_openai(R"(sk-[0-9A-Za-z\-_]{20,120})");
        static const std::regex re_github(R"((ghp_[0-9A-Za-z]{30,50}|github_pat_[0-9A-Za-z_]{30,90}))");
        static const std::regex re_hf(R"(hf_[0-9A-Za-z]{28,50})");
        static const std::regex re_aws(R"(AKIA[0-9A-Z]{16})");
        static const std::regex re_bearer(R"(Bearer\s+[A-Za-z0-9\-._~+/]{20,}=*)");
        static const std::regex re_privkey(R"(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)");

        out = std::regex_replace(out, re_google, "[REDACTED_GOOGLE_API_KEY]");
        out = std::regex_replace(out, re_openai, "[REDACTED_API_KEY]");
        out = std::regex_replace(out, re_github, "[REDACTED_GITHUB_TOKEN]");
        out = std::regex_replace(out, re_hf, "[REDACTED_HF_TOKEN]");
        out = std::regex_replace(out, re_aws, "[REDACTED_AWS_KEY]");
        out = std::regex_replace(out, re_bearer, "Bearer [REDACTED_BEARER_TOKEN]");
        out = std::regex_replace(out, re_privkey, "[REDACTED_PRIVATE_KEY]");
        return out;
    }

    static std::string json_escape(const std::string& s) {
        std::ostringstream o;
        for (unsigned char c : s) {
            switch (c) {
                case '"':  o << "\\\""; break;
                case '\\': o << "\\\\"; break;
                case '\b': o << "\\b";  break;
                case '\f': o << "\\f";  break;
                case '\n': o << "\\n";  break;
                case '\r': o << "\\r";  break;
                case '\t': o << "\\t";  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                        o << buf;
                    } else {
                        o << static_cast<char>(c);
                    }
            }
        }
        return o.str();
    }

    // Minimal JSON string field extractor for MCP / transcript JSONL lines
    static std::string json_get_string(const std::string& json, const std::string& key) {
        std::string pattern = "\"" + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos + pattern.size());
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
        if (pos >= json.size() || json[pos] != '"') return "";
        pos++;
        std::string out;
        out.reserve(128);
        while (pos < json.size()) {
            char c = json[pos++];
            if (c == '\\' && pos < json.size()) {
                char esc = json[pos++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    default:   out.push_back(esc); break;
                }
            } else if (c == '"') {
                break;
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    static int64_t json_get_int(const std::string& json, const std::string& key, int64_t def = 0) {
        std::string pattern = "\"" + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return def;
        pos = json.find(':', pos + pattern.size());
        if (pos == std::string::npos) return def;
        pos++;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
        if (pos >= json.size()) return def;
        if (json[pos] == '"') pos++;
        char* endptr = nullptr;
        long long val = std::strtoll(json.c_str() + pos, &endptr, 10);
        if (endptr == json.c_str() + pos) return def;
        return static_cast<int64_t>(val);
    }

    static std::string iso8601_now() {
        std::time_t t = std::time(nullptr);
        std::tm tm_val{};
        localtime_r(&t, &tm_val);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm_val);
        return std::string(buf);
    }

    static std::string run_cmd(const std::string& cmd, size_t max_bytes = 524288) {
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        std::string out;
        char buf[4096];
        while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
            out.append(buf);
            if (out.size() >= max_bytes) break;
        }
        pclose(pipe);
        return out;
    }

    static std::string to_lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static std::string trim(const std::string& s) {
        size_t b = 0;
        while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
        size_t e = s.size();
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
        return s.substr(b, e - b);
    }
};

} // namespace devvault
