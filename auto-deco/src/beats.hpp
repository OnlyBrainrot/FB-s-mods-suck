// Auto Deco - beat tracking.
//
// Finds the beats of a song from its audio, so decoration can pulse in time.
// Plain C++, no game dependency (tested in test/core_test.cpp).
//
// Method (standard onset-envelope + dynamic-programming tracker):
//   1. an onset envelope from rises in loudness, weighted towards the bass
//   2. tempo from the autocorrelation of that envelope, with a mild preference
//      for tempos around 120 BPM
//   3. beat times by dynamic programming: strong onsets, evenly spaced
//   4. downbeats: the beat-of-four with the most bass
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace beats {

struct Track {
    double bpm = 0.0;
    double confidence = 0.0;      // roughly 1 = no clear beat, 3+ = clear beat
    std::vector<double> times;    // seconds from the start of the audio
    std::vector<bool> downbeat;   // parallel to times
};

namespace detail {

inline std::vector<double> movingAverage(const std::vector<double>& v, int radius) {
    std::vector<double> out(v.size(), 0.0);
    if (v.empty()) return out;
    std::vector<double> prefix(v.size() + 1, 0.0);
    for (size_t k = 0; k < v.size(); ++k) prefix[k + 1] = prefix[k] + v[k];
    for (size_t k = 0; k < v.size(); ++k) {
        const size_t lo = k >= static_cast<size_t>(radius) ? k - radius : 0;
        const size_t hi = std::min(v.size(), k + radius + 1);
        out[k] = (prefix[hi] - prefix[lo]) / static_cast<double>(hi - lo);
    }
    return out;
}

} // namespace detail

constexpr double FRAME_SECONDS = 0.01; // target envelope resolution

// Onset strength per ~10 ms frame. `frameSeconds` receives the exact frame
// length (a whole number of samples), `bass` the bass-only part.
inline std::vector<double> onsetEnvelope(
    const float* x, size_t n, double sampleRate, double* frameSeconds = nullptr, std::vector<double>* bass = nullptr
) {
    std::vector<double> env;
    if (!x || n == 0 || sampleRate <= 0) return env;
    const size_t hop = std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate * FRAME_SECONDS)));
    if (frameSeconds) *frameSeconds = static_cast<double>(hop) / sampleRate;
    const size_t frames = n / hop;
    if (frames < 4) return env;

    // one-pole low-pass around 150 Hz for the kick/bass band
    const double a = std::exp(-2.0 * M_PI * 150.0 / sampleRate);
    std::vector<double> lowE(frames, 0.0), allE(frames, 0.0);
    double lp = 0.0;
    for (size_t f = 0; f < frames; ++f) {
        double sumLow = 0.0, sumAll = 0.0;
        for (size_t k = f * hop; k < (f + 1) * hop; ++k) {
            const double s = x[k];
            lp = (1.0 - a) * s + a * lp;
            sumLow += lp * lp;
            sumAll += s * s;
        }
        lowE[f] = std::log1p(1000.0 * sumLow / hop);
        allE[f] = std::log1p(1000.0 * sumAll / hop);
    }

    env.assign(frames, 0.0);
    std::vector<double> low(frames, 0.0);
    for (size_t f = 1; f < frames; ++f) {
        low[f] = std::max(0.0, lowE[f] - lowE[f - 1]);
        env[f] = low[f] + 0.5 * std::max(0.0, allE[f] - allE[f - 1]);
    }

    // remove slow changes, keep the peaks
    const int radius = static_cast<int>(0.25 * sampleRate / hop);
    auto mean = detail::movingAverage(env, radius);
    double sumSq = 0.0;
    for (size_t f = 0; f < frames; ++f) {
        env[f] = std::max(0.0, env[f] - mean[f]);
        sumSq += env[f] * env[f];
    }
    const double sd = std::sqrt(sumSq / frames);
    if (sd > 0) {
        for (auto& v : env) v /= sd;
    }
    if (bass) *bass = std::move(low);
    return env;
}

// Tempo in frames per beat, or 0 if the envelope is too short.
inline double estimatePeriod(
    const std::vector<double>& env, double frameSeconds, double minBpm, double maxBpm, double* confidence = nullptr
) {
    if (frameSeconds <= 0) return 0.0;
    const double fps = 1.0 / frameSeconds;
    const int minLag = std::max(1, static_cast<int>(std::floor(fps * 60.0 / maxBpm)));
    const int maxLag = static_cast<int>(std::ceil(fps * 60.0 / minBpm));
    if (static_cast<int>(env.size()) < maxLag * 4) return 0.0;

    std::vector<double> r(maxLag + 2, 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag) {
        if (lag < 1) continue;
        double s = 0.0;
        for (size_t t = 0; t + lag < env.size(); ++t) s += env[t] * env[t + lag];
        r[lag] = s / static_cast<double>(env.size() - lag);
    }

    // a beat period also lines up at twice the lag, so reward that too,
    // and prefer tempos near 120 BPM (one octave either way is mild)
    int best = 0;
    double bestScore = -1.0, total = 0.0;
    int count = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double doubled = 2 * lag <= maxLag + 1 ? r[2 * lag] : 0.0;
        const double bpm = 60.0 * fps / lag;
        const double prior = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0) / 1.0, 2.0));
        const double score = (r[lag] + 0.5 * doubled) * prior;
        total += r[lag];
        ++count;
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    if (best == 0) return 0.0;

    if (confidence) *confidence = count > 0 && total > 0 ? r[best] / (total / count) : 0.0;

    // parabolic refinement of the peak
    double period = best;
    const double y0 = r[best - 1], y1 = r[best], y2 = r[best + 1];
    const double denom = y0 - 2 * y1 + y2;
    if (std::fabs(denom) > 1e-12) {
        const double delta = 0.5 * (y0 - y2) / denom;
        if (std::fabs(delta) < 1.0) period += delta;
    }
    return period;
}

