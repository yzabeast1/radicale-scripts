#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

bool startsWith(const std::string &text, const std::string &prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::string toUpperCopy(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return input;
}

bool isDoneStatus(const std::string &statusUpper) {
    return statusUpper == "COMPLETED" || statusUpper == "CANCELLED";
}

bool isBasicUtcDateTime(const std::string &value) {
    if (value.size() != 16 || value[8] != 'T' || value[15] != 'Z') {
        return false;
    }

    auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 15) {
            continue;
        }
        if (!isDigit(value[i])) {
            return false;
        }
    }

    return true;
}

std::optional<std::chrono::sys_days> localDayFromUtcDateTime(const std::string &value) {
    int parsedYear = std::stoi(value.substr(0, 4));
    int parsedMonth = std::stoi(value.substr(4, 2));
    int parsedDay = std::stoi(value.substr(6, 2));
    int parsedHour = std::stoi(value.substr(9, 2));
    int parsedMinute = std::stoi(value.substr(11, 2));
    int parsedSecond = std::stoi(value.substr(13, 2));

    std::tm utcTimeParts{};
    utcTimeParts.tm_year = parsedYear - 1900;
    utcTimeParts.tm_mon = parsedMonth - 1;
    utcTimeParts.tm_mday = parsedDay;
    utcTimeParts.tm_hour = parsedHour;
    utcTimeParts.tm_min = parsedMinute;
    utcTimeParts.tm_sec = parsedSecond;

    std::time_t utcTime = timegm(&utcTimeParts);
    std::tm localTimeParts{};
    if (localtime_r(&utcTime, &localTimeParts) == nullptr) {
        return std::nullopt;
    }

    using namespace std::chrono;
    year_month_day ymd{
        std::chrono::year{localTimeParts.tm_year + 1900},
        std::chrono::month{static_cast<unsigned>(localTimeParts.tm_mon + 1)},
        std::chrono::day{static_cast<unsigned>(localTimeParts.tm_mday)}};
    if (!ymd.ok()) {
        return std::nullopt;
    }

    return sys_days{ymd};
}

std::optional<std::chrono::sys_days> parseCompletedDateValue(const std::string &valueRaw) {
    std::string value = trim(valueRaw);
    if (isBasicUtcDateTime(value)) {
        return localDayFromUtcDateTime(value);
    }

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

std::vector<std::string> readUnfoldedLines(const fs::path &filePath) {
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
        }
        else {
            unfolded.push_back(line);
        }
    }

    return unfolded;
}

struct TaskState {
    bool isCompleted = false;
    std::optional<std::chrono::sys_days> completedDate;
};

struct TaskMetadata {
    TaskState state;
    std::string uid;
    std::vector<std::string> parentUids;
    std::vector<std::string> childUids;
};

