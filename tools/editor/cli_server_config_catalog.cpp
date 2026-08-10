#include "cli_server_config_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_server_config.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* configKindName(uint8_t k) {
    using C = wowee::pipeline::WoweeServerConfig;
    switch (k) {
        case C::XPRate:      return "xprate";
        case C::DropRate:    return "droprate";
        case C::HonorRate:   return "honorrate";
        case C::RestedXP:    return "restedxp";
        case C::RealmType:   return "realmtype";
        case C::WorldFlag:   return "worldflag";
        case C::Performance: return "performance";
        case C::Security:    return "security";
        case C::Misc:        return "misc";
        default:             return "unknown";
    }
}

const char* valueKindName(uint8_t k) {
    using C = wowee::pipeline::WoweeServerConfig;
    switch (k) {
        case C::Float:  return "float";
        case C::Int:    return "int";
        case C::Bool:   return "bool";
        case C::String: return "string";
        default:        return "unknown";
    }
}

// Render the active value field (per valueKind) as a
// terse human-readable string. Used by the info display
// to show only the meaningful field per entry.
std::string activeValueString(
    const wowee::pipeline::WoweeServerConfig::Entry& e) {
    using C = wowee::pipeline::WoweeServerConfig;
    char buf[64];
    switch (e.valueKind) {
        case C::Float:
            std::snprintf(buf, sizeof(buf), "%.4f",
                          e.floatValue);
            return buf;
        case C::Int:
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(e.intValue));
            return buf;
        case C::Bool:
            return e.intValue != 0 ? "true" : "false";
        case C::String:
            return "\"" + e.strValue + "\"";
        default:
            return "?";
    }
}

bool saveOrError(const wowee::pipeline::WoweeServerConfig& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeServerConfigLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wcfg\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeServerConfig& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcfg\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  configs : %zu\n", c.entries.size());
}

