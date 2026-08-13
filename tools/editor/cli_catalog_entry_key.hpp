#pragma once

/**
 * cli_catalog_entry_key.hpp - which field of a catalog entry is its own id.
 *
 * Three commands search catalogs by id - --catalog-find, --catalog-id-range
 * and --catalog-by-name - and each carried its own copy of the answer. They
 * did not agree. Each walked the entry's keys looking for the first one ending
 * in "Id" that was not in a hand-kept list of names that mean "a reference to
 * some other entry", and the three lists had drifted to 51, 28 and 18 names
 * with none a superset of another.
 *
 * The lists could not have been made to agree, because the question they ask
 * has no answer in the abstract. `familyId` is a reference to another catalog
 * when it appears on a creature and is the primary key when it appears on a
 * creature family; `guildId` is the key of a guild and a reference from a
 * tabard. A name-based filter has to be wrong about one of the two.
 *
 * Measured against the info handlers that emit these entries, the three
 * commands named a field other than the entry's own id for 49 of the 128
 * catalog formats: --catalog-find on an item catalog matched displayId rather
 * than itemId, on a creature catalog familyId rather than creatureId, and on
 * achievement criteria all three picked a different field again. Nothing
 * reports an error in any of those cases - the search simply matches the wrong
 * entries, or none.
 *
 * So the key is declared per format, in the format table, next to the magic
 * and the extension. The heuristic below is kept only for a format that has
 * not declared one, where it is still a guess but now one guess rather than
 * three.
 */

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace wowee {
namespace editor {
namespace cli {

/// The result of asking an entry for its id: which field answered and what it
/// held. `found` is false for an entry with no usable numeric field at all.
struct EntryPrimaryKey {
    bool found = false;
    uint64_t value = 0;
    std::string name;
};

/// Field names that mean "this points at another entry" rather than "this is
/// what I am".
///
/// The union of the three lists that drifted apart, and only ever consulted
/// for a format whose primary key is not declared. A name here is not wrong
/// for a format that uses it as its key: the declared key is checked first and
/// wins.
inline bool isExternalRefField(const std::string& k) {
    static const char* kExternals[] = {
        "mapId", "areaId", "zoneId", "subAreaId",
        "spellId", "itemId", "npcId", "creatureId",
        "objectId", "gameObjectId",
        "factionId", "factionTemplateId", "guildId",
        "difficultyId", "instanceId",
        "raceId", "classId", "classMask", "raceMask",
        "skillLineId", "questId", "talentId",
        "achievementId", "criteriaId", "lootId",
        "soundId", "movieId", "displayId", "modelId",
        "iconId", "textureId", "auraId",
        "animationId", "particleId", "ribbonId",
        "vehicleId", "seatId", "currencyId",
        "trainerId", "vendorId", "mailTemplateId",
        "playerId", "characterId", "creatorPlayerId",
        "ownerId", "ownerCharacterId", "leaderId",
        "emblemId", "glyphId", "decalId",
        "previousRankId", "nextRankId",
    };
    for (const char* ref : kExternals) {
        if (k == ref) return true;
    }
    return false;
}

/// The id of one catalog entry.
///
/// `declaredKey` is the format's own statement of which field that is, from
/// the format table, and is used whenever the entry actually carries it as an
/// integer. Everything after that is the old guess, in the order it was
/// already tried: a non-reference `*Id`, then any `*Id`.
///
/// `allowAnyInteger` adds a last resort - the first integer field of any name.
/// Only --catalog-find had it, and it is how that command still reports
/// something for an entry with no id-shaped field rather than skipping it.
inline EntryPrimaryKey entryPrimaryKey(const nlohmann::json& entry,
                                       const char* declaredKey,
                                       bool allowAnyInteger = false) {
    EntryPrimaryKey out;
    if (!entry.is_object()) return out;

    if (declaredKey && *declaredKey) {
        auto it = entry.find(declaredKey);
        if (it != entry.end() && it->is_number_integer()) {
            return {true, it->get<uint64_t>(), declaredKey};
        }
    }

    const auto endsInId = [](const std::string& k) {
        return k.size() >= 2 && k.compare(k.size() - 2, 2, "Id") == 0;
    };

    for (auto it = entry.begin(); it != entry.end(); ++it) {
        if (endsInId(it.key()) && it->is_number_integer() &&
            !isExternalRefField(it.key())) {
            return {true, it->get<uint64_t>(), it.key()};
        }
    }
    for (auto it = entry.begin(); it != entry.end(); ++it) {
        if (endsInId(it.key()) && it->is_number_integer()) {
            return {true, it->get<uint64_t>(), it.key()};
        }
    }
    if (allowAnyInteger) {
        for (auto it = entry.begin(); it != entry.end(); ++it) {
            if (it->is_number_integer()) {
                return {true, it->get<uint64_t>(), it.key()};
            }
        }
    }
    return out;
}

} // namespace cli
} // namespace editor
} // namespace wowee