TaskState parseTaskState(const fs::path &filePath) {
    TaskState result;
    auto lines = readUnfoldedLines(filePath);

    for (const auto &originalLine : lines) {
        std::string lineUpper = toUpperCopy(originalLine);

        if (startsWith(lineUpper, "STATUS:")) {
            std::string status = trim(lineUpper.substr(std::string("STATUS:").size()));
            if (isDoneStatus(status)) {
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

TaskMetadata parseTaskMetadata(const fs::path &filePath) {
    TaskMetadata result;
    auto lines = readUnfoldedLines(filePath);

    for (const auto &originalLine : lines) {
        std::string lineUpper = toUpperCopy(originalLine);

        if (startsWith(lineUpper, "STATUS:")) {
            std::string status = trim(lineUpper.substr(std::string("STATUS:").size()));
            if (isDoneStatus(status)) {
                result.state.isCompleted = true;
            }
            continue;
        }

        if (startsWith(lineUpper, "COMPLETED") && lineUpper.find(':') != std::string::npos) {
            size_t colonPos = originalLine.find(':');
            if (colonPos != std::string::npos) {
                auto parsed = parseCompletedDateValue(originalLine.substr(colonPos + 1));
                if (parsed.has_value()) {
                    result.state.completedDate = parsed;
                }
            }
            continue;
        }

        size_t colonPos = originalLine.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }

        std::string nameAndParamsUpper = toUpperCopy(originalLine.substr(0, colonPos));
        std::string value = trim(originalLine.substr(colonPos + 1));

        if ((nameAndParamsUpper == "UID" || startsWith(nameAndParamsUpper, "UID;")) && !value.empty()) {
            result.uid = value;
            continue;
        }

        if (nameAndParamsUpper == "RELATED-TO" || startsWith(nameAndParamsUpper, "RELATED-TO;")) {
            bool hasReltype = nameAndParamsUpper.find("RELTYPE=") != std::string::npos;
            bool isParent = nameAndParamsUpper.find("RELTYPE=PARENT") != std::string::npos;
            bool isChild = nameAndParamsUpper.find("RELTYPE=CHILD") != std::string::npos;
            if ((!hasReltype || isParent) && !value.empty()) {
                result.parentUids.push_back(value);
            }
            else if (isChild && !value.empty()) {
                result.childUids.push_back(value);
            }
        }
    }

    return result;
}

bool hasIcsExtension(const fs::path &path) {
    if (!path.has_extension()) {
        return false;
    }
    std::string ext = toUpperCopy(path.extension().string());
    return ext == ".ICS";
}

bool isHiddenName(const fs::path &pathPart) {
    auto name = pathPart.string();
    return !name.empty() && name[0] == '.';
}

bool pathContainsHiddenParts(const fs::path &path) {
    for (const auto &part : path) {
        if (isHiddenName(part)) {
            return true;
        }
    }
    return false;
}

void printUsage(const std::string &exeName) {
    std::cout << "Usage:\n"
              << "  " << exeName << " --source <tasks_dir> --archive <archive_dir> --days <N> [--dry-run] [--no-recursive]\n\n"
              << "Options:\n"
              << "  --source <path>       Directory containing CalDAV task .ics files\n"
              << "  --archive <path>      Directory where old done tasks should be moved\n"
              << "  --days <N>            Move tasks completed more than N days ago\n"
              << "  --dry-run             Print what would be moved without changing files\n"
              << "  --no-recursive        Only scan top-level of --source\n"
              << "  --include-hidden      Include hidden files/folders (default: ignored)\n";
}

std::optional<Config> parseArgs(int argc, char *argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--source" && i + 1 < argc) {
            cfg.sourceDir = argv[++i];
        }
        else if (arg == "--archive" && i + 1 < argc) {
            cfg.archiveDir = argv[++i];
        }
        else if (arg == "--days" && i + 1 < argc) {
            cfg.daysThreshold = std::stoi(argv[++i]);
        }
        else if (arg == "--dry-run") {
            cfg.dryRun = true;
        }
        else if (arg == "--no-recursive") {
            cfg.recursive = false;
        }
        else if (arg == "--include-hidden") {
            cfg.includeHidden = true;
        }
        else if (arg == "--help" || arg == "-h") {
            return std::nullopt;
        }
        else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            return std::nullopt;
        }
    }

    if (cfg.sourceDir.empty() || cfg.archiveDir.empty() || cfg.daysThreshold < 0) {
        return std::nullopt;
    }

    return cfg;
}

std::chrono::sys_days currentLocalDay() {
    std::time_t now = std::time(nullptr);
    std::tm localNow{};
    if (localtime_r(&now, &localNow) == nullptr) {
        throw std::runtime_error("Could not determine current local date");
    }

    using namespace std::chrono;
    year_month_day ymd{
        std::chrono::year{localNow.tm_year + 1900},
        std::chrono::month{static_cast<unsigned>(localNow.tm_mon + 1)},
        std::chrono::day{static_cast<unsigned>(localNow.tm_mday)}};
    return sys_days{ymd};
}

bool shouldMove(const TaskState &task, std::chrono::sys_days todayLocal, int thresholdDays) {
    if (!task.isCompleted || !task.completedDate.has_value()) {
        return false;
    }

    auto age = todayLocal - task.completedDate.value();
    auto ageDays = std::chrono::duration_cast<std::chrono::days>(age).count();
    return ageDays > thresholdDays;
}

