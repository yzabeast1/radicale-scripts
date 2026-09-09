#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
const std::string UNKNOWN_YEAR = "1900";

namespace {
    constexpr const char *CALENDAR_NAME = "Birthdays";
    enum class ConvertResult {
        Converted,
        SkippedNoEvents,
        Failed
    };

    struct RuntimeConfig {
        std::filesystem::path contacts_path;
        std::filesystem::path output_path;
        std::string calendar_name;
        bool verbose = false;
    };

    RuntimeConfig config;

    void print_usage(const char *exe_name) {
        std::cout << "Usage: " << exe_name << " --contacts-dir PATH --birthdays-dir PATH --calendar-name NAME [-v|--verbose]\n";
    }

    bool parse_args(int argc, char *argv[], RuntimeConfig &config) {
        if (argc <= 1) {
            std::cerr << "Error: no arguments provided.\n";
            print_usage(argv[0]);
            return false;
        }

        for (int index = 1; index < argc; ++index) {
            const std::string arg = argv[index];
            if ((arg == "-h") || (arg == "--help")) {
                print_usage(argv[0]);
                return false;
            }

            if ((arg == "--contacts-dir") && index + 1 < argc) {
                config.contacts_path = argv[++index];
                continue;
            }

            if ((arg == "--birthdays-dir") && index + 1 < argc) {
                config.output_path = argv[++index];
                continue;
            }

            if ((arg == "--calendar-name") && index + 1 < argc) {
                config.calendar_name = argv[++index];
                continue;
            }

            if ((arg == "-v") || (arg == "--verbose")) {
                config.verbose = true;
                continue;
            }

            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            print_usage(argv[0]);
            return false;
        }

        if (config.contacts_path.empty() || config.output_path.empty() || config.calendar_name.empty()) {
            std::cerr << "Error: contacts directory, and birthdays directory are required.\n";
            print_usage(argv[0]);
            return false;
        }

        return true;
    }

    std::string read_file(const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::in | std::ios::binary);
        if (!input) {
            return {};
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    std::vector<std::string> split_vcards(const std::string &content) {
        std::vector<std::string> cards;
        std::size_t start = 0;
        constexpr const char *separator = "END:VCARD";
        while (true) {
            const auto end = content.find(separator, start);
            if (end == std::string::npos) {
                break;
            }
            cards.emplace_back(content.substr(start, end - start));
            start = end + std::string(separator).size();
        }
        return cards;
    }

    int hex_value(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'A' && ch <= 'F') {
            return 10 + (ch - 'A');
        }
        if (ch >= 'a' && ch <= 'f') {
            return 10 + (ch - 'a');
        }
        return -1;
    }

