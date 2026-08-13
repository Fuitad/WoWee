#pragma once

/// Reading one sound file into a sample.
///
/// Five of the sound managers had this, identical but for the sample type and
/// the name in the log line: the UI, ambient, combat, movement and spell ones.
/// What they share is a small policy rather than mechanics - a file that reads
/// as empty is a failure rather than a silent success, and a throwing read is
/// caught here rather than unwinding into whatever was building a sound bank.
/// Five copies of a policy is five chances for one of them to start treating
/// an empty file as loaded, and the sound it should have played simply never
/// arrives.
///
/// The mount and NPC voice managers deliberately do not use this: they probe
/// fileExists before reading and take their asset manager from a member rather
/// than an argument, which is a different shape and not one to flatten into
/// this by hand.

#include <exception>
#include <random>
#include <string>
#include <vector>

#include "core/logger.hpp"
#include "pipeline/asset_manager.hpp"

namespace wowee::audio {

/// Fill `sample` from `path`. False leaves it marked unloaded.
///
/// `who` names the manager in the log line, or is null to fail quietly. That
/// is not a detail: the combat, movement and spell banks ask for sounds that
/// legitimately may not be present in a given install and say so in a comment,
/// while the UI and ambient banks report a miss. Flattening the five into one
/// that always logs would have turned three of them into log spam on a normal
/// client, which is why the distinction is a parameter rather than a default.
template <typename Sample>
bool loadSampleFile(const std::string& path, Sample& sample,
                    pipeline::AssetManager* assets, const char* who) {
    sample.path = path;
    sample.loaded = false;
    if (!assets) return false;

    try {
        sample.data = assets->readFile(path);
        if (!sample.data.empty()) {
            sample.loaded = true;
            return true;
        }
    } catch (const std::exception& e) {
        if (who) LOG_ERROR(who, ": Failed to load ", path, ": ", e.what());
    }
    return false;
}

/// Which of `library`'s loaded samples to play, or null when none are.
///
/// The combat, movement and spell banks each wrote this out: gather the ones
/// that loaded, and if any did, pick one uniformly. They differed only in the
/// sample type and in the base volume, which stays at the call sites because
/// it is a decision about that bank rather than about picking.
///
/// Choosing among the *loaded* ones matters and is easy to get wrong by
/// indexing the whole library instead: a bank whose files are half missing
/// would then fall silent half the time it was asked to play, with nothing
/// reported either way.
template <typename Sample, typename Rng>
const Sample* pickLoadedSample(const std::vector<Sample>& library, Rng& gen) {
    std::vector<const Sample*> loaded;
    loaded.reserve(library.size());
    for (const Sample& sample : library) {
        if (sample.loaded) loaded.push_back(&sample);
    }
    if (loaded.empty()) return nullptr;
    std::uniform_int_distribution<size_t> pick(0, loaded.size() - 1);
    return loaded[pick(gen)];
}

}  // namespace wowee::audio
