#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct PropertyLocation {
    size_t index = 0;
    std::string keyUpper;
    std::string value;
};

std::string toUpperCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

bool iequals(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }

    return true;
}

std::string trim(const std::string &text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(start, end - start);
}

std::string digitsOnly(const std::string &text) {
    std::string result;
    result.reserve(text.size());

    for (unsigned char ch : text) {
        if (std::isdigit(ch) != 0) {
            result.push_back(static_cast<char>(ch));
        }
    }

    return result;
}

std::optional<std::string> normalizeAnniversaryValue(const std::string &value) {
    std::string digits = digitsOnly(trim(value));
    if (digits.size() != 8) {
        return std::nullopt;
    }

    return digits.substr(0, 4) + "-" + digits.substr(4, 2) + "-" + digits.substr(6, 2);
}

std::optional<std::string> normalizeItem1DateValue(const std::string &value) {
    std::string digits = digitsOnly(trim(value));
    if (digits.size() != 8) {
        return std::nullopt;
    }

    return digits.substr(0, 4) + "-" + digits.substr(4, 2) + "-" + digits.substr(6, 2);
}

std::vector<std::string> readLines(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }

    return lines;
}

std::optional<PropertyLocation> findProperty(const std::vector<std::string> &lines, const std::string &propertyNameUpper) {
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string &line = lines[i];
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, colonPos);
        std::string keyUpper = toUpperCopy(key);

        if (keyUpper == propertyNameUpper) {
            PropertyLocation found;
            found.index = i;
            found.keyUpper = keyUpper;
            found.value = trim(line.substr(colonPos + 1));
            return found;
        }

        if (keyUpper.rfind(propertyNameUpper + ";", 0) == 0) {
            PropertyLocation found;
            found.index = i;
            found.keyUpper = keyUpper;
            found.value = trim(line.substr(colonPos + 1));
            return found;
        }
    }

    return std::nullopt;
}

bool hasVcfExtension(const fs::path &path) {
    if (!path.has_extension()) {
        return false;
    }

    std::string extensionUpper = toUpperCopy(path.extension().string());
    return extensionUpper == ".VCF";
}

size_t insertionIndexBeforeEndVcard(const std::vector<std::string> &lines) {
    for (size_t i = lines.size(); i > 0; --i) {
        if (iequals(trim(lines[i - 1]), "END:VCARD")) {
            return i - 1;
        }
    }

    return lines.size();
}

void insertBeforeEndVcard(std::vector<std::string> &lines, const std::string &newLine) {
    size_t insertAt = insertionIndexBeforeEndVcard(lines);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), newLine);
}

bool rewriteFileIfChanged(const fs::path &path, std::vector<std::string> &lines) {
    auto anniversary = findProperty(lines, "ANNIVERSARY");
    auto itemDate = findProperty(lines, "ITEM1.X-ABDATE");

    const bool hasAnniversary = anniversary.has_value();
    const bool hasItemDate = itemDate.has_value();

    if (!hasAnniversary && !hasItemDate) {
        return false;
    }

    bool changed = false;

    if (hasAnniversary && !hasItemDate) {
        auto item1Value = normalizeItem1DateValue(anniversary->value);
        if (!item1Value.has_value()) {
            return false;
        }

        insertBeforeEndVcard(lines, "item1.X-ABDATE:" + *item1Value);
        insertBeforeEndVcard(lines, "item1.X-ABLABEL:_$!<Anniversary>!$_");

        auto normalizedAnniversary = normalizeAnniversaryValue(anniversary->value);
        if (normalizedAnniversary.has_value() && anniversary->value != *normalizedAnniversary) {
            lines[anniversary->index] = "ANNIVERSARY:" + *normalizedAnniversary;
        }

        changed = true;
    }
    else if (!hasAnniversary && hasItemDate) {
        auto anniversaryValue = normalizeAnniversaryValue(itemDate->value);
        if (!anniversaryValue.has_value()) {
            return false;
        }

        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertionIndexBeforeEndVcard(lines)), "ANNIVERSARY:" + *anniversaryValue);
        changed = true;
    }
    else if (hasAnniversary && hasItemDate) {
        auto anniversaryValue = normalizeAnniversaryValue(anniversary->value);
        auto item1AnniversaryValue = normalizeAnniversaryValue(itemDate->value);
        auto item1Value = normalizeItem1DateValue(itemDate->value);

        if (!item1AnniversaryValue.has_value() || !item1Value.has_value()) {
            return false;
        }

        if (!anniversaryValue.has_value() || *anniversaryValue != *item1AnniversaryValue) {
            lines[anniversary->index] = "ANNIVERSARY:" + *item1AnniversaryValue;
            changed = true;
        }

        if (itemDate->value != *item1Value) {
            lines[itemDate->index] = "item1.X-ABDATE:" + *item1Value;
            changed = true;
        }
    }

    if (!changed) {
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not write file: " + path.string());
    }

    for (const auto &line : lines) {
        output << line << '\n';
    }

    return true;
}

void printUsage(const std::string &exeName) {
    std::cout << "Usage: " << exeName << " --contacts-folder <contacts_folder>\n";
}

int main(int argc, char *argv[]) {
    std::cout << "Anniversary Fixer" << "\n";

    std::optional<fs::path> contactsFolder;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }

        if (arg == "--contacts-folder" && i + 1 < argc) {
            contactsFolder = fs::path(argv[++i]);
            continue;
        }

        std::cerr << "Unknown or incomplete argument: " << arg << "\n";
        printUsage(argv[0]);
        return 1;
    }

    if (!contactsFolder.has_value()) {
        printUsage(argv[0]);
        return 1;
    }

    if (!fs::exists(*contactsFolder) || !fs::is_directory(*contactsFolder)) {
        std::cerr << "Not a directory: " << *contactsFolder << "\n";
        return 1;
    }

    size_t inspected = 0;
    size_t changed = 0;

    try {
        for (const auto &entry : fs::directory_iterator(*contactsFolder)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const fs::path &path = entry.path();
            if (!hasVcfExtension(path)) {
                continue;
            }

            ++inspected;
            std::vector<std::string> lines = readLines(path);
            if (rewriteFileIfChanged(path, lines)) {
                ++changed;
                std::cout << "Updated: " << path.filename().string() << "\n";
            }
        }
    }
    catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "Inspected " << inspected << " contact file(s), changed " << changed << ".\n";
    return 0;
}