    std::string decode_quoted_printable(const std::string &text) {
        std::string out;
        out.reserve(text.size());

        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '=') {
                if (i + 1 < text.size() && text[i + 1] == '\r' && i + 2 < text.size() && text[i + 2] == '\n') {
                    i += 2;
                    continue;
                }
                if (i + 2 < text.size() && text[i + 1] == '\n') {
                    i += 1;
                    continue;
                }
                if (i + 2 < text.size()) {
                    const int high = hex_value(text[i + 1]);
                    const int low = hex_value(text[i + 2]);
                    if (high >= 0 && low >= 0) {
                        out.push_back(static_cast<char>((high << 4) | low));
                        i += 2;
                        continue;
                    }
                }
            }
            out.push_back(text[i]);
        }

        return out;
    }

    std::string hash_hex_fnv1a_64(const std::string &text) {
        constexpr std::uint64_t offset_basis = 1469598103934665603ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;

        std::uint64_t hash = offset_basis;
        for (unsigned char ch : text) {
            hash ^= static_cast<std::uint64_t>(ch);
            hash *= prime;
        }

        std::ostringstream stream;
        stream << std::hex << hash;
        return stream.str();
    }

    std::string normalized_date(const std::smatch &match) {
        if (match[1].str() == "1604" || match[1].str() == "0001" || match[1].str() == "-") {
            return UNKNOWN_YEAR + match[2].str() + match[3].str();
        }
        return match[1].str() + match[2].str() + match[3].str();
    }

    std::string normalized_date(const std::smatch &match, std::size_t year_index, std::size_t month_index, std::size_t day_index) {
        const std::string year = match[year_index].str();
        if (year == "1604" || year == "0001" || year == "-") {
            return UNKNOWN_YEAR + match[month_index].str() + match[day_index].str();
        }
        return year + match[month_index].str() + match[day_index].str();
    }

    std::string make_event(const std::string &date, const std::string &summary, const std::string &uid_seed) {
        const std::string uid = hash_hex_fnv1a_64(uid_seed) + "@VCFtoICS.com";
        std::ostringstream event;
        event << "BEGIN:VEVENT\n"
              << "DTSTART:" << date << "\n"
              << "SUMMARY:" << summary << "\n"
              << "RRULE:FREQ=YEARLY\n"
              << "DURATION:P1D\n"
              << "TRANSP:TRANSPARENT\n"
              << "UID:" << uid << "\n"
              << "END:VEVENT\n";
        return event.str();
    }

    std::vector<std::string> extract_ios_anniversaries(const std::string &card) {
        std::vector<std::string> anniversaries;
        const std::regex labeled_date_re(R"((item\d+)\.X-ABDATE(?:;VALUE=DATE)?:(-|\d{4})-?(\d{2})-?(\d{2}))", std::regex_constants::icase);
        const std::regex label_re(R"((item\d+)\.X-ABLABEL:(.*))", std::regex_constants::icase);
        const std::regex generic_date_re(R"(X-ABDATE(?:;VALUE=DATE)?:(-|\d{4})-?(\d{2})-?(\d{2}))", std::regex_constants::icase);
        const std::regex generic_label_re(R"(X-ABLABEL:(.*))", std::regex_constants::icase);
        const std::regex anniversary_word_re(R"(anniversary)", std::regex_constants::icase);

        std::unordered_map<std::string, std::string> item_to_date;

        for (std::sregex_iterator it(card.begin(), card.end(), labeled_date_re), end; it != end; ++it) {
            item_to_date[(*it)[1].str()] = normalized_date(*it, 2, 3, 4);
        }

        for (std::sregex_iterator it(card.begin(), card.end(), label_re), end; it != end; ++it) {
            const std::string item = (*it)[1].str();
            const std::string label = decode_quoted_printable((*it)[2].str());
            if (!std::regex_search(label, anniversary_word_re)) {
                continue;
            }

            auto date_it = item_to_date.find(item);
            if (date_it != item_to_date.end()) {
                anniversaries.push_back(date_it->second);
            }
        }

        if (!anniversaries.empty()) {
            return anniversaries;
        }

        if (std::regex_search(card, generic_label_re) && std::regex_search(card, anniversary_word_re)) {
            std::smatch generic_date_match;
            if (std::regex_search(card, generic_date_match, generic_date_re)) {
                anniversaries.push_back(normalized_date(generic_date_match));
            }
        }

        return anniversaries;
    }

    std::string extract_vcard_uid(const std::string &card) {
        const std::regex uid_re(R"(UID(?::|;[^:\r\n]*:)([^\r\n]*))", std::regex_constants::icase);
        std::smatch uid_match;
        if (!std::regex_search(card, uid_match, uid_re)) {
            return {};
        }
        return decode_quoted_printable(uid_match[1].str());
    }

    ConvertResult convert_one_file(const std::filesystem::path &input_path, const std::filesystem::path &output_dir, const std::string &calendar_name) {
        const std::string file_content = read_file(input_path);
        if (file_content.empty() && std::filesystem::file_size(input_path) > 0) {
            std::cerr << "Failed to read input file : " << input_path << "\n";
            return ConvertResult::Failed;
        }

        const std::regex birthday_re(R"(BDAY(?:;[^:\r\n]*)?:(-|\d{4})-?(\d{2})-?(\d{2}))", std::regex_constants::icase);
        const std::regex anniversary_re(R"((?:ANNIVERSARY|X-ANNIVERSARY)(?:;VALUE=DATE)?:(-|\d{4})-?(\d{2})-?(\d{2}))", std::regex_constants::icase);
        const std::regex name_re(R"(FN(?::|;[^:\r\n]*:)([^\r\n]*))", std::regex_constants::icase);

        const auto cards = split_vcards(file_content);
        const std::string source_name = input_path.filename().string();
        int birthday_count = 0;
        int anniversary_count = 0;
        std::vector<std::string> birthday_events;
        std::vector<std::string> anniversary_events;
        std::unordered_set<std::string> anniversary_keys;

        for (std::size_t card_index = 0; card_index < cards.size(); ++card_index) {
            const auto &card = cards[card_index];
            std::smatch name_match;

            if (!std::regex_search(card, name_match, name_re)) {
                continue;
            }

            std::string name = decode_quoted_printable(name_match[1].str());
            const std::string vcard_uid = extract_vcard_uid(card);
            const std::string card_identity = vcard_uid.empty() ? ("index:" + std::to_string(card_index) + "|name:" + name) : ("uid:" + vcard_uid);

            std::smatch birthday_match;
            if (std::regex_search(card, birthday_match, birthday_re)) {
                const std::string birthday = normalized_date(birthday_match);
                if (config.verbose) {
                    std::cout << "Found birthday in vCard: " << name << " on " << birthday << "\n";
                }
                const std::string summary = name + "'s Birthday";
                const std::string uid_seed = "birthday|" + source_name + "|" + card_identity + "|" + birthday;
                birthday_events.push_back(make_event(birthday, summary, uid_seed));
                ++birthday_count;
            }

            std::smatch anniversary_match;
            if (std::regex_search(card, anniversary_match, anniversary_re)) {
                const std::string anniversary = normalized_date(anniversary_match);
                const std::string key = name + "|" + anniversary;
                if (anniversary_keys.insert(key).second) {
                    if (config.verbose) {
                        std::cout << "Found anniversary in vCard: " << name << " on " << anniversary << "\n";
                    }
                    const std::string summary = name + "'s Anniversary";
                    const std::string uid_seed = "anniversary|" + source_name + "|" + card_identity + "|" + anniversary;
                    anniversary_events.push_back(make_event(anniversary, summary, uid_seed));
                    ++anniversary_count;
                }
            }

            const auto ios_anniversaries = extract_ios_anniversaries(card);
            for (const auto &anniversary : ios_anniversaries) {
                const std::string key = name + "|" + anniversary;
                if (anniversary_keys.insert(key).second) {
                    if (config.verbose) {
                        std::cout << "Found anniversary in vCard: " << name << " on " << anniversary << "\n";
                    }
                    const std::string summary = name + "'s Anniversary";
                    const std::string uid_seed = "anniversary|" + source_name + "|" + card_identity + "|" + anniversary;
                    anniversary_events.push_back(make_event(anniversary, summary, uid_seed));
                    ++anniversary_count;
                }
            }
        }

        if (birthday_count == 0 && anniversary_count == 0) {
            return ConvertResult::SkippedNoEvents;
        }

        auto write_calendar = [&](const std::filesystem::path &output_path, const std::vector<std::string> &events) -> bool {
            std::ofstream output(output_path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output) {
                std::cerr << "Invalid output file path : " << output_path << "\n";
                return false;
            }

            output << "BEGIN:VCALENDAR\n"
                   << "PRODID:-//VCF to ICS//NONSGML " << calendar_name << " V1.0//EN\n"
                   << "X-WR-CALNAME:" << calendar_name << "\n"
                   << "VERSION:2.0\n";

            for (const auto &event : events) {
                output << event;
            }

            output << "END:VCALENDAR";
            return true;
        };

        const std::string base_name = input_path.stem().string();

        if (birthday_count > 0) {
            const std::filesystem::path birthdays_output = output_dir / (base_name + "_birthdays.ics");
            if (!write_calendar(birthdays_output, birthday_events)) {
                return ConvertResult::Failed;
            }
        }

        if (anniversary_count > 0) {
            const std::filesystem::path anniversaries_output = output_dir / (base_name + "_anniversaries.ics");
            if (!write_calendar(anniversaries_output, anniversary_events)) {
                return ConvertResult::Failed;
            }
        }

        return ConvertResult::Converted;
    }

} // namespace

