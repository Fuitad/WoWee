#include "cli_vehicles_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_vehicles.hpp"
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


size_t totalSeats(const wowee::pipeline::WoweeVehicle& c) {
    size_t n = 0;
    for (const auto& e : c.entries) n += e.seats.size();
    return n;
}

void printGenSummary(const wowee::pipeline::WoweeVehicle& c,
                     const std::string& base) {
    std::printf("Wrote %s.wvhc\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  vehicles : %zu (%zu seats total)\n",
                c.entries.size(), totalSeats(c));
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterVehicles";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wvhc");
    auto c = wowee::pipeline::WoweeVehicleLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeVehicleLoader>(c, base, "gen-vehicles", ".wvhc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenSiege(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SiegeVehicles";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wvhc");
    auto c = wowee::pipeline::WoweeVehicleLoader::makeSiege(name);
    if (!saveOrError<wowee::pipeline::WoweeVehicleLoader>(c, base, "gen-vehicles-siege", ".wvhc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFlying(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FlyingVehicles";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wvhc");
    auto c = wowee::pipeline::WoweeVehicleLoader::makeFlying(name);
    if (!saveOrError<wowee::pipeline::WoweeVehicleLoader>(c, base, "gen-vehicles-flying", ".wvhc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wvhc");
    if (!wowee::pipeline::WoweeVehicleLoader::exists(base)) {
        std::fprintf(stderr, "WVHC not found: %s.wvhc\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeVehicleLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wvhc"] = base + ".wvhc";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json seats = nlohmann::json::array();
            for (const auto& s : e.seats) {
                seats.push_back({
                    {"seatIndex", s.seatIndex},
                    {"seatFlags", s.seatFlags},
                    {"attachmentId", s.attachmentId},
                    {"controlSpellId", s.controlSpellId},
                    {"exitSpellId", s.exitSpellId},
                    {"passengerYaw", s.passengerYaw},
                });
            }
            arr.push_back({
                {"vehicleId", e.vehicleId},
                {"creatureId", e.creatureId},
                {"name", e.name},
                {"description", e.description},
                {"vehicleKind", e.vehicleKind},
                {"vehicleKindName", wowee::pipeline::WoweeVehicle::vehicleKindName(e.vehicleKind)},
                {"movementKind", e.movementKind},
                {"movementKindName", wowee::pipeline::WoweeVehicle::movementKindName(e.movementKind)},
                {"turnSpeed", e.turnSpeed},
                {"pitchSpeed", e.pitchSpeed},
                {"flightCapabilityId", e.flightCapabilityId},
                {"powerType", e.powerType},
                {"powerTypeName", wowee::pipeline::WoweeVehicle::powerTypeName(e.powerType)},
                {"maxPower", e.maxPower},
                {"seats", seats},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WVHC: %s.wvhc\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  vehicles : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   creature   kind            move      power    seats  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %5u      %-13s   %-7s   %-7s  %3zu    %s\n",
                    e.vehicleId, e.creatureId,
                    wowee::pipeline::WoweeVehicle::vehicleKindName(e.vehicleKind),
                    wowee::pipeline::WoweeVehicle::movementKindName(e.movementKind),
                    wowee::pipeline::WoweeVehicle::powerTypeName(e.powerType),
                    e.seats.size(), e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each vehicle emits all 10 scalar fields
    // plus dual int + name forms for vehicleKind /
    // movementKind / powerType, and a nested array of seats
    // (each with their own scalar fields). Hand-edits can use
    // either int or name form for the enums.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wvhc");
    if (outPath.empty()) outPath = base + ".wvhc.json";
    if (!wowee::pipeline::WoweeVehicleLoader::exists(base)) {
        return reportMissing("export-wvhc-json", "WVHC", base, ".wvhc");
    }
    auto c = wowee::pipeline::WoweeVehicleLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json seats = nlohmann::json::array();
        for (const auto& s : e.seats) {
            seats.push_back({
                {"seatIndex", s.seatIndex},
                {"seatFlags", s.seatFlags},
                {"attachmentId", s.attachmentId},
                {"controlSpellId", s.controlSpellId},
                {"exitSpellId", s.exitSpellId},
                {"passengerYaw", s.passengerYaw},
            });
        }
        arr.push_back({
            {"vehicleId", e.vehicleId},
            {"creatureId", e.creatureId},
            {"name", e.name},
            {"description", e.description},
            {"vehicleKind", e.vehicleKind},
            {"vehicleKindName", wowee::pipeline::WoweeVehicle::vehicleKindName(e.vehicleKind)},
            {"movementKind", e.movementKind},
            {"movementKindName", wowee::pipeline::WoweeVehicle::movementKindName(e.movementKind)},
            {"turnSpeed", e.turnSpeed},
            {"pitchSpeed", e.pitchSpeed},
            {"flightCapabilityId", e.flightCapabilityId},
            {"powerType", e.powerType},
            {"powerTypeName", wowee::pipeline::WoweeVehicle::powerTypeName(e.powerType)},
            {"maxPower", e.maxPower},
            {"seats", seats},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wvhc-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source   : %s.wvhc\n", base.c_str());
    std::printf("  vehicles : %zu (%zu seats total)\n",
                c.entries.size(), totalSeats(c));
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wvhc");
    outBase = cli::withoutExt(outBase, ".wvhc");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wvhc-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wvhc-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto vehicleKindFromName = [](const std::string& s) -> uint8_t {
        if (s == "mount")          return wowee::pipeline::WoweeVehicle::Mount;
        if (s == "chopper")        return wowee::pipeline::WoweeVehicle::Chopper;
        if (s == "tank")           return wowee::pipeline::WoweeVehicle::Tank;
        if (s == "demolisher")     return wowee::pipeline::WoweeVehicle::Demolisher;
        if (s == "gunship")        return wowee::pipeline::WoweeVehicle::Gunship;
        if (s == "flying-mount")   return wowee::pipeline::WoweeVehicle::FlyingMount;
        if (s == "transport-rail") return wowee::pipeline::WoweeVehicle::TransportRail;
        if (s == "siege-weapon")   return wowee::pipeline::WoweeVehicle::SiegeWeapon;
        return wowee::pipeline::WoweeVehicle::Mount;
    };
    auto movementKindFromName = [](const std::string& s) -> uint8_t {
        if (s == "ground")        return wowee::pipeline::WoweeVehicle::Ground;
        if (s == "air")           return wowee::pipeline::WoweeVehicle::Air;
        if (s == "water")         return wowee::pipeline::WoweeVehicle::Water;
        if (s == "submerged")     return wowee::pipeline::WoweeVehicle::Submerged;
        if (s == "air+water")     return wowee::pipeline::WoweeVehicle::AmphibiousAW;
        if (s == "ground+water")  return wowee::pipeline::WoweeVehicle::AmphibiousGW;
        return wowee::pipeline::WoweeVehicle::Ground;
    };
    auto powerTypeFromName = [](const std::string& s) -> uint8_t {
        if (s == "mana")   return wowee::pipeline::WoweeVehicle::Mana;
        if (s == "energy") return wowee::pipeline::WoweeVehicle::Energy;
        if (s == "pyrite") return wowee::pipeline::WoweeVehicle::Pyrite;
        if (s == "heat")   return wowee::pipeline::WoweeVehicle::Heat;
        if (s == "none")   return wowee::pipeline::WoweeVehicle::None;
        return wowee::pipeline::WoweeVehicle::None;
    };
    wowee::pipeline::WoweeVehicle c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeVehicle::Entry e;
            e.vehicleId = je.value("vehicleId", 0u);
            e.creatureId = je.value("creatureId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("vehicleKind") &&
                je["vehicleKind"].is_number_integer()) {
                e.vehicleKind = static_cast<uint8_t>(
                    je["vehicleKind"].get<int>());
            } else if (je.contains("vehicleKindName") &&
                       je["vehicleKindName"].is_string()) {
                e.vehicleKind = vehicleKindFromName(
                    je["vehicleKindName"].get<std::string>());
            }
            if (je.contains("movementKind") &&
                je["movementKind"].is_number_integer()) {
                e.movementKind = static_cast<uint8_t>(
                    je["movementKind"].get<int>());
            } else if (je.contains("movementKindName") &&
                       je["movementKindName"].is_string()) {
                e.movementKind = movementKindFromName(
                    je["movementKindName"].get<std::string>());
            }
            e.turnSpeed = je.value("turnSpeed", 3.14f);
            e.pitchSpeed = je.value("pitchSpeed", 1.0f);
            e.flightCapabilityId = je.value("flightCapabilityId", 0u);
            if (je.contains("powerType") &&
                je["powerType"].is_number_integer()) {
                e.powerType = static_cast<uint8_t>(
                    je["powerType"].get<int>());
            } else if (je.contains("powerTypeName") &&
                       je["powerTypeName"].is_string()) {
                e.powerType = powerTypeFromName(
                    je["powerTypeName"].get<std::string>());
            }
            e.maxPower = je.value("maxPower", 100u);
            if (je.contains("seats") && je["seats"].is_array()) {
                for (const auto& js : je["seats"]) {
                    wowee::pipeline::WoweeVehicle::Seat s;
                    s.seatIndex = static_cast<uint8_t>(
                        js.value("seatIndex", 0));
                    s.seatFlags = static_cast<uint8_t>(
                        js.value("seatFlags", 0));
                    s.attachmentId = static_cast<uint8_t>(
                        js.value("attachmentId", 0));
                    s.controlSpellId = js.value("controlSpellId", 0u);
                    s.exitSpellId = js.value("exitSpellId", 0u);
                    s.passengerYaw = js.value("passengerYaw", 0.0f);
                    e.seats.push_back(s);
                }
            }
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeVehicleLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wvhc-json: failed to save %s.wvhc\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wvhc\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  vehicles : %zu (%zu seats total)\n",
                c.entries.size(), totalSeats(c));
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wvhc");
    if (!wowee::pipeline::WoweeVehicleLoader::exists(base)) {
        return reportMissing("validate-wvhc", "WVHC", base, ".wvhc");
    }
    auto c = wowee::pipeline::WoweeVehicleLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.vehicleId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.vehicleId == 0) errors.push_back(ctx + ": vehicleId is 0");
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.creatureId == 0)
            errors.push_back(ctx + ": creatureId is 0 "
                "(no rendered model)");
        if (e.vehicleKind > wowee::pipeline::WoweeVehicle::SiegeWeapon) {
            errors.push_back(ctx + ": vehicleKind " +
                std::to_string(e.vehicleKind) + " not in 0..7");
        }
        if (e.movementKind > wowee::pipeline::WoweeVehicle::AmphibiousGW) {
            errors.push_back(ctx + ": movementKind " +
                std::to_string(e.movementKind) + " not in 0..5");
        }
        if (e.powerType > wowee::pipeline::WoweeVehicle::None) {
            errors.push_back(ctx + ": powerType " +
                std::to_string(e.powerType) + " not in 0..4");
        }
        if (e.seats.empty()) {
            errors.push_back(ctx +
                ": no seats (vehicle has no rideable position)");
        }
        // Flying vehicles MUST be on Air or AmphibiousAW
        // movement, otherwise they fall through the world.
        if ((e.vehicleKind == wowee::pipeline::WoweeVehicle::FlyingMount ||
             e.vehicleKind == wowee::pipeline::WoweeVehicle::Gunship) &&
            e.movementKind != wowee::pipeline::WoweeVehicle::Air &&
            e.movementKind != wowee::pipeline::WoweeVehicle::AmphibiousAW) {
            errors.push_back(ctx +
                ": flying vehicle without Air/AmphibiousAW movement "
                "(would fall through world)");
        }
        // Driver-flag exclusivity check.
        int driverCount = 0;
        std::vector<uint8_t> seatIdxSeen;
        for (size_t si = 0; si < e.seats.size(); ++si) {
            const auto& s = e.seats[si];
            if (s.seatFlags & wowee::pipeline::WoweeVehicle::kSeatDriver) {
                ++driverCount;
            }
            for (uint8_t prev : seatIdxSeen) {
                if (prev == s.seatIndex) {
                    errors.push_back(ctx + ": seat[" +
                        std::to_string(si) + "] duplicate seatIndex=" +
                        std::to_string(s.seatIndex));
                    break;
                }
            }
            seatIdxSeen.push_back(s.seatIndex);
        }
        if (driverCount == 0) {
            warnings.push_back(ctx +
                ": no seat marked kSeatDriver "
                "(no one can steer this vehicle)");
        }
        if (driverCount > 1) {
            errors.push_back(ctx +
                ": multiple seats marked kSeatDriver (driverCount=" +
                std::to_string(driverCount) + ")");
        }
        if (!idsSeen.add(e.vehicleId)) errors.push_back(ctx + ": duplicate vehicleId");
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wvhc", base, errors, warnings);
    std::printf("validate-wvhc: %s.wvhc\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu vehicles, %zu seats, all vehicleIds unique\n",
                    c.entries.size(), totalSeats(c));
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleVehiclesCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-vehicles") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-vehicles-siege") == 0 && i + 1 < argc) {
        outRc = handleGenSiege(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-vehicles-flying") == 0 && i + 1 < argc) {
        outRc = handleGenFlying(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wvhc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wvhc") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wvhc-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wvhc-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
