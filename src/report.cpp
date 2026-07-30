#include "v2doc/report.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace v2doc {
namespace {

std::string escape_html(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
            case '&':
                escaped.append("&amp;");
                break;
            case '<':
                escaped.append("&lt;");
                break;
            case '>':
                escaped.append("&gt;");
                break;
            case '"':
                escaped.append("&quot;");
                break;
            case '\'':
                escaped.append("&#39;");
                break;
            default:
                escaped.push_back(character);
                break;
        }
    }
    return escaped;
}

void write_file(
    const std::filesystem::path &path, const std::string_view contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("unable to write report file: " + path.string());
    }
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) {
        throw std::runtime_error("unable to write report file: " + path.string());
    }
}

std::string checked_thumbnail_path(const std::filesystem::path &path) {
    if (path.empty() || path.is_absolute()) {
        throw std::invalid_argument(
            "thumbnail path must stay inside thumbnails/");
    }
    const auto normalized = path.lexically_normal();
    auto component = normalized.begin();
    if (component == normalized.end() || *component != "thumbnails") {
        throw std::invalid_argument(
            "thumbnail path must stay inside thumbnails/");
    }
    ++component;
    if (component == normalized.end()) {
        throw std::invalid_argument(
            "thumbnail path must stay inside thumbnails/");
    }
    for (; component != normalized.end(); ++component) {
        if (*component == "..") {
            throw std::invalid_argument(
                "thumbnail path must stay inside thumbnails/");
        }
    }
    return normalized.generic_string();
}

}  // namespace

std::string format_timestamp(const std::int64_t milliseconds) {
    if (milliseconds < 0) {
        throw std::invalid_argument("timestamp cannot be negative");
    }

    const auto hours = milliseconds / 3'600'000;
    const auto minutes = (milliseconds / 60'000) % 60;
    const auto seconds = (milliseconds / 1000) % 60;
    const auto remainder = milliseconds % 1000;

    std::ostringstream formatted;
    formatted << std::setfill('0') << std::setw(2) << hours << ':'
              << std::setw(2) << minutes << ':' << std::setw(2) << seconds
              << '.' << std::setw(3) << remainder;
    return formatted.str();
}

void write_report(
    const std::filesystem::path &directory,
    const std::string_view input_name,
    const std::string_view language,
    const std::span<const TranscriptBlock> blocks,
    const std::span<const std::string> warnings) {
    std::filesystem::create_directories(directory / "thumbnails");

    std::ostringstream html;
    html << "<!doctype html>\n"
         << "<html lang=\"" << escape_html(language) << "\">\n"
         << "<head>\n"
         << "<meta charset=\"utf-8\">\n"
         << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
         << "<title>Transcript: " << escape_html(input_name) << "</title>\n"
         << "<style>\n"
         << ":root{color-scheme:light;font-family:system-ui,sans-serif}"
         << "body{margin:0;background:#f8fafc;color:#111827}"
         << "main{max-width:960px;margin:auto;padding:2rem}"
         << "h1{font-size:1.6rem}.meta{color:#374151}"
         << ".warning{background:#fef3c7;color:#78350f;border:1px solid #f59e0b;"
         << "padding:.75rem;margin:.75rem 0;border-radius:.4rem}"
         << ".entry{display:grid;grid-template-columns:320px 1fr;gap:1rem;"
         << "background:#fff;border:1px solid #cbd5e1;border-radius:.5rem;"
         << "padding:1rem;margin:1rem 0;box-shadow:0 1px 2px #0001}"
         << ".entry img,.placeholder{width:100%;aspect-ratio:16/9;"
         << "object-fit:cover;border-radius:.35rem;background:#e5e7eb}"
         << ".placeholder{display:grid;place-items:center;color:#1f2937;"
         << "font-weight:600}.speaker{display:inline-block;background:#1d4ed8;"
         << "color:#fff;border-radius:999px;padding:.2rem .65rem;font-weight:700}"
         << ".time{font-variant-numeric:tabular-nums;color:#374151;margin-left:.5rem}"
         << ".text{font-size:1.05rem;line-height:1.6;white-space:pre-wrap}"
         << "@media(max-width:700px){.entry{grid-template-columns:1fr}}"
         << "</style>\n"
         << "</head>\n<body>\n<main>\n"
         << "<h1>Video transcript</h1>\n"
         << "<p class=\"meta\"><strong>Input:</strong> "
         << escape_html(input_name) << " &middot; <strong>Language:</strong> "
         << escape_html(language) << "</p>\n";

    for (const auto &warning : warnings) {
        html << "<div class=\"warning\">" << escape_html(warning)
             << "</div>\n";
    }

    nlohmann::json json;
    json["schema"] = "v2doc-transcript-v1";
    json["input"] = input_name;
    json["language"] = language;
    json["warnings"] = warnings;
    json["blocks"] = nlohmann::json::array();

    std::ostringstream text;
    for (const auto &block : blocks) {
        html << "<article class=\"entry\">\n";
        nlohmann::json thumbnail_json = nullptr;
        if (block.thumbnail.has_value()) {
            const auto thumbnail = checked_thumbnail_path(*block.thumbnail);
            html << "<img src=\"" << escape_html(thumbnail)
                 << "\" alt=\"Video frame at "
                 << escape_html(format_timestamp(block.start_ms))
                 << "\">\n";
            thumbnail_json = thumbnail;
        } else {
            html << "<div class=\"placeholder\" role=\"img\" "
                    "aria-label=\"Thumbnail unavailable\">"
                    "Thumbnail unavailable</div>\n";
        }
        html << "<div><p><span class=\"speaker\">"
             << escape_html(block.speaker)
             << "</span><span class=\"time\">"
             << escape_html(format_timestamp(block.start_ms)) << " &rarr; "
             << escape_html(format_timestamp(block.end_ms))
             << "</span></p><p class=\"text\">"
             << escape_html(block.text) << "</p></div>\n</article>\n";

        json["blocks"].push_back({
            {"start_ms", block.start_ms},
            {"end_ms", block.end_ms},
            {"speaker", block.speaker},
            {"text", block.text},
            {"thumbnail", thumbnail_json},
        });

        text << '[' << format_timestamp(block.start_ms) << " --> "
             << format_timestamp(block.end_ms) << "] " << block.speaker
             << ": " << block.text << '\n';
    }
    html << "</main>\n</body>\n</html>\n";

    write_file(directory / "index.html", html.str());
    write_file(directory / "transcript.txt", text.str());
    write_file(directory / "transcript.json", json.dump(2) + '\n');
    write_file(directory / ".v2doc-report", "v2doc-report-v1\n");
}

}  // namespace v2doc

