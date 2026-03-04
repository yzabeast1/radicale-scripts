#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

struct Config {
    fs::path sourceDir;
    fs::path archiveDir;
    int daysThreshold = -1;
    bool dryRun = false;
    bool recursive = true;
    bool includeHidden = false;
};

std::string trim(std::string_view value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::string toUpperCopy(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return input;
}

std::optional<std::chrono::sys_days> parseCompletedDateValue(const std::string& valueRaw) {
    std::string value = trim(valueRaw);
    if (value.size() < 8) {
        return std::nullopt;
    }

    auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    for (size_t i = 0; i < 8; ++i) {
        if (!isDigit(value[i])) {
            return std::nullopt;
        }
    }

    int parsedYear = std::stoi(value.substr(0, 4));
    unsigned month = static_cast<unsigned>(std::stoi(value.substr(4, 2)));
    unsigned day = static_cast<unsigned>(std::stoi(value.substr(6, 2)));

    using namespace std::chrono;
    year_month_day ymd{std::chrono::year{parsedYear}, std::chrono::month{month}, std::chrono::day{day}};
    if (!ymd.ok()) {
        return std::nullopt;
    }

    return sys_days{ymd};
}

std::vector<std::string> readUnfoldedLines(const fs::path& filePath) {
    std::ifstream input(filePath);
    if (!input) {
        throw std::runtime_error("Could not open file: " + filePath.string());
    }

    std::vector<std::string> unfolded;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!unfolded.empty() && !line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            unfolded.back() += line.substr(1);
        } else {
            unfolded.push_back(line);
        }
    }

    return unfolded;
}

struct TaskState {
    bool isCompleted = false;
    std::optional<std::chrono::sys_days> completedDate;
};

TaskState parseTaskState(const fs::path& filePath) {
    TaskState result;
    auto lines = readUnfoldedLines(filePath);

    for (const auto& originalLine : lines) {
        std::string lineUpper = toUpperCopy(originalLine);

        if (startsWith(lineUpper, "STATUS:")) {
            std::string status = trim(lineUpper.substr(std::string("STATUS:").size()));
            if (status == "COMPLETED") {
                result.isCompleted = true;
            }
            continue;
        }

        if (startsWith(lineUpper, "COMPLETED") && lineUpper.find(':') != std::string::npos) {
            size_t colonPos = originalLine.find(':');
            if (colonPos != std::string::npos) {
                auto parsed = parseCompletedDateValue(originalLine.substr(colonPos + 1));
                if (parsed.has_value()) {
                    result.completedDate = parsed;
                }
            }
            continue;
        }
    }

    return result;
}

bool hasIcsExtension(const fs::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    std::string ext = toUpperCopy(path.extension().string());
    return ext == ".ICS";
}

bool isHiddenName(const fs::path& pathPart) {
    auto name = pathPart.string();
    return !name.empty() && name[0] == '.';
}

bool pathContainsHiddenParts(const fs::path& path) {
    for (const auto& part : path) {
        if (isHiddenName(part)) {
            return true;
        }
    }
    return false;
}

void printUsage(const std::string& exeName) {
    std::cout
        << "Usage:\n"
        << "  " << exeName << " --source <tasks_dir> --archive <archive_dir> --days <N> [--dry-run] [--no-recursive]\n\n"
        << "Options:\n"
        << "  --source <path>       Directory containing CalDAV task .ics files\n"
        << "  --archive <path>      Directory where old completed tasks should be moved\n"
        << "  --days <N>            Move tasks completed more than N days ago\n"
        << "  --dry-run             Print what would be moved without changing files\n"
        << "  --no-recursive        Only scan top-level of --source\n"
        << "  --include-hidden      Include hidden files/folders (default: ignored)\n";
}

std::optional<Config> parseArgs(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--source" && i + 1 < argc) {
            cfg.sourceDir = argv[++i];
        } else if (arg == "--archive" && i + 1 < argc) {
            cfg.archiveDir = argv[++i];
        } else if (arg == "--days" && i + 1 < argc) {
            cfg.daysThreshold = std::stoi(argv[++i]);
        } else if (arg == "--dry-run") {
            cfg.dryRun = true;
        } else if (arg == "--no-recursive") {
            cfg.recursive = false;
        } else if (arg == "--include-hidden") {
            cfg.includeHidden = true;
        } else if (arg == "--help" || arg == "-h") {
            return std::nullopt;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            return std::nullopt;
        }
    }

    if (cfg.sourceDir.empty() || cfg.archiveDir.empty() || cfg.daysThreshold < 0) {
        return std::nullopt;
    }

    return cfg;
}

