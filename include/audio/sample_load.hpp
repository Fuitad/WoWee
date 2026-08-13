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
#include <string>

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

}  // namespace wowee::audio