// Beat frames by dynamic programming over the envelope.
inline std::vector<size_t> trackFrames(const std::vector<double>& env, double period, double tightness = 100.0) {
    std::vector<size_t> beats;
    const size_t n = env.size();
    if (n == 0 || period < 2) return beats;

    std::vector<double> score(n, 0.0);
    std::vector<long> back(n, -1);
    const long lo = static_cast<long>(std::floor(period / 2));
    const long hi = static_cast<long>(std::ceil(period * 2));
    for (size_t t = 0; t < n; ++t) {
        double bestPrev = 0.0;
        long bestIdx = -1;
        for (long d = lo; d <= hi; ++d) {
            const long p = static_cast<long>(t) - d;
            if (p < 0) break;
            const double penalty = std::log(static_cast<double>(d) / period);
            const double s = score[static_cast<size_t>(p)] - tightness * penalty * penalty;
            if (bestIdx < 0 || s > bestPrev) {
                bestPrev = s;
                bestIdx = p;
            }
        }
        score[t] = env[t] + (bestIdx >= 0 ? std::max(bestPrev, 0.0) : 0.0);
        back[t] = bestIdx >= 0 && bestPrev > 0.0 ? bestIdx : -1;
    }

    // best ending within the last beat
    size_t end = n - 1;
    double bestEnd = -1e300;
    const size_t from = n > static_cast<size_t>(period) ? n - static_cast<size_t>(period) : 0;
    for (size_t t = from; t < n; ++t) {
        if (score[t] > bestEnd) {
            bestEnd = score[t];
            end = t;
        }
    }
    for (long t = static_cast<long>(end); t >= 0; t = back[static_cast<size_t>(t)]) beats.push_back(static_cast<size_t>(t));
    std::reverse(beats.begin(), beats.end());

    // the chain can stop early if the intro is quiet: extend it backwards
    if (!beats.empty()) {
        double t = static_cast<double>(beats.front()) - period;
        std::vector<size_t> head;
        while (t >= 0) {
            head.push_back(static_cast<size_t>(std::lround(t)));
            t -= period;
        }
        std::reverse(head.begin(), head.end());
        beats.insert(beats.begin(), head.begin(), head.end());
    }
    return beats;
}

// Full pipeline. `x` is mono audio.
inline Track track(const float* x, size_t n, double sampleRate, double minBpm = 70.0, double maxBpm = 190.0) {
    Track out;
    std::vector<double> bass;
    double frameSeconds = 0.0;
    auto env = onsetEnvelope(x, n, sampleRate, &frameSeconds, &bass);
    if (env.empty()) return out;

    double confidence = 0.0;
    const double period = estimatePeriod(env, frameSeconds, minBpm, maxBpm, &confidence);
    if (period <= 0) return out;

    out.bpm = 60.0 / (period * frameSeconds);
    out.confidence = confidence;
    auto frames = trackFrames(env, period);

    // which of every four beats carries the most bass
    double phaseScore[4] = {0, 0, 0, 0};
    for (size_t k = 0; k < frames.size(); ++k) {
        const size_t f = frames[k];
        double s = 0.0;
        for (size_t g = f > 2 ? f - 2 : 0; g <= f + 2 && g < bass.size(); ++g) s = std::max(s, bass[g]);
        phaseScore[k % 4] += s;
    }
    const int phase = static_cast<int>(std::max_element(phaseScore, phaseScore + 4) - phaseScore);

    // an onset shows up in the frame that contains it: report the frame's middle
    for (size_t k = 0; k < frames.size(); ++k) {
        out.times.push_back((static_cast<double>(frames[k]) + 0.5) * frameSeconds);
        out.downbeat.push_back(static_cast<int>(k % 4) == phase);
    }
    return out;
}

// Guidelines saved in the level: "time~colour~time~colour~..."
inline std::vector<double> parseGuidelines(const std::string& s) {
    std::vector<double> times;
    size_t pos = 0;
    int field = 0;
    while (pos <= s.size()) {
        size_t next = s.find('~', pos);
        if (next == std::string::npos) next = s.size();
        if (field % 2 == 0 && next > pos) {
            const std::string token = s.substr(pos, next - pos);
            char* end = nullptr;
            const double v = std::strtod(token.c_str(), &end);
            if (end && end != token.c_str() && std::isfinite(v) && v >= 0) times.push_back(v);
        }
        ++field;
        pos = next + 1;
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return std::fabs(a - b) < 1e-4; }), times.end());
    return times;
}

// Evenly spaced beats from a tempo and the time of the first beat.
inline Track fromTempo(double bpm, double firstBeat, double duration) {
    Track out;
    if (!(bpm > 0) || duration <= 0) return out;
    out.bpm = bpm;
    out.confidence = 0;
    const double step = 60.0 / bpm;
    int k = 0;
    for (double t = firstBeat; t < duration; t += step, ++k) {
        if (t < 0) continue;
        out.times.push_back(t);
        out.downbeat.push_back(k % 4 == 0);
    }
    return out;
}

} // namespace beats