int main(int argc, char *argv[]) {
    std::cout << "VCF to ICS Converter" << std::endl;
    config.calendar_name = CALENDAR_NAME;
    if (!parse_args(argc, argv, config)) {
        return 1;
    }

    const std::filesystem::path &contacts_path = config.contacts_path;
    const std::filesystem::path &birthdays_path = config.output_path;

    if (!std::filesystem::exists(contacts_path) || !std::filesystem::is_directory(contacts_path)) {
        std::cerr << "Contacts directory is invalid: " << contacts_path << "\n";
        return 1;
    }

    if (!std::filesystem::exists(birthdays_path) || !std::filesystem::is_directory(birthdays_path)) {
        std::cerr << "Birthdays directory is invalid: " << birthdays_path << "\n";
        return 1;
    }

    for (const auto &entry : std::filesystem::directory_iterator(birthdays_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ics") {
            std::error_code ec;
            std::filesystem::remove(entry.path(), ec);
            if (ec) {
                std::cerr << "Failed to remove " << entry.path() << ": " << ec.message() << "\n";
            }
            else if (config.verbose) {
                std::cout << "Removed old file: " << entry.path() << "\n";
            }
        }
    }

    int converted_count = 0;
    int skipped_count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(contacts_path)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".vcf") {
            continue;
        }

        const std::filesystem::path input_file = entry.path();
        if (config.verbose) {
            std::cout << "Processing: " << input_file << "\n";
        }

        const ConvertResult result = convert_one_file(input_file, birthdays_path, config.calendar_name);
        if (result == ConvertResult::Failed) {
            std::cerr << "Conversion failed for " << input_file << "\n";
            continue;
        }

        if (result == ConvertResult::SkippedNoEvents) {
            ++skipped_count;
            if (config.verbose) {
                std::cout << "Skipped (no birthdays or anniversaries): " << input_file << "\n";
            }
            continue;
        }

        ++converted_count;
        if (config.verbose) {
            std::cout << "Converted: " << input_file << "\n";
        }
    }

    std::cout << "Converted " << converted_count << " file(s), skipped " << skipped_count << " file(s) without birthdays or anniversaries.\n";
    return 0;
}