int handleGenRates(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RateMultipliers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcfg");
    auto c = wowee::pipeline::WoweeServerConfigLoader::makeRates(name);
    if (!saveOrError(c, base, "gen-cfg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPerf(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PerformanceTuning";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcfg");
    auto c = wowee::pipeline::WoweeServerConfigLoader::makePerformance(name);
    if (!saveOrError(c, base, "gen-cfg-perf")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenSecurity(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SecurityPolicy";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcfg");
    auto c = wowee::pipeline::WoweeServerConfigLoader::makeSecurity(name);
    if (!saveOrError(c, base, "gen-cfg-sec")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcfg");
    if (!wowee::pipeline::WoweeServerConfigLoader::exists(base)) {
        std::fprintf(stderr, "WCFG not found: %s.wcfg\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeServerConfigLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcfg"] = base + ".wcfg";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"configId", e.configId},
                {"name", e.name},
                {"description", e.description},
                {"configKind", e.configKind},
                {"configKindName", configKindName(e.configKind)},
                {"valueKind", e.valueKind},
                {"valueKindName", valueKindName(e.valueKind)},
                {"restartRequired", e.restartRequired != 0},
                {"floatValue", e.floatValue},
                {"intValue", e.intValue},
                {"strValue", e.strValue},
                {"activeValue", activeValueString(e)},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCFG: %s.wcfg\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  configs : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind          valueKind  restart  value                 name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-11s   %-8s   %s        %-20s   %s\n",
                    e.configId, configKindName(e.configKind),
                    valueKindName(e.valueKind),
                    e.restartRequired ? "Y" : "n",
                    activeValueString(e).c_str(),
                    e.name.c_str());
    }
    return 0;
}

int parseConfigKindToken(const std::string& s) {
    using C = wowee::pipeline::WoweeServerConfig;
    if (s == "xprate")      return C::XPRate;
    if (s == "droprate")    return C::DropRate;
    if (s == "honorrate")   return C::HonorRate;
    if (s == "restedxp")    return C::RestedXP;
    if (s == "realmtype")   return C::RealmType;
    if (s == "worldflag")   return C::WorldFlag;
    if (s == "performance") return C::Performance;
    if (s == "security")    return C::Security;
    if (s == "misc")        return C::Misc;
    return -1;
}

int parseValueKindToken(const std::string& s) {
    using C = wowee::pipeline::WoweeServerConfig;
    if (s == "float")  return C::Float;
    if (s == "int")    return C::Int;
    if (s == "bool")   return C::Bool;
    if (s == "string") return C::String;
    return -1;
}

template <typename ParseFn>
bool readEnumField(const nlohmann::json& je,
                    const char* intKey,
                    const char* nameKey,
                    ParseFn parseFn,
                    const char* label,
                    uint32_t entryId,
                    uint8_t& outValue) {
    if (je.contains(intKey)) {
        const auto& v = je[intKey];
        if (v.is_string()) {
            int parsed = parseFn(v.get<std::string>());
            if (parsed < 0) {
                std::fprintf(stderr,
                    "import-wcfg-json: unknown %s token "
                    "'%s' on entry id=%u\n",
                    label, v.get<std::string>().c_str(),
                    entryId);
                return false;
            }
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
        if (v.is_number_integer()) {
            outValue = static_cast<uint8_t>(v.get<int>());
            return true;
        }
    }
    if (je.contains(nameKey) && je[nameKey].is_string()) {
        int parsed = parseFn(je[nameKey].get<std::string>());
        if (parsed >= 0) {
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
    }
    return true;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wcfg");
    if (out.empty()) out = base + ".wcfg.json";
    if (!wowee::pipeline::WoweeServerConfigLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wcfg-json: WCFG not found: %s.wcfg\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeServerConfigLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WCFG";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"configId", e.configId},
            {"name", e.name},
            {"description", e.description},
            {"configKind", e.configKind},
            {"configKindName", configKindName(e.configKind)},
            {"valueKind", e.valueKind},
            {"valueKindName", valueKindName(e.valueKind)},
            {"restartRequired", e.restartRequired != 0},
            {"floatValue", e.floatValue},
            {"intValue", e.intValue},
            {"strValue", e.strValue},
            {"activeValue", activeValueString(e)},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wcfg-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu configs)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wcfg");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wcfg-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcfg-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeServerConfig c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wcfg-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeServerConfig::Entry e;
        e.configId = je.value("configId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (!readEnumField(je, "configKind", "configKindName",
                            parseConfigKindToken, "configKind",
                            e.configId, e.configKind)) return 1;
        if (!readEnumField(je, "valueKind", "valueKindName",
                            parseValueKindToken, "valueKind",
                            e.configId, e.valueKind)) return 1;
        if (je.contains("restartRequired")) {
            const auto& v = je["restartRequired"];
            if (v.is_boolean())
                e.restartRequired = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.restartRequired = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        e.floatValue = je.value("floatValue", 0.0f);
        e.intValue = je.value("intValue", int64_t{0});
        e.strValue = je.value("strValue", std::string{});
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeServerConfigLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcfg-json: failed to save %s.wcfg\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcfg (%zu configs)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcfg");
    if (!wowee::pipeline::WoweeServerConfigLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wcfg: WCFG not found: %s.wcfg\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeServerConfigLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    std::set<std::string> namesSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.configId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.configId == 0)
            errors.push_back(ctx + ": configId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.configKind > 7 && e.configKind != 255) {
            errors.push_back(ctx + ": configKind " +
                std::to_string(e.configKind) +
                " out of range (must be 0..7 or 255 Misc)");
        }
        if (e.valueKind > 3) {
            errors.push_back(ctx + ": valueKind " +
                std::to_string(e.valueKind) +
                " out of range (must be 0..3)");
        }
        // Per-valueKind validity: each kind requires its
        // matching field to carry meaningful data, others
        // should be unset (server ignores them).
        using C = wowee::pipeline::WoweeServerConfig;
        if (e.valueKind == C::Float &&
            e.floatValue == 0.0f && e.intValue != 0) {
            warnings.push_back(ctx +
                ": valueKind=Float but intValue is "
                "non-zero — intValue will be ignored "
                "by the server but stored on disk; "
                "consider clearing for cleaner serialization");
        }
        if (e.valueKind == C::Int && e.floatValue != 0.0f) {
            warnings.push_back(ctx +
                ": valueKind=Int but floatValue is "
                "non-zero — floatValue will be ignored "
                "by the server");
        }
        if (e.valueKind == C::Bool && e.intValue != 0 &&
            e.intValue != 1) {
            errors.push_back(ctx +
                ": valueKind=Bool but intValue is " +
                std::to_string(e.intValue) +
                " (must be 0 or 1)");
        }
        if (e.valueKind == C::String && e.strValue.empty()) {
            warnings.push_back(ctx +
                ": valueKind=String but strValue is "
                "empty — config would default to empty "
                "string");
        }
        // Names should be unique within a catalog (the
        // server uses name as the primary lookup key for
        // config-by-name access patterns common in
        // legacy SQL-driven server configs).
        if (!e.name.empty() &&
            !namesSeen.insert(e.name).second) {
            errors.push_back(ctx +
                ": duplicate config name '" + e.name +
                "' — server name-based lookups would "
                "be ambiguous");
        }
        if (!idsSeen.insert(e.configId).second) {
            errors.push_back(ctx + ": duplicate configId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wcfg", base, errors, warnings);
    std::printf("validate-wcfg: %s.wcfg\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu configs, all configIds + "
                    "names unique, per-kind value semantics "
                    "satisfied\n", c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleServerConfigCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-cfg") == 0 && i + 1 < argc) {
        outRc = handleGenRates(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cfg-perf") == 0 && i + 1 < argc) {
        outRc = handleGenPerf(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cfg-sec") == 0 && i + 1 < argc) {
        outRc = handleGenSecurity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcfg") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcfg") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcfg-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcfg-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
