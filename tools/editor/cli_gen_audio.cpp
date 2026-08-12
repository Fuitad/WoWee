#include "cli_gen_audio.hpp"
#include "cli_subprocess.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Write a PCM-16 mono WAV with a hand-rolled 44-byte RIFF/WAVE header.
// No library dependencies. Returns false if the file can't be opened.
bool writeWavMono16(const std::string& path,
                    const std::vector<int16_t>& samples,
                    int sampleRate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t totalSamples = static_cast<uint32_t>(samples.size());
    uint32_t dataBytes = totalSamples * 2;
    uint32_t riffSize = 36 + dataBytes;
    uint16_t numChannels = 1;
    uint16_t bitsPerSample = 16;
    uint16_t blockAlign = numChannels * bitsPerSample / 8;
    uint32_t byteRate = sampleRate * blockAlign;
    auto wU32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto wU16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    wU32(riffSize);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    wU32(16);                  // fmt chunk size
    wU16(1);                   // PCM
    wU16(numChannels);
    wU32(static_cast<uint32_t>(sampleRate));
    wU32(byteRate);
    wU16(blockAlign);
    wU16(bitsPerSample);
    std::fwrite("data", 1, 4, f);
    wU32(dataBytes);
    std::fwrite(samples.data(), 2, totalSamples, f);
    std::fclose(f);
    return true;
}

// Apply 5ms attack/release fade to prevent click on tone start/stop.
void applyEdgeEnvelope(std::vector<int16_t>& samples, int sampleRate) {
    uint32_t total = static_cast<uint32_t>(samples.size());
    int envSamples = std::min<uint32_t>(total / 4,
                        static_cast<uint32_t>(sampleRate * 0.005f));
    if (envSamples <= 0) return;
    for (uint32_t s = 0; s < total; ++s) {
        float env = 1.0f;
        if (static_cast<int>(s) < envSamples) {
            env = static_cast<float>(s) / envSamples;
        } else if (static_cast<int>(total - s) < envSamples) {
            env = static_cast<float>(total - s) / envSamples;
        }
        samples[s] = static_cast<int16_t>(samples[s] * env);
    }
}

