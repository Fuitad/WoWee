#include "cli_spell_procs_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_procs.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {


void printGenSummary(const wowee::pipeline::WoweeSpellProc& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsps\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  procs   : %zu\n", c.entries.size());
}

int handleGenWeapon(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WeaponProcs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsps");
    auto c = wowee::pipeline::WoweeSpellProcLoader::makeWeapon(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellProcLoader>(c, base, "gen-sps", ".wsps")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAura(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AuraProcs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsps");
    auto c = wowee::pipeline::WoweeSpellProcLoader::makeAura(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellProcLoader>(c, base, "gen-sps-aura", ".wsps")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTalent(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TalentProcs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsps");
    auto c = wowee::pipeline::WoweeSpellProcLoader::makeTalent(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellProcLoader>(c, base, "gen-sps-talent", ".wsps")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendProcFlagNames(uint32_t flags, std::string& out) {
    using F = wowee::pipeline::WoweeSpellProc;
    auto add = [&](const char* n) {
        if (!out.empty()) out += "|";
        out += n;
    };
    if (flags & F::DealtMeleeAutoAttack)  add("DealtMeleeAutoAttack");
    if (flags & F::DealtMeleeSpell)       add("DealtMeleeSpell");
    if (flags & F::TakenMeleeAutoAttack)  add("TakenMeleeAutoAttack");
    if (flags & F::TakenMeleeSpell)       add("TakenMeleeSpell");
    if (flags & F::DealtRangedAutoAttack) add("DealtRangedAutoAttack");
    if (flags & F::DealtRangedSpell)      add("DealtRangedSpell");
    if (flags & F::DealtSpell)            add("DealtSpell");
    if (flags & F::DealtSpellHeal)        add("DealtSpellHeal");
    if (flags & F::TakenSpell)            add("TakenSpell");
    if (flags & F::OnKill)                add("OnKill");
    if (flags & F::OnDeath)               add("OnDeath");
    if (flags & F::OnCastFinished)        add("OnCastFinished");
    if (flags & F::Critical)              add("Critical");
    if (out.empty()) out = "-";
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsps");
    if (!wowee::pipeline::WoweeSpellProcLoader::exists(base)) {
        std::fprintf(stderr, "WSPS not found: %s.wsps\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellProcLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsps"] = base + ".wsps";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string flagNames;
            appendProcFlagNames(e.procFlags, flagNames);
            arr.push_back({
                {"procId", e.procId},
                {"name", e.name},
                {"description", e.description},
                {"triggerSpellId", e.triggerSpellId},
                {"procFromSpellId", e.procFromSpellId},
                {"procChance", e.procChance},
                {"procPpm", e.procPpm},
                {"procFlags", e.procFlags},
                {"procFlagsLabels", flagNames},
                {"internalCooldownMs", e.internalCooldownMs},
                {"charges", e.charges},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSPS: %s.wsps\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  procs   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   trigger  fromSpell  chance%%  PPM     ICDms   chg   flags\n");
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendProcFlagNames(e.procFlags, flagNames);
        std::printf("  %4u   %5u    %5u     %5.1f   %5.1f   %5u   %3u   %s\n",
                    e.procId, e.triggerSpellId, e.procFromSpellId,
                    e.procChance * 100.0f, e.procPpm,
                    e.internalCooldownMs, e.charges,
                    flagNames.c_str());
        std::printf("                                                                 %s\n",
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wsps");
    if (!wowee::pipeline::WoweeSpellProcLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wsps-json: WSPS not found: %s.wsps\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellProcLoader::load(base);
    if (outPath.empty()) outPath = base + ".wsps.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendProcFlagNames(e.procFlags, flagNames);
        nlohmann::json je;
        je["procId"] = e.procId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["triggerSpellId"] = e.triggerSpellId;
        je["procFromSpellId"] = e.procFromSpellId;
        je["procChance"] = e.procChance;
        je["procPpm"] = e.procPpm;
        je["procFlags"] = e.procFlags;
        je["procFlagsLabels"] = flagNames;
        je["internalCooldownMs"] = e.internalCooldownMs;
        je["charges"] = e.charges;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wsps-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  procs   : %zu\n", c.entries.size());
    return 0;
}

uint32_t parseProcFlagsField(const nlohmann::json& jv) {
    using F = wowee::pipeline::WoweeSpellProc;
    if (jv.is_number_integer() || jv.is_number_unsigned())
        return jv.get<uint32_t>();
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        uint32_t out = 0;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find('|', pos);
            if (end == std::string::npos) end = s.size();
            std::string tok = s.substr(pos, end - pos);
            for (auto& ch : tok) ch = static_cast<char>(std::tolower(ch));
            if (tok == "dealtmeleeautoattack")  out |= F::DealtMeleeAutoAttack;
            else if (tok == "dealtmeleespell")  out |= F::DealtMeleeSpell;
            else if (tok == "takenmeleeautoattack") out |= F::TakenMeleeAutoAttack;
            else if (tok == "takenmeleespell")  out |= F::TakenMeleeSpell;
            else if (tok == "dealtrangedautoattack") out |= F::DealtRangedAutoAttack;
            else if (tok == "dealtrangedspell") out |= F::DealtRangedSpell;
            else if (tok == "dealtspell")       out |= F::DealtSpell;
            else if (tok == "dealtspellheal")   out |= F::DealtSpellHeal;
            else if (tok == "takenspell")       out |= F::TakenSpell;
            else if (tok == "onkill")           out |= F::OnKill;
            else if (tok == "ondeath")          out |= F::OnDeath;
            else if (tok == "oncastfinished")   out |= F::OnCastFinished;
            else if (tok == "critical")         out |= F::Critical;
            pos = end + 1;
        }
        return out;
    }
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wsps-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wsps-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellProc c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellProc::Entry e;
            if (je.contains("procId"))             e.procId = je["procId"].get<uint32_t>();
            if (je.contains("name"))               e.name = je["name"].get<std::string>();
            if (je.contains("description"))        e.description = je["description"].get<std::string>();
            if (je.contains("triggerSpellId"))     e.triggerSpellId = je["triggerSpellId"].get<uint32_t>();
            if (je.contains("procFromSpellId"))    e.procFromSpellId = je["procFromSpellId"].get<uint32_t>();
            if (je.contains("procChance"))         e.procChance = je["procChance"].get<float>();
            if (je.contains("procPpm"))            e.procPpm = je["procPpm"].get<float>();
            if (je.contains("procFlags"))
                e.procFlags = parseProcFlagsField(je["procFlags"]);
            else if (je.contains("procFlagsLabels"))
                e.procFlags = parseProcFlagsField(je["procFlagsLabels"]);
            if (je.contains("internalCooldownMs")) e.internalCooldownMs = je["internalCooldownMs"].get<uint32_t>();
            if (je.contains("charges"))            e.charges = je["charges"].get<uint8_t>();
            if (je.contains("iconColorRGBA"))      e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wsps");
    outBase = cli::withoutExt(outBase, ".wsps");
    if (!wowee::pipeline::WoweeSpellProcLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsps-json: failed to save %s.wsps\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsps\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  procs   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsps");
    if (!wowee::pipeline::WoweeSpellProcLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wsps: WSPS not found: %s.wsps\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellProcLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    constexpr uint32_t kKnownFlagMask =
        wowee::pipeline::WoweeSpellProc::DealtMeleeAutoAttack |
        wowee::pipeline::WoweeSpellProc::DealtMeleeSpell |
        wowee::pipeline::WoweeSpellProc::TakenMeleeAutoAttack |
        wowee::pipeline::WoweeSpellProc::TakenMeleeSpell |
        wowee::pipeline::WoweeSpellProc::DealtRangedAutoAttack |
        wowee::pipeline::WoweeSpellProc::DealtRangedSpell |
        wowee::pipeline::WoweeSpellProc::DealtSpell |
        wowee::pipeline::WoweeSpellProc::DealtSpellHeal |
        wowee::pipeline::WoweeSpellProc::TakenSpell |
        wowee::pipeline::WoweeSpellProc::OnKill |
        wowee::pipeline::WoweeSpellProc::OnDeath |
        wowee::pipeline::WoweeSpellProc::OnCastFinished |
        wowee::pipeline::WoweeSpellProc::Critical;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.procId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.procId == 0)
            errors.push_back(ctx + ": procId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.triggerSpellId == 0)
            errors.push_back(ctx +
                ": triggerSpellId is 0 — proc will fire nothing");
        if (e.procFlags == 0)
            errors.push_back(ctx +
                ": procFlags is 0 — proc will never trigger "
                "(no qualifying event configured)");
        if (e.procFlags & ~kKnownFlagMask) {
            warnings.push_back(ctx +
                ": procFlags has bits outside known mask " +
                "(0x" + std::to_string(e.procFlags & ~kKnownFlagMask) +
                ") — engine will ignore unknown flags");
        }
        if (e.procChance < 0.0f || e.procChance > 1.0f) {
            warnings.push_back(ctx +
                ": procChance " + std::to_string(e.procChance) +
                " is outside [0..1]; engine clamps but author "
                "should double-check");
        }
        if (e.procPpm < 0.0f) {
            errors.push_back(ctx +
                ": procPpm < 0 — invalid procs-per-minute rate");
        }
        // Both procChance and procPpm set is contradictory —
        // engine prefers procPpm when non-zero so procChance
        // is ignored.
        if (e.procChance > 0.0f && e.procPpm > 0.0f) {
            warnings.push_back(ctx +
                ": both procChance (" + std::to_string(e.procChance) +
                ") and procPpm (" + std::to_string(e.procPpm) +
                ") set — engine uses procPpm and ignores "
                "procChance");
        }
        // No chance configured at all = proc never fires.
        if (e.procChance == 0.0f && e.procPpm == 0.0f) {
            warnings.push_back(ctx +
                ": both procChance=0 and procPpm=0 — proc " +
                "will never trigger");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.procId) {
                errors.push_back(ctx + ": duplicate procId");
                break;
            }
        }
        idsSeen.push_back(e.procId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wsps", base, errors, warnings);
    std::printf("validate-wsps: %s.wsps\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu procs, all procIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellProcsCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-sps") == 0 && i + 1 < argc) {
        outRc = handleGenWeapon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sps-aura") == 0 && i + 1 < argc) {
        outRc = handleGenAura(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sps-talent") == 0 && i + 1 < argc) {
        outRc = handleGenTalent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsps") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsps") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsps-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsps-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
