// Beat tracker tests on synthetic music with a known tempo.
//   c++ -std=c++20 -O2 -I src test/beats_test.cpp -o beats_test && ./beats_test
#include "beats.hpp"

#include <cstdio>
#include <random>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            ++failures;                                         \
        }                                                       \
    } while (0)

struct Song {
    std::vector<float> samples;
    std::vector<double> beats;
    double sr;
};

// Kick on every beat, snare on 2 and 4, hi-hats on the off-beats, a sustained
// pad and noise. `swing` shifts every other beat; `drift` changes the tempo.
static Song makeSong(double bpm, double first, double seconds, double sr = 22050, double noise = 0.05,
                     double drift = 0.0, unsigned seed = 1) {
    Song s;
    s.sr = sr;
    const size_t n = static_cast<size_t>(seconds * sr);
    s.samples.assign(n, 0.f);
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.f, 1.f);

    double t = first;
    int k = 0;
    while (t < seconds - 0.3) {
        s.beats.push_back(t);
        const double period = 60.0 / (bpm * (1.0 + drift * t / seconds));
        const size_t at = static_cast<size_t>(t * sr);
        // kick: 55 Hz decaying sine with a click
        for (size_t i = 0; i < static_cast<size_t>(0.25 * sr) && at + i < n; ++i) {
            const double u = i / sr;
            s.samples[at + i] += static_cast<float>(0.9 * std::exp(-u * 18.0) * std::sin(2 * M_PI * 55.0 * u));
        }
        // snare on 2 and 4: noise burst
        if (k % 2 == 1) {
            for (size_t i = 0; i < static_cast<size_t>(0.12 * sr) && at + i < n; ++i) {
                s.samples[at + i] += 0.35f * static_cast<float>(std::exp(-(i / sr) * 30.0)) * gauss(rng);
            }
        }
        // hi-hat on the off-beat
        const size_t off = static_cast<size_t>((t + period / 2) * sr);
        for (size_t i = 0; i < static_cast<size_t>(0.04 * sr) && off + i < n; ++i) {
            s.samples[off + i] += 0.15f * static_cast<float>(std::exp(-(i / sr) * 80.0)) * gauss(rng);
        }
        t += period;
        ++k;
    }
    // pad and noise
    for (size_t i = 0; i < n; ++i) {
        const double u = i / sr;
        s.samples[i] += static_cast<float>(0.08 * std::sin(2 * M_PI * 220.0 * u) * (0.6 + 0.4 * std::sin(u * 0.7)));
        s.samples[i] += static_cast<float>(noise) * gauss(rng);
    }
    return s;
}

// fraction of true beats that have a detected beat within `tol` seconds
static double hitRate(const std::vector<double>& truth, const std::vector<double>& got, double tol, double* meanErr = nullptr) {
    if (truth.empty()) return 0.0;
    size_t hits = 0;
    double errSum = 0.0;
    for (double t : truth) {
        double best = 1e9;
        double signedErr = 0.0;
        for (double g : got) {
            if (std::fabs(g - t) < best) {
                best = std::fabs(g - t);
                signedErr = g - t;
            }
        }
        if (best <= tol) {
            ++hits;
            errSum += signedErr;
        }
    }
    if (meanErr) *meanErr = hits ? errSum / hits : 0.0;
    return static_cast<double>(hits) / truth.size();
}

static void checkSong(const char* name, double bpm, double first, double drift, double noise, bool allowHalf = false) {
    Song song = makeSong(bpm, first, 40.0, 22050, noise, drift);
    beats::Track tr = beats::track(song.samples.data(), song.samples.size(), song.sr);
    double meanErr = 0.0;
    const double rate = hitRate(song.beats, tr.times, 0.035, &meanErr);
    const bool tempoOk = std::fabs(tr.bpm - bpm * (1 + drift / 2)) < 3.0 ||
                         (allowHalf && std::fabs(tr.bpm * 2 - bpm) < 3.0);
    std::printf("%-22s true %.1f bpm -> %.2f bpm, conf %.2f, %zu beats, hit %.0f%%, mean err %+.1f ms\n", name, bpm,
                tr.bpm, tr.confidence, tr.times.size(), rate * 100, meanErr * 1000);
    CHECK(tempoOk);
    if (std::fabs(tr.bpm * 2 - bpm) < 3.0) {
        // half tempo: every other beat must still land
        CHECK(hitRate(tr.times, song.beats, 0.035) > 0.9);
    } else {
        CHECK(rate > 0.92);
        CHECK(std::fabs(meanErr) < 0.015);
    }
    CHECK(tr.confidence > 1.5);
}

int main() {
    checkSong("128 bpm", 128.0, 0.37, 0.0, 0.05);
    checkSong("100 bpm", 100.0, 1.10, 0.0, 0.05);
    checkSong("150 bpm", 150.0, 0.05, 0.0, 0.05);
    checkSong("174 bpm (dnb)", 174.0, 0.52, 0.0, 0.05, true);
    checkSong("140 bpm, noisy", 140.0, 0.80, 0.0, 0.25);
    checkSong("120 bpm, drifting 2%", 120.0, 0.25, 0.02, 0.05);

    // downbeats: kick is the same on every beat here, so just check every 4th is flagged
    {
        Song song = makeSong(128.0, 0.37, 30.0);
        auto tr = beats::track(song.samples.data(), song.samples.size(), song.sr);
        int flagged = 0;
        for (size_t k = 0; k < tr.downbeat.size(); ++k) flagged += tr.downbeat[k];
        CHECK(std::abs(flagged - static_cast<int>(tr.times.size() / 4)) <= 1);
    }

    // silence and noise do not invent a confident beat
    {
        std::vector<float> silence(22050 * 20, 0.f);
        auto tr = beats::track(silence.data(), silence.size(), 22050);
        CHECK(tr.times.empty() || tr.confidence < 1.5);
        std::mt19937 rng(5);
        std::normal_distribution<float> g(0.f, 0.3f);
        std::vector<float> noise(22050 * 20);
        for (auto& v : noise) v = g(rng);
        auto tn = beats::track(noise.data(), noise.size(), 22050);
        std::printf("white noise -> conf %.2f\n", tn.confidence);
        CHECK(tn.confidence < 1.5);
    }

    // too short
    {
        std::vector<float> tiny(100, 0.1f);
        CHECK(beats::track(tiny.data(), tiny.size(), 22050).times.empty());
    }

    // guidelines
    {
        auto g = beats::parseGuidelines("0.5~0~0.7~0.9~1~1~1~0.8~bad~1~");
        CHECK(g.size() == 3 && std::fabs(g[0] - 0.5) < 1e-9 && std::fabs(g[2] - 1.0) < 1e-9);
        CHECK(beats::parseGuidelines("").empty());
    }

    // fixed tempo
    {
        auto f = beats::fromTempo(120.0, 0.25, 10.0);
        CHECK(f.times.size() == 20 && std::fabs(f.times[1] - 0.75) < 1e-9 && f.downbeat[0] && !f.downbeat[1] && f.downbeat[4]);
    }

    if (failures == 0) std::printf("all beat tests passed\n");
    return failures == 0 ? 0 : 1;
}
