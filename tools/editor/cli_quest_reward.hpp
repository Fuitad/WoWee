#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the quest-reward mutation handlers - both operate
// on a quest's reward struct in zone.json.
//   --add-quest-reward-item   append item rewards (greedy multi-arg)
//   --set-quest-reward        update XP / gold / silver / copper
//
// Returns true if matched; outRc holds the exit code.
bool handleQuestReward(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