int handleTone(int& i, int argc, char** argv) {
    // Synthesize a procedural mono PCM-16 WAV. Opens a new
    // file family in the open-format ecosystem (alongside
    // WOM/WOB/PNG/JSON) - proprietary MP3 placeholders can
    // be replaced with hand-synthesized WAVs that have no
    // patent or licensing baggage.
    std::string outPath = argv[++i];
    float freq = 0.0f;
    float duration = 0.0f;
    try { freq = std::stof(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-audio-tone: <freqHz> must be a number\n");
        return 1;
    }
    try { duration = std::stof(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-audio-tone: <durationSec> must be a number\n");
        return 1;
    }
    int sampleRate = 44100;
    std::string waveform = "sine";
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { sampleRate = std::stoi(argv[++i]); } catch (...) {}
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        waveform = argv[++i];
    }
    if (freq <= 0 || freq > 24000 ||
        duration <= 0 || duration > 600 ||
        sampleRate < 8000 || sampleRate > 192000) {
        std::fprintf(stderr,
            "gen-audio-tone: freq 0..24000Hz, duration 0..600s, sampleRate 8000..192000\n");
        return 1;
    }
    uint32_t totalSamples = static_cast<uint32_t>(duration * sampleRate);
    const float twoPi = 2.0f * 3.14159265358979f;
    std::vector<int16_t> samples(totalSamples, 0);
    for (uint32_t s = 0; s < totalSamples; ++s) {
        float t = static_cast<float>(s) / sampleRate;
        float phase = std::fmod(t * freq, 1.0f);
        float v = 0.0f;
        if (waveform == "sine") {
            v = std::sin(twoPi * t * freq);
        } else if (waveform == "square") {
            v = (phase < 0.5f) ? 1.0f : -1.0f;
        } else if (waveform == "triangle") {
            v = (phase < 0.5f)
                ? (4.0f * phase - 1.0f)
                : (3.0f - 4.0f * phase);
        } else if (waveform == "saw") {
            v = 2.0f * phase - 1.0f;
        } else {
            std::fprintf(stderr,
                "gen-audio-tone: unknown waveform '%s' (sine|square|triangle|saw)\n",
                waveform.c_str());
            return 1;
        }
        v *= 0.5f;  // 50% headroom, never clip
        samples[s] = static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    }
    applyEdgeEnvelope(samples, sampleRate);
    if (!writeWavMono16(outPath, samples, sampleRate)) {
        std::fprintf(stderr,
            "gen-audio-tone: cannot open %s for write\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  format     : WAV PCM-16 mono\n");
    std::printf("  freq       : %.2f Hz\n", freq);
    std::printf("  duration   : %.3f sec\n", duration);
    std::printf("  sampleRate : %d Hz\n", sampleRate);
    std::printf("  waveform   : %s\n", waveform.c_str());
    std::printf("  samples    : %u\n", totalSamples);
    std::printf("  bytes      : %u (44-byte header + data)\n",
                44 + totalSamples * 2);
    return 0;
}

int handleNoise(int& i, int argc, char** argv) {
    // Procedural noise WAV. Three "colors" in audio engineering:
    //   white  - equal energy per Hz (uniform random samples)
    //   pink   - equal energy per octave (1/f spectrum) via
    //            Voss-McCartney 7-band cascade. Sounds like
    //            rain or wind.
    //   brown  - 1/f² spectrum via random walk (integrated
    //            white noise). Sounds like distant surf or
    //            rumbling weather.
    std::string outPath = argv[++i];
    float duration = 0.0f;
    try { duration = std::stof(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-audio-noise: <durationSec> must be a number\n");
        return 1;
    }
    int sampleRate = 22050;
    std::string color = "white";
    uint32_t seed = 1;
    float amp = 0.5f;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { sampleRate = std::stoi(argv[++i]); } catch (...) {}
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        color = argv[++i];
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { seed = static_cast<uint32_t>(std::stoul(argv[++i])); } catch (...) {}
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { amp = std::stof(argv[++i]); } catch (...) {}
    }
    if (duration <= 0 || duration > 600 ||
        sampleRate < 8000 || sampleRate > 192000 ||
        amp <= 0 || amp > 1.0f) {
        std::fprintf(stderr,
            "gen-audio-noise: duration 0..600s, sampleRate 8000..192000, amp 0..1\n");
        return 1;
    }
    uint32_t totalSamples = static_cast<uint32_t>(duration * sampleRate);
    std::vector<int16_t> samples(totalSamples, 0);
    uint32_t state = seed ? seed : 1u;
    auto next01 = [&state]() -> float {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) * (1.0f / 16777216.0f);
    };
    auto nextSigned = [&]() -> float { return next01() * 2.0f - 1.0f; };
    // Voss-McCartney pink noise state: 7 random rows
    // updated at progressively halved rates.
    float pinkRows[7] = {0};
    float pinkSum = 0.0f;
    int pinkIdx = 0;
    float brownState = 0.0f;
    for (uint32_t s = 0; s < totalSamples; ++s) {
        float v = 0.0f;
        if (color == "white") {
            v = nextSigned();
        } else if (color == "pink") {
            pinkIdx++;
            int rowsToUpdate = 0;
            int idx = pinkIdx;
            while ((idx & 1) == 0 && rowsToUpdate < 7) {
                idx >>= 1;
                rowsToUpdate++;
            }
            pinkSum -= pinkRows[rowsToUpdate];
            pinkRows[rowsToUpdate] = nextSigned();
            pinkSum += pinkRows[rowsToUpdate];
            v = pinkSum / 7.0f;
        } else if (color == "brown") {
            brownState = std::clamp(brownState + nextSigned() * 0.1f, -1.0f, 1.0f);
            v = brownState * 3.0f;  // amplify since walk stays small
        } else {
            std::fprintf(stderr,
                "gen-audio-noise: unknown color '%s' (white|pink|brown)\n",
                color.c_str());
            return 1;
        }
        v *= amp;
        samples[s] = static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    }
    applyEdgeEnvelope(samples, sampleRate);
    if (!writeWavMono16(outPath, samples, sampleRate)) {
        std::fprintf(stderr,
            "gen-audio-noise: cannot open %s for write\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  format     : WAV PCM-16 mono\n");
    std::printf("  duration   : %.3f sec\n", duration);
    std::printf("  sampleRate : %d Hz\n", sampleRate);
    std::printf("  color      : %s noise\n", color.c_str());
    std::printf("  amplitude  : %.2f\n", amp);
    std::printf("  seed       : %u\n", seed);
    std::printf("  samples    : %u\n", totalSamples);
    std::printf("  bytes      : %u (44-byte header + data)\n",
                44 + totalSamples * 2);
    return 0;
}

int handleSweep(int& i, int argc, char** argv) {
    // Frequency sweep (chirp) WAV. Sine wave whose frequency
    // glides from startHz to endHz across the duration.
    //
    //   linear: f(t) = f0 + (f1-f0) * (t/T)
    //           Phase integrates to f0*t + (f1-f0)*t²/(2T)
    //   exp:    f(t) = f0 * (f1/f0)^(t/T)
    //           Phase integrates to f0*T/ln(r) * (r^(t/T)-1)
    //           where r = f1/f0
    std::string outPath = argv[++i];
    float f0 = 0.0f, f1 = 0.0f, duration = 0.0f;
    try { f0 = std::stof(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-audio-sweep: <startHz> must be a number\n");
        return 1;
    }
    try { f1 = std::stof(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-audio-sweep: <endHz> must be a number\n");
        return 1;
    }
    try { duration = std::stof(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-audio-sweep: <durationSec> must be a number\n");
        return 1;
    }
    int sampleRate = 44100;
    std::string shape = "linear";
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { sampleRate = std::stoi(argv[++i]); } catch (...) {}
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        shape = argv[++i];
    }
    if (f0 <= 0 || f0 > 24000 || f1 <= 0 || f1 > 24000 ||
        duration <= 0 || duration > 600 ||
        sampleRate < 8000 || sampleRate > 192000) {
        std::fprintf(stderr,
            "gen-audio-sweep: freqs 0..24000Hz, duration 0..600s, sampleRate 8000..192000\n");
        return 1;
    }
    if (shape != "linear" && shape != "exp") {
        std::fprintf(stderr,
            "gen-audio-sweep: unknown shape '%s' (linear|exp)\n", shape.c_str());
        return 1;
    }
    uint32_t totalSamples = static_cast<uint32_t>(duration * sampleRate);
    const float twoPi = 2.0f * 3.14159265358979f;
    std::vector<int16_t> samples(totalSamples, 0);
    float r = f1 / f0;
    float lnR = std::log(r);
    for (uint32_t s = 0; s < totalSamples; ++s) {
        float t = static_cast<float>(s) / sampleRate;
        float phase;
        if (shape == "linear") {
            phase = f0 * t + 0.5f * (f1 - f0) * t * t / duration;
        } else {
            if (std::abs(lnR) < 1e-6f) {
                phase = f0 * t;
            } else {
                phase = f0 * duration / lnR *
                        (std::exp(lnR * t / duration) - 1.0f);
            }
        }
        float v = std::sin(twoPi * phase) * 0.5f;
        samples[s] = static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    }
    applyEdgeEnvelope(samples, sampleRate);
    if (!writeWavMono16(outPath, samples, sampleRate)) {
        std::fprintf(stderr,
            "gen-audio-sweep: cannot open %s for write\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  format     : WAV PCM-16 mono\n");
    std::printf("  freq       : %.2f -> %.2f Hz (%s)\n",
                f0, f1, shape.c_str());
    std::printf("  duration   : %.3f sec\n", duration);
    std::printf("  sampleRate : %d Hz\n", sampleRate);
    std::printf("  samples    : %u\n", totalSamples);
    std::printf("  bytes      : %u (44-byte header + data)\n",
                44 + totalSamples * 2);
    return 0;
}

int handleZoneAudioPack(int& i, int argc, char** argv) {
    // Drop a 6-WAV starter audio pack into <zoneDir>/audio/.
    // Two ambient drones (low + fifth above), a melodic chime,
    // a UI click, an alert, and a music stinger. All hand-
    // synthesized PCM-16 mono WAVs with no licensing baggage,
    // replacing typical proprietary MP3 placeholders.
    std::string zoneDir = argv[++i];
    std::filesystem::path zp(zoneDir);
    if (!std::filesystem::exists(zp / "zone.json")) {
        std::fprintf(stderr,
            "gen-zone-audio-pack: %s has no zone.json\n",
            zoneDir.c_str());
        return 1;
    }
    std::filesystem::path audioDir = zp / "audio";
    std::error_code ec;
    std::filesystem::create_directories(audioDir, ec);
    if (ec) {
        std::fprintf(stderr,
            "gen-zone-audio-pack: cannot create %s: %s\n",
            audioDir.string().c_str(), ec.message().c_str());
        return 1;
    }
    std::string self = (argc > 0) ? argv[0] : "wowee_editor";
    struct AudioJob {
        std::string fileName;
        std::string freq;
        std::string duration;
        std::string sampleRate;
        std::string waveform;
    };
    std::vector<AudioJob> jobs = {
        {"ambient-low.wav",    "110",  "3.0", "22050", "sine"},
        {"ambient-mid.wav",    "165",  "3.0", "22050", "sine"},
        {"music-stinger.wav",  "220",  "1.5", "44100", "triangle"},
        {"chime.wav",          "880",  "0.4", "44100", "triangle"},
        {"alert.wav",          "660",  "0.2", "44100", "square"},
        {"click.wav",          "1500", "0.04","44100", "square"},
    };
    int written = 0;
    for (const auto& job : jobs) {
        std::filesystem::path out = audioDir / job.fileName;
        int rc = wowee::editor::cli::runChild(self, {
            "--gen-audio-tone", out.string(),
            job.freq, job.duration, job.sampleRate, job.waveform
        }, /*quiet=*/true);
        if (rc != 0) {
            std::fprintf(stderr,
                "gen-zone-audio-pack: %s failed (rc=%d)\n",
                job.fileName.c_str(), rc);
        } else {
            ++written;
        }
    }
    std::printf("gen-zone-audio-pack: wrote %d of %zu sounds to %s\n",
                written, jobs.size(), audioDir.string().c_str());
    return written == static_cast<int>(jobs.size()) ? 0 : 1;
}

}  // namespace

bool handleGenAudio(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-audio-tone") == 0 && i + 3 < argc) {
        outRc = handleTone(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--gen-audio-noise") == 0 && i + 2 < argc) {
        outRc = handleNoise(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--gen-audio-sweep") == 0 && i + 4 < argc) {
        outRc = handleSweep(i, argc, argv);
        return true;
    }
    if (std::strcmp(argv[i], "--gen-zone-audio-pack") == 0 && i + 1 < argc) {
        outRc = handleZoneAudioPack(i, argc, argv);
        return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