std::chrono::sys_days currentDayUtc() {
    using namespace std::chrono;
    auto now = system_clock::now();
    return floor<days>(now);
}

bool shouldMove(const TaskState& task, std::chrono::sys_days todayUtc, int thresholdDays) {
    if (!task.isCompleted || !task.completedDate.has_value()) {
        return false;
    }

    auto age = todayUtc - task.completedDate.value();
    auto ageDays = std::chrono::duration_cast<std::chrono::days>(age).count();
    return ageDays > thresholdDays;
}

int run(const Config& cfg) {
    if (!fs::exists(cfg.sourceDir) || !fs::is_directory(cfg.sourceDir)) {
        std::cerr << "Source directory does not exist or is not a directory: " << cfg.sourceDir << "\n";
        return 1;
    }

    if (!cfg.dryRun) {
        std::error_code ec;
        fs::create_directories(cfg.archiveDir, ec);
        if (ec) {
            std::cerr << "Failed to create archive directory: " << cfg.archiveDir << " (" << ec.message() << ")\n";
            return 1;
        }
    }

    auto today = currentDayUtc();

    size_t scanned = 0;
    size_t moved = 0;
    size_t skipped = 0;

    auto handleFile = [&](const fs::path& filePath) {
        fs::path relative = fs::relative(filePath, cfg.sourceDir);
        if (!cfg.includeHidden && pathContainsHiddenParts(relative)) {
            return;
        }

        if (!hasIcsExtension(filePath)) {
            return;
        }

        ++scanned;

        TaskState task;
        try {
            task = parseTaskState(filePath);
        } catch (const std::exception& ex) {
            std::cerr << "Skipping unreadable file " << filePath << ": " << ex.what() << "\n";
            ++skipped;
            return;
        }

        if (!shouldMove(task, today, cfg.daysThreshold)) {
            return;
        }

        fs::path destination = cfg.archiveDir / relative;

        if (cfg.dryRun) {
            std::cout << "[DRY RUN] Would move: " << filePath << " -> " << destination << "\n";
            ++moved;
            return;
        }

        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        if (ec) {
            std::cerr << "Failed to create destination directory for " << destination << ": " << ec.message() << "\n";
            ++skipped;
            return;
        }

        fs::rename(filePath, destination, ec);
        if (!ec) {
            std::cout << "Moved: " << filePath << " -> " << destination << "\n";
            ++moved;
            return;
        }

        ec.clear();
        fs::copy_file(filePath, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "Failed to copy " << filePath << " to " << destination << ": " << ec.message() << "\n";
            ++skipped;
            return;
        }

        ec.clear();
        fs::remove(filePath, ec);
        if (ec) {
            std::cerr << "Copied but failed to remove source " << filePath << ": " << ec.message() << "\n";
            ++skipped;
            return;
        }

        std::cout << "Moved (copy+remove): " << filePath << " -> " << destination << "\n";
        ++moved;
    };

    if (cfg.recursive) {
        for (fs::recursive_directory_iterator it(cfg.sourceDir), end; it != end; ++it) {
            const auto& entry = *it;
            if (!cfg.includeHidden && entry.is_directory()) {
                fs::path relDir = fs::relative(entry.path(), cfg.sourceDir);
                if (pathContainsHiddenParts(relDir)) {
                    it.disable_recursion_pending();
                    continue;
                }
            }
            if (entry.is_regular_file()) {
                handleFile(entry.path());
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(cfg.sourceDir)) {
            if (entry.is_regular_file()) {
                handleFile(entry.path());
            }
        }
    }

    std::cout << "\nScanned: " << scanned << " .ics files\n"
              << "Moved:   " << moved << "\n"
              << "Skipped: " << skipped << "\n";

    return 0;
}

int main(int argc, char* argv[]) {
    auto cfg = parseArgs(argc, argv);
    if (!cfg.has_value()) {
        printUsage(argv[0]);
        return 1;
    }

    try {
        return run(cfg.value());
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