bool connectedTreeHasNotOldEnoughTask(const TaskMetadata &task, const std::unordered_map<std::string, std::vector<std::string>> &adjacentUidsByUid, const std::unordered_map<std::string, TaskMetadata> &metadataByUid, std::chrono::sys_days todayLocal, int thresholdDays) {
    if (task.uid.empty()) {
        return false;
    }

    if (!shouldMove(task.state, todayLocal, thresholdDays)) {
        return true;
    }

    std::vector<std::string> pending;
    if (auto it = adjacentUidsByUid.find(task.uid); it != adjacentUidsByUid.end()) {
        pending = it->second;
    }

    std::unordered_set<std::string> visited;
    visited.insert(task.uid);

    while (!pending.empty()) {
        std::string relatedUid = pending.back();
        pending.pop_back();

        if (relatedUid.empty() || !visited.insert(relatedUid).second) {
            continue;
        }

        auto relatedIt = metadataByUid.find(relatedUid);
        if (relatedIt == metadataByUid.end()) {
            continue;
        }

        const TaskMetadata &related = relatedIt->second;
        if (!shouldMove(related.state, todayLocal, thresholdDays)) {
            return true;
        }

        auto adjacentIt = adjacentUidsByUid.find(relatedUid);
        if (adjacentIt != adjacentUidsByUid.end()) {
            pending.insert(pending.end(), adjacentIt->second.begin(), adjacentIt->second.end());
        }
    }

    return false;
}

int run(const Config &cfg) {
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

    auto today = currentLocalDay();

    std::vector<fs::path> taskFiles;

    auto considerFile = [&](const fs::path &filePath) {
        fs::path relative = fs::relative(filePath, cfg.sourceDir);
        if (!cfg.includeHidden && pathContainsHiddenParts(relative)) {
            return;
        }

        if (!hasIcsExtension(filePath)) {
            return;
        }

        taskFiles.push_back(filePath);
    };

    if (cfg.recursive) {
        for (fs::recursive_directory_iterator it(cfg.sourceDir), end; it != end; ++it) {
            const auto &entry = *it;
            if (!cfg.includeHidden && entry.is_directory()) {
                fs::path relDir = fs::relative(entry.path(), cfg.sourceDir);
                if (pathContainsHiddenParts(relDir)) {
                    it.disable_recursion_pending();
                    continue;
                }
            }
            if (entry.is_regular_file()) {
                considerFile(entry.path());
            }
        }
    }
    else {
        for (const auto &entry : fs::directory_iterator(cfg.sourceDir)) {
            if (entry.is_regular_file()) {
                considerFile(entry.path());
            }
        }
    }

    size_t scanned = taskFiles.size();
    size_t moved = 0;
    size_t skipped = 0;
    std::unordered_map<std::string, TaskMetadata> metadataByPath;
    std::unordered_map<std::string, TaskMetadata> metadataByUid;
    std::unordered_map<std::string, std::vector<std::string>> adjacentUidsByUid;

    for (const auto &filePath : taskFiles) {
        try {
            TaskMetadata metadata = parseTaskMetadata(filePath);
            metadataByPath[filePath.string()] = metadata;
            if (!metadata.uid.empty()) {
                metadataByUid[metadata.uid] = metadata;
            }
        }
        catch (const std::exception &ex) {
            std::cerr << "Skipping unreadable file " << filePath << ": " << ex.what() << "\n";
            ++skipped;
        }
    }

    for (const auto &[uid, metadata] : metadataByUid) {
        for (const auto &parentUid : metadata.parentUids) {
            if (!parentUid.empty()) {
                adjacentUidsByUid[uid].push_back(parentUid);
                adjacentUidsByUid[parentUid].push_back(uid);
            }
        }

        for (const auto &childUid : metadata.childUids) {
            if (!childUid.empty()) {
                adjacentUidsByUid[uid].push_back(childUid);
                adjacentUidsByUid[childUid].push_back(uid);
            }
        }
    }

    auto handleFile = [&](const fs::path &filePath) {
        auto metaIt = metadataByPath.find(filePath.string());
        if (metaIt == metadataByPath.end()) {
            return;
        }

        const TaskMetadata &metadata = metaIt->second;

        fs::path relative = fs::relative(filePath, cfg.sourceDir);

        if (!shouldMove(metadata.state, today, cfg.daysThreshold)) {
            return;
        }

        if (connectedTreeHasNotOldEnoughTask(metadata, adjacentUidsByUid, metadataByUid, today, cfg.daysThreshold)) {
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

    for (const auto &filePath : taskFiles) {
        handleFile(filePath);
    }

    std::cout << "\nScanned: " << scanned << " .ics files\n"
              << "Moved:   " << moved << "\n"
              << "Skipped: " << skipped << "\n";

    return 0;
}

int main(int argc, char *argv[]) {
    std::cout << "CalDAV Task Archiver" << std::endl;
    tzset();
    auto cfg = parseArgs(argc, argv);
    if (!cfg.has_value()) {
        printUsage(argv[0]);
        return 1;
    }

    try {
        return run(cfg.value());
    }
    catch (const std::exception &ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
