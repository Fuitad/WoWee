#pragma once

/**
 * geoset_rules.hpp — how a character geoset id is chosen when a model does not
 * have the one that was asked for.
 *
 * A geoset id is a group and a variant: group * 100 + variant. The members of a
 * group are alternatives for one part of a character — five kinds of boot, six
 * kinds of cloak — so when a model does not carry the exact variant asked for,
 * another member of the same group is usually the right answer.
 *
 * Usually. Variant 1 means NONE — bare feet, no cloak, no beard — and so does
 * variant 0 where a DBC stores the variant directly and uses zero for absent.
 * Every other member of the group is *something*, so substituting for none does
 * not approximate it, it contradicts it. That single mistake produced three
 * separate faults: a clean-shaven NPC with a beard, a character with no cloak
 * wearing an untextured one, and a player with no feet.
 *
 * It produced them three times because this rule was written five times, as a
 * local lambda in each place that needed it, and only some of them learned it.
 * It lives here now, with a test, and the call sites ask rather than decide.
 */

#include <cstdint>
#include <unordered_set>

namespace wowee {
namespace core {

/// The body part a geoset id belongs to: 501 and 505 are both group 5, boots.
constexpr uint16_t geosetGroup(uint16_t id) { return static_cast<uint16_t>(id / 100); }

/// Which alternative within the group: 505 is variant 5.
constexpr uint16_t geosetVariant(uint16_t id) { return static_cast<uint16_t>(id % 100); }

/// Whether this id is the group's way of saying the character has none of it.
///
/// Both spellings appear. The geoset tables use variant 1 — 501 is bare feet,
/// 1501 is no cloak, 101 is no beard. The DBCs that store a variant directly,
/// CharFacialHairStyles among them, use 0 for absent, and 0 arrives here as
/// group*100 + 0.
constexpr bool geosetMeansNone(uint16_t id) {
    const uint16_t variant = geosetVariant(id);
    return variant == 0 || variant == 1;
}

/// The geoset a model should actually draw for `preferred`.
///
/// Returns `preferred` when the model has it; the group's lowest member when it
/// does not and a substitute is meaningful; and 0 — draw nothing — when what was
/// asked for was none and the model has no way to say so. A caller that gets 0
/// adds nothing for that group.
///
/// `available` is what the model carries. An empty set means "unknown", and then
/// `preferred` is returned unchanged rather than guessed at.
inline uint16_t resolveGeoset(uint16_t preferred,
                              const std::unordered_set<uint16_t>& available) {
    if (available.empty()) return preferred;
    if (available.count(preferred) > 0) return preferred;
    if (geosetMeansNone(preferred)) return 0;

    const uint16_t group = geosetGroup(preferred);
    uint16_t lowest = 0;
    for (uint16_t id : available) {
        if (geosetGroup(id) != group) continue;
        if (lowest == 0 || id < lowest) lowest = id;
    }
    return lowest;
}

/// Add a character's facial-hair geosets — beard, moustache, sideburns — to a set.
///
/// CharFacialHairStyles stores a variant per group and uses 0 for "this
/// character has none of that". Zero must not be turned into an id: group*100+0
/// is a geoset no model carries, and a caller that then substitutes within the
/// group hands a beard to someone who asked for none. That happened, on NPCs.
///
/// A caller that inserts raw ids was only accidentally safe — the invalid id
/// matched nothing — so both spellings of the mistake are removed by asking here
/// instead.
inline void addFacialHairGeosets(std::unordered_set<uint16_t>& out,
                                 uint16_t variant100, uint16_t variant200,
                                 uint16_t variant300) {
    if (variant100 != 0) out.insert(static_cast<uint16_t>(100 + variant100));
    if (variant200 != 0) out.insert(static_cast<uint16_t>(200 + variant200));
    if (variant300 != 0) out.insert(static_cast<uint16_t>(300 + variant300));
}

/// Key for the (race, sex, variation) maps built from CharHairGeosets.dbc and
/// CharFacialHairStyles.dbc.
///
/// It was packed by hand in ten places across three files. Every one of them had
/// to agree bit for bit with every other, or a lookup would miss and say
/// nothing — and a hair or beard lookup that misses does not report a fault, it
/// quietly draws the default.
constexpr uint32_t appearanceKey(uint8_t race, uint8_t sex, uint8_t variation) {
    return (static_cast<uint32_t>(race) << 16) |
           (static_cast<uint32_t>(sex) << 8) |
           static_cast<uint32_t>(variation);
}

}  // namespace core
}  // namespace wowee
