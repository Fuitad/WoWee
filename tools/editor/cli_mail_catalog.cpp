#include "cli_mail_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_mail.hpp"
#include <nlohmann/json.hpp>

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


void printGenSummary(const wowee::pipeline::WoweeMail& c,
                     const std::string& base) {
    std::printf("Wrote %s.wmal\n", base.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  templates : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterMail";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmal");
    auto c = wowee::pipeline::WoweeMailLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeMailLoader>(c, base, "gen-mail", ".wmal")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHoliday(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HolidayMail";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmal");
    auto c = wowee::pipeline::WoweeMailLoader::makeHoliday(name);
    if (!saveOrError<wowee::pipeline::WoweeMailLoader>(c, base, "gen-mail-holiday", ".wmal")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAuction(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AuctionMail";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmal");
    auto c = wowee::pipeline::WoweeMailLoader::makeAuction(name);
    if (!saveOrError<wowee::pipeline::WoweeMailLoader>(c, base, "gen-mail-auction", ".wmal")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmal");
    if (!wowee::pipeline::WoweeMailLoader::exists(base)) {
        return reportMissing("WMAL", base, ".wmal");
    }
    auto c = wowee::pipeline::WoweeMailLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmal"] = base + ".wmal";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json je;
            je["templateId"] = e.templateId;
            je["senderNpcId"] = e.senderNpcId;
            je["subject"] = e.subject;
            je["body"] = e.body;
            je["senderName"] = e.senderName;
            je["moneyCopperAttached"] = e.moneyCopperAttached;
            je["categoryId"] = e.categoryId;
            je["categoryName"] = wowee::pipeline::WoweeMail::categoryName(e.categoryId);
            je["cod"] = e.cod;
            je["returnable"] = e.returnable;
            je["expiryDays"] = e.expiryDays;
            nlohmann::json att = nlohmann::json::array();
            for (const auto& a : e.attachments) {
                att.push_back({{"itemId", a.itemId},
                                {"quantity", a.quantity}});
            }
            je["attachments"] = att;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMAL: %s.wmal\n", base.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  templates : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::printf("\n  templateId=%u  category=%s  expires=%ud  cod=%u  return=%u\n",
                    e.templateId,
                    wowee::pipeline::WoweeMail::categoryName(e.categoryId),
                    e.expiryDays, e.cod, e.returnable);
        if (e.senderNpcId) {
            std::printf("    sender    : %s (npcId=%u)\n",
                        e.senderName.c_str(), e.senderNpcId);
        } else {
            std::printf("    sender    : %s\n", e.senderName.c_str());
        }
        std::printf("    subject   : %s\n", e.subject.c_str());
        if (!e.body.empty()) {
            std::printf("    body      : %s\n", e.body.c_str());
        }
        if (e.moneyCopperAttached) {
            std::printf("    money     : %uc\n", e.moneyCopperAttached);
        }
        if (!e.attachments.empty()) {
            std::printf("    items     :");
            for (const auto& a : e.attachments) {
                std::printf(" item%u x%u", a.itemId, a.quantity);
            }
            std::printf("\n");
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each template emits scalar fields plus
    // attachments array; categoryId emits dual int + name
    // forms.
    return cli::exportCatalogJson<wowee::pipeline::WoweeMailLoader>(
        i, argc, argv, "wmal", "WMAL", "templates ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json je;
            je["templateId"] = e.templateId;
            je["senderNpcId"] = e.senderNpcId;
            je["subject"] = e.subject;
            je["body"] = e.body;
            je["senderName"] = e.senderName;
            je["moneyCopperAttached"] = e.moneyCopperAttached;
            je["categoryId"] = e.categoryId;
            je["categoryName"] = wowee::pipeline::WoweeMail::categoryName(e.categoryId);
            je["cod"] = e.cod;
            je["returnable"] = e.returnable;
            je["expiryDays"] = e.expiryDays;
            nlohmann::json att = nlohmann::json::array();
            for (const auto& a : e.attachments) {
                att.push_back({{"itemId", a.itemId},
                                {"quantity", a.quantity}});
            }
            je["attachments"] = att;
            arr.push_back(je);
        }
        j["entries"] = arr;
            return j;
        });
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wmal");
    outBase = cli::withoutExt(outBase, ".wmal");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wmal-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wmal-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto categoryFromName = [](const std::string& s) -> uint8_t {
        if (s == "quest")       return wowee::pipeline::WoweeMail::QuestReward;
        if (s == "auction")     return wowee::pipeline::WoweeMail::Auction;
        if (s == "gm")          return wowee::pipeline::WoweeMail::GmCorrespondence;
        if (s == "achievement") return wowee::pipeline::WoweeMail::AchievementReward;
        if (s == "event")       return wowee::pipeline::WoweeMail::EventMailing;
        if (s == "raffle")      return wowee::pipeline::WoweeMail::Raffle;
        if (s == "script")      return wowee::pipeline::WoweeMail::ScriptDelivery;
        if (s == "returned")    return wowee::pipeline::WoweeMail::ReturnedMail;
        return wowee::pipeline::WoweeMail::QuestReward;
    };
    wowee::pipeline::WoweeMail c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeMail::Entry e;
            e.templateId = je.value("templateId", 0u);
            e.senderNpcId = je.value("senderNpcId", 0u);
            e.subject = je.value("subject", std::string{});
            e.body = je.value("body", std::string{});
            e.senderName = je.value("senderName", std::string{});
            e.moneyCopperAttached = je.value("moneyCopperAttached", 0u);
            if (je.contains("categoryId") && je["categoryId"].is_number_integer()) {
                e.categoryId = static_cast<uint8_t>(je["categoryId"].get<int>());
            } else if (je.contains("categoryName") &&
                       je["categoryName"].is_string()) {
                e.categoryId = categoryFromName(je["categoryName"].get<std::string>());
            }
            e.cod = static_cast<uint8_t>(je.value("cod", 0));
            e.returnable = static_cast<uint8_t>(je.value("returnable", 1));
            e.expiryDays = static_cast<uint16_t>(je.value("expiryDays", 30));
            if (je.contains("attachments") && je["attachments"].is_array()) {
                for (const auto& ja : je["attachments"]) {
                    wowee::pipeline::WoweeMail::Attachment a;
                    a.itemId = ja.value("itemId", 0u);
                    a.quantity = static_cast<uint16_t>(ja.value("quantity", 1));
                    e.attachments.push_back(a);
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeMailLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmal-json: failed to save %s.wmal\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmal\n", outBase.c_str());
    std::printf("  source    : %s\n", jsonPath.c_str());
    std::printf("  templates : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeMailLoader>(
        i, argc, argv, "wmal", "WMAL",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.templateId) + ")";
            if (e.templateId == 0) errors.push_back(ctx + ": templateId is 0");
            if (e.subject.empty()) errors.push_back(ctx + ": subject is empty");
            if (e.senderNpcId == 0 && e.senderName.empty()) {
                errors.push_back(ctx +
                    ": neither senderNpcId nor senderName set (no displayable sender)");
            }
            if (e.categoryId > wowee::pipeline::WoweeMail::ReturnedMail) {
                errors.push_back(ctx + ": categoryId " +
                    std::to_string(e.categoryId) + " not in 0..7");
            }
            if (e.expiryDays == 0) {
                warnings.push_back(ctx +
                    ": expiryDays=0 (mail expires immediately)");
            }
            if (e.cod && e.moneyCopperAttached == 0) {
                warnings.push_back(ctx +
                    ": cod=1 but moneyCopperAttached=0 (free COD)");
            }
            // Mail with no money + no items is informational only.
            // Legitimate for GM correspondence (text-only notices)
            // and for the Auction category where the runtime fills
            // in the real outcome (winning bid amount / sold item)
            // at send time. Flag only for the categories where
            // empty mail is genuinely a typo.
            if (e.moneyCopperAttached == 0 && e.attachments.empty() &&
                e.categoryId != wowee::pipeline::WoweeMail::GmCorrespondence &&
                e.categoryId != wowee::pipeline::WoweeMail::Auction &&
                e.categoryId != wowee::pipeline::WoweeMail::ReturnedMail) {
                warnings.push_back(ctx +
                    ": no money + no items (informational mail only)");
            }
            for (size_t ai = 0; ai < e.attachments.size(); ++ai) {
                const auto& a = e.attachments[ai];
                if (a.itemId == 0) {
                    errors.push_back(ctx + " attachment " + std::to_string(ai) +
                        ": itemId is 0");
                }
                if (a.quantity == 0) {
                    errors.push_back(ctx + " attachment " + std::to_string(ai) +
                        ": quantity is 0");
                }
            }
            if (!idsSeen.add(e.templateId)) errors.push_back(ctx + ": duplicate templateId");
        }
            return formatted("%zu templates, all templateIds unique", c.entries.size());
        });
}

} // namespace

bool handleMailCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-mail") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mail-holiday") == 0 && i + 1 < argc) {
        outRc = handleGenHoliday(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mail-auction") == 0 && i + 1 < argc) {
        outRc = handleGenAuction(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmal") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmal") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmal-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmal-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
