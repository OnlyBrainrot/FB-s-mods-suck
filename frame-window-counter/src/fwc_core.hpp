// Frame Window Counter - core.
//
// Plain C++ with no game or Geode dependency, so all of it can be unit-tested
// on any machine (see test/core_test.cpp):
//   * macro file parsing (GDR2 binary, legacy GDR JSON / MessagePack,
//     Silicate SLC2 / SLC3, .fwc.json)
//   * the frame-window counter: a step machine that drives an abstract
//     simulator, shifting one input at a time and seeing whether the run survives
//   * tallies and a precision estimate
#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fwc {

constexpr double TPS = 240.0; // physics ticks per second

struct Input {
    int tick = 0;     // physics tick (240 per second) the input happens on
    int button = 1;   // 1 = jump, 2 = left, 3 = right
    bool p2 = false;  // player 2
    bool down = true; // press or release

    bool operator==(const Input& o) const {
        return tick == o.tick && button == o.button && p2 == o.p2 && down == o.down;
    }
};

inline void sortInputs(std::vector<Input>& inputs) {
    std::stable_sort(inputs.begin(), inputs.end(), [](const Input& a, const Input& b) { return a.tick < b.tick; });
}

// ---------------------------------------------------------------------------
// A tiny dynamic value, with JSON and MessagePack readers
// ---------------------------------------------------------------------------

struct Value {
    enum Type { Null, Bool, Int, Float, String, Array, Object };
    Type type = Null;
    bool b = false;
    long long i = 0;
    double f = 0.0;
    std::string s;
    std::vector<Value> arr;
    std::vector<std::string> keys; // object members: keys[k] -> vals[k]
    std::vector<Value> vals;

    const Value* get(std::string_view key) const {
        if (type != Object) return nullptr;
        for (size_t k = 0; k < keys.size() && k < vals.size(); ++k) {
            if (keys[k] == key) return &vals[k];
        }
        return nullptr;
    }
    void set(std::string key, Value value) {
        keys.push_back(std::move(key));
        vals.push_back(std::move(value));
    }
    bool isNumber() const { return type == Int || type == Float; }
    double number(double fallback = 0.0) const {
        if (type == Int) return static_cast<double>(i);
        if (type == Float) return f;
        if (type == Bool) return b ? 1.0 : 0.0;
        return fallback;
    }
    bool truthy() const {
        if (type == Bool) return b;
        if (type == Int) return i != 0;
        if (type == Float) return f != 0.0;
        return false;
    }
};

namespace detail {

constexpr int MAX_DEPTH = 64;

struct JsonReader {
    std::string_view src;
    size_t pos = 0;
    bool ok = true;

    void skipSpace() {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\n' || src[pos] == '\r' || src[pos] == '\t')) ++pos;
    }
    bool eat(char c) {
        skipSpace();
        if (pos < src.size() && src[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    std::string parseString() {
        std::string out;
        // opening quote already consumed
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos >= src.size()) break;
            char e = src[pos++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (pos + 4 > src.size()) {
                        ok = false;
                        return out;
                    }
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = src[pos++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else ok = false;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: out += e; break; // \" \\ \/
            }
        }
        ok = false; // unterminated
        return out;
    }
    Value parse(int depth = 0) {
        Value v;
        skipSpace();
        if (depth > MAX_DEPTH || pos >= src.size()) {
            ok = false;
            return v;
        }
        char c = src[pos];
        if (c == '{') {
            ++pos;
            v.type = Value::Object;
            if (eat('}')) return v;
            while (ok) {
                if (!eat('"')) {
                    ok = false;
                    break;
                }
                std::string key = parseString();
                if (!eat(':')) {
                    ok = false;
                    break;
                }
                v.set(std::move(key), parse(depth + 1));
                if (eat(',')) continue;
                if (!eat('}')) ok = false;
                break;
            }
        } else if (c == '[') {
            ++pos;
            v.type = Value::Array;
            if (eat(']')) return v;
            while (ok) {
                v.arr.push_back(parse(depth + 1));
                if (eat(',')) continue;
                if (!eat(']')) ok = false;
                break;
            }
        } else if (c == '"') {
            ++pos;
            v.type = Value::String;
            v.s = parseString();
        } else if (src.compare(pos, 4, "true") == 0) {
            pos += 4;
            v.type = Value::Bool;
            v.b = true;
        } else if (src.compare(pos, 5, "false") == 0) {
            pos += 5;
            v.type = Value::Bool;
        } else if (src.compare(pos, 4, "null") == 0) {
            pos += 4;
        } else {
            size_t start = pos;
            bool isFloat = false;
            while (pos < src.size()) {
                char d = src[pos];
                if ((d >= '0' && d <= '9') || d == '-' || d == '+') {
                    ++pos;
                } else if (d == '.' || d == 'e' || d == 'E') {
                    isFloat = true;
                    ++pos;
                } else {
                    break;
                }
            }
            if (pos == start) {
                ok = false;
                return v;
            }
            std::string num(src.substr(start, pos - start));
            if (isFloat) {
                v.type = Value::Float;
                v.f = std::strtod(num.c_str(), nullptr);
            } else {
                v.type = Value::Int;
                v.i = std::strtoll(num.c_str(), nullptr, 10);
            }
        }
        return v;
    }
};

struct MsgpackReader {
    const std::uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool ok = true;

    bool need(size_t n) {
        if (pos + n > size) {
            ok = false;
            return false;
        }
        return true;
    }
    std::uint64_t be(size_t n) {
        if (!need(n)) return 0;
        std::uint64_t v = 0;
        for (size_t k = 0; k < n; ++k) v = (v << 8) | data[pos++];
        return v;
    }
    std::string str(size_t n) {
        if (!need(n)) return {};
        std::string out(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return out;
    }
    Value array(size_t n, int depth) {
        Value v;
        v.type = Value::Array;
        for (size_t k = 0; k < n && ok; ++k) v.arr.push_back(parse(depth + 1));
        return v;
    }
    Value map(size_t n, int depth) {
        Value v;
        v.type = Value::Object;
        for (size_t k = 0; k < n && ok; ++k) {
            Value key = parse(depth + 1);
            std::string name = key.type == Value::String ? key.s : std::to_string(key.i);
            v.set(std::move(name), parse(depth + 1));
        }
        return v;
    }
    Value parse(int depth = 0) {
        Value v;
        if (depth > MAX_DEPTH || !need(1)) {
            ok = false;
            return v;
        }
        std::uint8_t t = data[pos++];
        if (t <= 0x7f) {
            v.type = Value::Int;
            v.i = t;
        } else if (t <= 0x8f) {
            return map(t & 0x0f, depth);
        } else if (t <= 0x9f) {
            return array(t & 0x0f, depth);
        } else if (t <= 0xbf) {
            v.type = Value::String;
            v.s = str(t & 0x1f);
        } else if (t >= 0xe0) {
            v.type = Value::Int;
            v.i = static_cast<std::int8_t>(t);
        } else {
            switch (t) {
                case 0xc0: break;
                case 0xc2: v.type = Value::Bool; break;
                case 0xc3: v.type = Value::Bool; v.b = true; break;
                case 0xc4: v.type = Value::String; v.s = str(be(1)); break;
                case 0xc5: v.type = Value::String; v.s = str(be(2)); break;
                case 0xc6: v.type = Value::String; v.s = str(be(4)); break;
                case 0xc7: { size_t n = be(1); pos += need(n + 1) ? n + 1 : 0; break; }
                case 0xc8: { size_t n = be(2); pos += need(n + 1) ? n + 1 : 0; break; }
                case 0xc9: { size_t n = be(4); pos += need(n + 1) ? n + 1 : 0; break; }
                case 0xca: {
                    std::uint32_t raw = static_cast<std::uint32_t>(be(4));
                    float fl;
                    std::memcpy(&fl, &raw, 4);
                    v.type = Value::Float;
                    v.f = fl;
                    break;
                }
                case 0xcb: {
                    std::uint64_t raw = be(8);
                    std::memcpy(&v.f, &raw, 8);
                    v.type = Value::Float;
                    break;
                }
                case 0xcc: v.type = Value::Int; v.i = static_cast<long long>(be(1)); break;
                case 0xcd: v.type = Value::Int; v.i = static_cast<long long>(be(2)); break;
                case 0xce: v.type = Value::Int; v.i = static_cast<long long>(be(4)); break;
                case 0xcf: v.type = Value::Int; v.i = static_cast<long long>(be(8)); break;
                case 0xd0: v.type = Value::Int; v.i = static_cast<std::int8_t>(be(1)); break;
                case 0xd1: v.type = Value::Int; v.i = static_cast<std::int16_t>(be(2)); break;
                case 0xd2: v.type = Value::Int; v.i = static_cast<std::int32_t>(be(4)); break;
                case 0xd3: v.type = Value::Int; v.i = static_cast<std::int64_t>(be(8)); break;
                case 0xd4: pos += need(2) ? 2 : 0; break;
                case 0xd5: pos += need(3) ? 3 : 0; break;
                case 0xd6: pos += need(5) ? 5 : 0; break;
                case 0xd7: pos += need(9) ? 9 : 0; break;
                case 0xd8: pos += need(17) ? 17 : 0; break;
                case 0xd9: v.type = Value::String; v.s = str(be(1)); break;
                case 0xda: v.type = Value::String; v.s = str(be(2)); break;
                case 0xdb: v.type = Value::String; v.s = str(be(4)); break;
                case 0xdc: return array(be(2), depth);
                case 0xdd: return array(be(4), depth);
                case 0xde: return map(be(2), depth);
                case 0xdf: return map(be(4), depth);
                default: ok = false; break; // 0xc1 is never used
            }
        }
        return v;
    }
};

// Reader for the binary GDR format: variable-length integers (7 bits per byte,
// low bits first), length-prefixed strings, big-endian floats.
struct GdrReader {
    const std::uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool ok = true;

    bool empty() const { return pos >= size; }
    std::uint64_t varint() {
        std::uint64_t v = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            if (pos >= size) {
                ok = false;
                return 0;
            }
            std::uint8_t byte = data[pos++];
            if (shift < 64) v |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
            if (!(byte & 0x80)) return v;
        }
        ok = false;
        return 0;
    }
    void skip(std::uint64_t n) {
        if (n > size - pos) {
            ok = false;
            pos = size;
        } else {
            pos += static_cast<size_t>(n);
        }
    }
    void skipString() { skip(varint()); }
    double f64() {
        if (size - pos < 8) {
            ok = false;
            return 0;
        }
        std::uint64_t raw = 0;
        for (int k = 0; k < 8; ++k) raw = (raw << 8) | data[pos++];
        double d;
        std::memcpy(&d, &raw, 8);
        return d;
    }
};

// Little-endian reader for the Silicate formats.
struct LeReader {
    const std::uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool ok = true;

    size_t left() const { return size - pos; }
    std::uint64_t u(size_t bytes) {
        if (bytes > 8 || left() < bytes) {
            ok = false;
            pos = size;
            return 0;
        }
        std::uint64_t v = 0;
        for (size_t k = 0; k < bytes; ++k) v |= static_cast<std::uint64_t>(data[pos + k]) << (8 * k);
        pos += bytes;
        return v;
    }
    double f64() {
        std::uint64_t raw = u(8);
        double d;
        std::memcpy(&d, &raw, 8);
        return d;
    }
    void skip(std::uint64_t n) {
        if (n > left()) {
            ok = false;
            pos = size;
        } else {
            pos += static_cast<size_t>(n);
        }
    }
};

} // namespace detail

inline bool parseJson(std::string_view text, Value& out) {
    detail::JsonReader r{text};
    out = r.parse();
    r.skipSpace();
    return r.ok && r.pos == text.size();
}

inline bool parseMsgpack(const std::uint8_t* data, size_t size, Value& out) {
    detail::MsgpackReader r{data, size};
    out = r.parse();
    return r.ok;
}

// ---------------------------------------------------------------------------
// Macro files
// ---------------------------------------------------------------------------

struct Macro {
    double framerate = TPS;
    std::vector<Input> inputs;
    std::vector<int> windows; // parallel to inputs; 0 = not counted (only from .fwc.json)
    std::string format;
    long long levelId = 0;    // 0 = unknown
};

struct ParseResult {
    bool ok = false;
    std::string error;
    Macro macro;
};

inline int frameToTick(double frame, double framerate) {
    if (framerate <= 0.0) framerate = TPS;
    return static_cast<int>(std::llround(frame * TPS / framerate));
}

namespace detail {

inline ParseResult parseGdrBinary(const std::uint8_t* data, size_t size) {
    ParseResult res;
    GdrReader r{data, size, 3}; // after the "GDR" magic
    r.varint();                 // format version
    std::uint64_t tagLen = r.varint();
    r.skip(tagLen);             // input extension tag
    r.skipString();             // author
    r.skipString();             // description
    r.skip(4);                  // duration (float)
    r.varint();                 // game version
    double framerate = r.f64();
    r.varint();                 // seed
    r.varint();                 // coins
    r.varint();                 // ldm
    bool platformer = r.varint() != 0;
    r.skipString();             // bot name
    r.varint();                 // bot version
    const std::uint64_t levelId = r.varint();
    r.skipString();             // level name
    r.skip(r.varint());         // replay extension
    std::uint64_t deaths = r.varint();
    for (std::uint64_t k = 0; k < deaths && r.ok; ++k) r.varint();
    r.varint();                 // total input count
    std::uint64_t p1Left = r.varint();
    if (!r.ok || !(framerate > 0.0) || framerate > 1e6) {
        res.error = "The GDR file header is damaged or uses an unknown layout.";
        return res;
    }

    const bool hasInputExt = tagLen > 0;
    std::uint64_t prev = 0;
    while (!r.empty() && r.ok) {
        std::uint64_t packed = r.varint();
        Input in;
        std::uint64_t delta;
        if (platformer) {
            delta = packed >> 3;
            in.button = static_cast<int>((packed >> 1) & 3);
        } else {
            delta = packed >> 1;
            in.button = 1;
        }
        in.down = (packed & 1) != 0;
        in.p2 = p1Left == 0;
        std::uint64_t frame = prev + delta;
        in.tick = frameToTick(static_cast<double>(frame), framerate);
        if (hasInputExt) r.skip(r.varint());
        if (!r.ok) break;
        res.macro.inputs.push_back(in);
        prev = frame;
        if (p1Left > 0 && --p1Left == 0) prev = 0; // player 2's frames restart from zero
    }
    if (!r.ok) {
        res.error = "The GDR file ended unexpectedly.";
        return res;
    }
    res.macro.framerate = framerate;
    res.macro.levelId = static_cast<long long>(levelId);
    res.macro.format = "GDR2";
    res.ok = true;
    return res;
}

// Turns Silicate frames into ticks. Silicate macros can change their tick rate
// part-way through, and can contain restarts; only the last attempt is kept.
struct SlcTimeline {
    double tps = TPS;
    double baseTime = 0.0;          // seconds at baseFrame
    std::uint64_t baseFrame = 0;
    std::vector<Input> inputs;
    bool tooMany = false;

    static constexpr size_t MAX_INPUTS = 5000000;

    double timeAt(std::uint64_t frame) const {
        return baseTime + static_cast<double>(frame - baseFrame) / tps;
    }
    void player(std::uint64_t frame, int button, bool p2, bool down) {
        if (inputs.size() >= MAX_INPUTS) {
            tooMany = true;
            return;
        }
        inputs.push_back(Input{static_cast<int>(std::llround(timeAt(frame) * TPS)), button, p2, down});
    }
    void setTps(std::uint64_t frame, double newTps) {
        if (!(newTps > 0.0) || newTps > 1e6) return;
        baseTime = timeAt(frame);
        baseFrame = frame;
        tps = newTps;
    }
    void restart(std::uint64_t frame) {
        inputs.clear();
        baseTime = 0.0;
        baseFrame = frame;
    }
};

inline ParseResult finishSlc(SlcTimeline& timeline, double headerTps, const char* formatName) {
    ParseResult res;
    if (timeline.tooMany) {
        res.error = "The SLC file expands to an unreasonable number of inputs.";
        return res;
    }
    res.macro.inputs = std::move(timeline.inputs);
    res.macro.windows.assign(res.macro.inputs.size(), 0);
    res.macro.framerate = headerTps;
    res.macro.format = formatName;
    res.ok = true;
    return res;
}

// Silicate SLC version 2: "SILL", tick rate, metadata, then "blobs" of inputs
// that share a byte width. Each input packs [frame delta | type:3 | p2 | hold].
inline ParseResult parseSlc2(const std::uint8_t* data, size_t size) {
    ParseResult res;
    LeReader r{data, size, 4};
    const double tps = r.f64();
    r.skip(r.u(8)); // bot-specific metadata
    r.u(8);         // total input count
    const std::uint64_t blobCount = r.u(8);
    if (!r.ok || !(tps > 0.0) || tps > 1e6 || blobCount > r.left() / 24) {
        res.error = "The SLC file header is damaged.";
        return res;
    }

    struct Blob {
        std::uint64_t byteSize, start, length;
    };
    std::vector<Blob> blobs;
    for (std::uint64_t k = 0; k < blobCount; ++k) {
        Blob b;
        b.byteSize = r.u(8);
        b.start = r.u(8);
        b.length = r.u(8);
        blobs.push_back(b);
    }

    SlcTimeline timeline;
    timeline.tps = tps;
    std::uint64_t frame = 0;
    for (const Blob& blob : blobs) {
        if (blob.byteSize < 1 || blob.byteSize > 8 || blob.length > r.left() / blob.byteSize) {
            res.error = "The SLC file has a damaged input block.";
            return res;
        }
        for (std::uint64_t k = 0; k < blob.length && r.ok; ++k) {
            const std::uint64_t state = r.u(static_cast<size_t>(blob.byteSize));
            frame += state >> 5;
            const int type = static_cast<int>((state >> 2) & 7);
            const bool p2 = (state & 2) != 0;
            const bool hold = (state & 1) != 0;
            switch (type) {
                case 1: case 2: case 3: timeline.player(frame, type, p2, hold); break;
                case 4: case 5: timeline.restart(frame); break;
                case 7: timeline.setTps(frame, r.f64()); break;
                default: break; // 0 = skip, 6 = death marker
            }
        }
    }
    if (!r.ok || r.left() < 3 || std::memcmp(data + r.pos, "EOM", 3) != 0) {
        res.error = "The SLC file ended unexpectedly.";
        return res;
    }
    return finishSlc(timeline, tps, "SLC2");
}

// Silicate SLC version 3: "SLC3RPLY", metadata, then "atoms". The action atom
// holds sections: plain runs of inputs, repeated runs, and special actions.
inline ParseResult parseSlc3(const std::uint8_t* data, size_t size) {
    ParseResult res;
    LeReader r{data, size, 8};
    const std::uint64_t metaSize = r.u(2);
    if (!r.ok || metaSize < 8 || metaSize > r.left()) {
        res.error = "The SLC file header is damaged.";
        return res;
    }
    LeReader meta{data + r.pos, 8}; // metadata starts with the tick rate
    const double tps = meta.f64();
    r.skip(metaSize);
    if (!(tps > 0.0) || tps > 1e6) {
        res.error = "The SLC file has an invalid tick rate.";
        return res;
    }

    SlcTimeline timeline;
    timeline.tps = tps;
    bool foundActions = false;

    // the last byte of the file is a footer
    while (r.ok && r.left() > 1) {
        const std::uint64_t id = r.u(4);
        const std::uint64_t atomSize = r.u(8) & ~(0xFFull << 56); // top byte holds flags
        if (!r.ok || atomSize > r.left()) {
            res.error = "The SLC file has a damaged section.";
            return res;
        }
        if (id != 1 || foundActions) { // not the action atom: skip it
            r.skip(atomSize);
            continue;
        }
        foundActions = true;

        LeReader a{data + r.pos, static_cast<size_t>(atomSize)};
        r.skip(atomSize);
        a.u(8); // action count

        std::uint64_t frame = 0; // frame of the previous action
        auto playerState = [&](std::uint64_t delta, int button, bool p2, bool hold) {
            frame += delta;
            if (button == 0) {
                // "swift": a press and release on the same frame
                timeline.player(frame, 1, p2, true);
                timeline.player(frame, 1, p2, false);
            } else {
                timeline.player(frame, button, p2, hold);
            }
        };

        while (a.ok && a.left() >= 2 && !timeline.tooMany) {
            const std::uint64_t header = a.u(2);
            const int kind = static_cast<int>(header >> 14);
            if (kind == 0 || kind == 1) {
                const size_t byteSize = static_cast<size_t>(1) << ((header >> 12) & 3);
                const std::uint64_t length = 1ull << ((header >> 8) & 15);
                const std::uint64_t repeats = kind == 1 ? 1ull << ((header >> 3) & 31) : 1;
                if (length > a.left() / byteSize) {
                    a.ok = false;
                    break;
                }
                std::vector<std::uint64_t> states;
                states.reserve(static_cast<size_t>(length));
                for (std::uint64_t k = 0; k < length; ++k) states.push_back(a.u(byteSize));
                for (std::uint64_t rep = 0; rep < repeats && !timeline.tooMany; ++rep) {
                    for (std::uint64_t state : states) {
                        playerState(state >> 4, static_cast<int>((state >> 2) & 3), (state & 2) != 0, (state & 1) != 0);
                    }
                }
            } else if (kind == 2) {
                const int special = static_cast<int>((header >> 10) & 15);
                frame += a.u(static_cast<size_t>(1) << ((header >> 8) & 3));
                switch (special) {
                    case 0: case 1: a.u(8); timeline.restart(frame); break; // restart (+ seed)
                    case 2: a.u(8); break;                                  // death marker (+ seed)
                    case 3: timeline.setTps(frame, a.f64()); break;
                    case 4: break;                                          // bugpoint marker
                    default: a.ok = false; break;
                }
            } else {
                a.ok = false;
            }
        }
        if (!a.ok) {
            res.error = "The SLC file has damaged input data.";
            return res;
        }
    }
    if (!r.ok || !foundActions) {
        res.error = foundActions ? "The SLC file ended unexpectedly." : "The SLC file contains no inputs.";
        return res;
    }
    return finishSlc(timeline, tps, "SLC3");
}

inline ParseResult parseTree(const Value& root, const char* formatName) {
    ParseResult res;
    if (root.type != Value::Object) {
        res.error = "The macro file does not contain an object at the top level.";
        return res;
    }
    const Value* inputs = root.get("inputs");
    if (!inputs || inputs->type != Value::Array) {
        res.error = "The macro file has no \"inputs\" list.";
        return res;
    }

    const Value* fmt = root.get("format");
    const bool isFwc = fmt && fmt->type == Value::String && fmt->s == "fwc";

    double framerate = TPS;
    if (const Value* fr = root.get("framerate"); fr && fr->isNumber() && fr->number() > 0) framerate = fr->number();

    if (const Value* level = root.get("level")) {
        if (const Value* id = level->get("id"); id && id->isNumber()) res.macro.levelId = static_cast<long long>(id->number());
    }
    if (const Value* id = root.get("levelId"); id && id->isNumber()) res.macro.levelId = static_cast<long long>(id->number());

    for (const Value& item : inputs->arr) {
        if (item.type != Value::Object) continue;
        Input in;
        if (const Value* t = item.get("tick"); t && t->isNumber()) {
            in.tick = static_cast<int>(std::llround(t->number()));
        } else if (const Value* f = item.get("frame"); f && f->isNumber()) {
            in.tick = frameToTick(f->number(), framerate);
        } else {
            continue;
        }
        if (const Value* b = item.get("btn"); b && b->isNumber()) in.button = static_cast<int>(b->number());
        if (const Value* b = item.get("button"); b && b->isNumber()) in.button = static_cast<int>(b->number());
        if (const Value* p = item.get("2p")) in.p2 = p->truthy();
        if (const Value* p = item.get("p2")) in.p2 = p->truthy();
        if (const Value* d = item.get("down")) in.down = d->truthy();
        if (in.button < 1 || in.button > 3) in.button = 1;
        int window = 0;
        if (const Value* w = item.get("window"); w && w->isNumber()) window = static_cast<int>(w->number());
        res.macro.inputs.push_back(in);
        res.macro.windows.push_back(std::max(window, 0));
    }

    // keep windows attached to their inputs while sorting
    std::vector<size_t> order(res.macro.inputs.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return res.macro.inputs[a].tick < res.macro.inputs[b].tick;
    });
    std::vector<Input> sortedInputs;
    std::vector<int> sortedWindows;
    for (size_t k : order) {
        sortedInputs.push_back(res.macro.inputs[k]);
        sortedWindows.push_back(res.macro.windows[k]);
    }
    res.macro.inputs = std::move(sortedInputs);
    res.macro.windows = std::move(sortedWindows);
    res.macro.framerate = framerate;
    res.macro.format = isFwc ? "FWC" : formatName;
    res.ok = true;
    return res;
}

} // namespace detail

// Detects the format from the file contents.
inline ParseResult parseMacro(const std::uint8_t* data, size_t size) {
    ParseResult res;
    if (!data || size < 2) {
        res.error = "The macro file is empty.";
        return res;
    }

    size_t first = 0;
    while (first < size && (data[first] == ' ' || data[first] == '\n' || data[first] == '\r' || data[first] == '\t')) ++first;
    // UTF-8 byte order mark
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) first = 3;

    if (first < size && data[first] == '{') {
        Value root;
        std::string_view text(reinterpret_cast<const char*>(data) + first, size - first);
        if (!parseJson(text, root)) {
            res.error = "The macro file looks like JSON but could not be read.";
            return res;
        }
        res = detail::parseTree(root, "GDR JSON");
    } else if (size >= 8 && std::memcmp(data, "SLC3RPLY", 8) == 0) {
        res = detail::parseSlc3(data, size);
    } else if (size >= 4 && std::memcmp(data, "SILL", 4) == 0) {
        res = detail::parseSlc2(data, size);
    } else if (size >= 3 && data[0] == 'G' && data[1] == 'D' && data[2] == 'R') {
        res = detail::parseGdrBinary(data, size);
        if (res.ok) res.macro.windows.assign(res.macro.inputs.size(), 0);
    } else {
        Value root;
        if (!parseMsgpack(data, size, root) || root.type != Value::Object) {
            res.error = "Unrecognised macro format. Supported: .gdr / .gdr2, GDR .json, Silicate .slc, and .fwc.json.";
            return res;
        }
        res = detail::parseTree(root, "GDR MessagePack");
    }

    if (res.ok) {
        if (res.macro.inputs.empty()) {
            res.ok = false;
            res.error = "The macro has no inputs.";
            return res;
        }
        // binary GDR stores inputs per player, and tick-rate changes can reorder
        // nothing but rounding; a stable sort by time is right for every format
        sortInputs(res.macro.inputs);
    }
    return res;
}

// The mod's own save format: the inputs (in ticks) with their counted windows.
inline std::string writeFwcJson(
    const std::vector<Input>& inputs, const std::vector<int>& windows, int maxWindow, int horizon, long long levelId = 0
) {
    std::string out;
    out += "{\n  \"format\": \"fwc\",\n  \"version\": 1,\n  \"framerate\": 240,\n";
    out += "  \"levelId\": " + std::to_string(levelId) + ",\n";
    out += "  \"maxWindow\": " + std::to_string(maxWindow) + ",\n";
    out += "  \"horizon\": " + std::to_string(horizon) + ",\n";
    out += "  \"inputs\": [\n";
    for (size_t k = 0; k < inputs.size(); ++k) {
        const Input& in = inputs[k];
        const int w = k < windows.size() ? windows[k] : 0;
        char line[160];
        std::snprintf(
            line, sizeof(line), "    {\"tick\": %d, \"button\": %d, \"p2\": %s, \"down\": %s, \"window\": %d}%s\n",
            in.tick, in.button, in.p2 ? "true" : "false", in.down ? "true" : "false", w,
            k + 1 < inputs.size() ? "," : ""
        );
        out += line;
    }
    out += "  ]\n}\n";
    return out;
}

// Copies windows from a saved .fwc.json onto a freshly loaded macro, matching
// inputs by (tick, button, player, press/release). Returns how many matched.
inline size_t applySavedWindows(const Macro& saved, const std::vector<Input>& inputs, std::vector<int>& windows) {
    std::map<std::tuple<int, int, bool, bool>, int> lookup;
    for (size_t k = 0; k < saved.inputs.size() && k < saved.windows.size(); ++k) {
        const Input& in = saved.inputs[k];
        if (saved.windows[k] > 0) lookup[{in.tick, in.button, in.p2, in.down}] = saved.windows[k];
    }
    windows.assign(inputs.size(), 0);
    size_t matched = 0;
    for (size_t k = 0; k < inputs.size(); ++k) {
        const Input& in = inputs[k];
        auto it = lookup.find({in.tick, in.button, in.p2, in.down});
        if (it != lookup.end()) {
            windows[k] = it->second;
            ++matched;
        }
    }
    return matched;
}

// ---------------------------------------------------------------------------
// Counting frame windows
// ---------------------------------------------------------------------------

enum class StepResult { Alive, Dead, Complete };

// What the counter needs from the game. The mod implements this on top of the
// real level; the tests implement it with a toy model.
struct Sim {
    virtual ~Sim() = default;
    virtual void restart() = 0;        // back to the newest checkpoint, or the level start if there is none
    virtual int tick() = 0;            // ticks simulated since the level start
    virtual void input(const Input&) = 0;
    virtual StepResult step() = 0;     // advance one tick
    virtual void checkpoint() = 0;     // save the current state on a stack
    virtual void dropCheckpoint() = 0; // discard the newest saved state
};

struct CountOptions {
    int maxWindow = 10;         // stop widening at this many frames ("10 or more")
    int horizon = 48;           // ticks the run must survive past the following input
    int checkpointGap = 240;    // minimum ticks between checkpoints
    bool useCheckpoints = true; // false = replay every trial from the level start (slow, safest)
    bool countReleases = true;
};

// For each input: move it later, then earlier, one tick at a time with every
// other input left alone, and count how many positions still survive.
class Counter {
public:
    Counter(std::vector<Input> inputs, CountOptions options)
        : m_in(std::move(inputs)), m_opt(options) {
        sortInputs(m_in);
        m_win.assign(m_in.size(), 0);
        if (m_opt.maxWindow < 1) m_opt.maxWindow = 1;
        if (m_opt.horizon < 1) m_opt.horizon = 1;
        if (m_opt.checkpointGap < 1) m_opt.checkpointGap = 1;

        // how many buttons are held after each input, to find "hands off" ticks
        std::map<std::pair<bool, int>, bool> held;
        int count = 0;
        m_heldAfter.reserve(m_in.size());
        for (const Input& in : m_in) {
            bool& h = held[{in.p2, in.button}];
            if (in.down && !h) ++count;
            if (!in.down && h) --count;
            h = in.down;
            m_heldAfter.push_back(count);
        }
        m_lastTick = m_in.empty() ? 0 : m_in.back().tick;
    }

    // Simulates at most `budget` ticks. Returns true while there is more to do.
    bool advance(Sim& sim, int budget) {
        while (budget > 0 && !finished()) {
            if (!m_active) {
                plan(sim);
                continue;
            }
            const int t = sim.tick();
            if (t >= m_until) {
                endRun(sim, true);
                continue;
            }
            dispatch(sim, t);
            const StepResult r = sim.step();
            --budget;
            if (r == StepResult::Dead) {
                m_deathTick = sim.tick();
                endRun(sim, false);
            } else if (r == StepResult::Complete) {
                endRun(sim, true);
            }
        }
        return !finished();
    }

    bool finished() const { return m_phase == Phase::Done || m_phase == Phase::Failed; }
    bool failed() const { return m_phase == Phase::Failed; }
    const std::string& error() const { return m_error; }
    const std::vector<int>& windows() const { return m_win; }
    const std::vector<Input>& inputs() const { return m_in; }
    size_t current() const { return m_i; }
    size_t checkpoints() const { return m_anchors.size(); }
    bool usingCheckpoints() const { return m_opt.useCheckpoints; }
    double progress() const {
        if (m_in.empty() || m_phase == Phase::Done) return 1.0;
        return static_cast<double>(m_i) / static_cast<double>(m_in.size());
    }

private:
    enum class Phase { Start, Baseline, Inputs, Done, Failed };
    enum class Stage { Anchor, Verify, Later, Earlier };
    enum class Kind { Baseline, Advance, Verify, Trial };

    void fail(std::string message) {
        m_phase = Phase::Failed;
        m_error = std::move(message);
        m_active = false;
    }

    int anchorTick() const { return m_anchors.empty() ? 0 : m_anchors.back(); }

    int heldBefore(int tick) const {
        // inputs strictly before `tick`
        auto it = std::lower_bound(m_in.begin(), m_in.end(), tick, [](const Input& in, int t) { return in.tick < t; });
        size_t k = static_cast<size_t>(it - m_in.begin());
        return k == 0 ? 0 : m_heldAfter[k - 1];
    }

    int prevSame(size_t i) const {
        for (size_t k = i; k-- > 0;) {
            if (m_in[k].p2 == m_in[i].p2 && m_in[k].button == m_in[i].button) return m_in[k].tick;
        }
        return -1;
    }
    int nextSame(size_t i) const {
        for (size_t k = i + 1; k < m_in.size(); ++k) {
            if (m_in[k].p2 == m_in[i].p2 && m_in[k].button == m_in[i].button) return m_in[k].tick;
        }
        return INT_MAX;
    }
    // the run has to survive until this tick for the trial to pass
    int horizonFor(size_t i, int shift) const {
        const int f = m_in[i].tick;
        int next = f;
        for (size_t k = i + 1; k < m_in.size(); ++k) {
            if (m_in[k].tick > f) {
                next = m_in[k].tick;
                break;
            }
        }
        return std::max({f, f + shift, next}) + m_opt.horizon;
    }
    bool validShift(size_t i, int shift) const {
        const int t = m_in[i].tick + shift;
        // an input can't cross its neighbours on the same button, or leave the replayed stretch
        return t >= 0 && t >= anchorTick() && t > prevSame(i) && t < nextSame(i);
    }

    // latest "hands off" tick far enough before input i to make a new checkpoint worthwhile
    int findAnchor(size_t i) const {
        if (!m_opt.useCheckpoints || m_skipAnchor) return -1;
        const int hi = m_in[i].tick - m_opt.maxWindow - 2;
        const int lo = anchorTick() + m_opt.checkpointGap;
        for (int t = hi; t >= lo; --t) {
            if (heldBefore(t) == 0) return t;
        }
        return -1;
    }

    void startRun(Sim& sim, Kind kind, long shiftIndex, int shift, int until) {
        sim.restart();
        const int t0 = sim.tick();
        auto it = std::lower_bound(m_in.begin(), m_in.end(), t0, [](const Input& in, int t) { return in.tick < t; });
        m_next = static_cast<size_t>(it - m_in.begin());
        m_shiftIndex = shiftIndex;
        m_shifted = shiftIndex >= 0 ? m_in[static_cast<size_t>(shiftIndex)] : Input{};
        m_shifted.tick += shift;
        m_shiftDone = shiftIndex < 0 || static_cast<size_t>(shiftIndex) < m_next;
        m_until = until;
        m_kind = kind;
        m_active = true;
    }

    void dispatch(Sim& sim, int t) {
        while (m_next < m_in.size()) {
            if (static_cast<long>(m_next) == m_shiftIndex) {
                ++m_next; // the moved input is sent separately
                continue;
            }
            if (m_in[m_next].tick > t) break;
            sim.input(m_in[m_next]);
            ++m_next;
        }
        if (!m_shiftDone && m_shifted.tick <= t) {
            sim.input(m_shifted);
            m_shiftDone = true;
        }
    }

    void dropAnchor(Sim& sim) {
        sim.dropCheckpoint();
        m_anchors.pop_back();
        m_skipAnchor = true;
        if (++m_badAnchors >= 3) {
            // checkpoints keep breaking the run: stop using them altogether
            while (!m_anchors.empty()) {
                sim.dropCheckpoint();
                m_anchors.pop_back();
            }
            m_opt.useCheckpoints = false;
        }
    }

    void finishInput() {
        m_win[m_i] = std::min(1 + m_later + m_earlier, m_opt.maxWindow);
        nextInput();
    }
    void nextInput() {
        ++m_i;
        m_stage = Stage::Anchor;
        m_later = m_earlier = 0;
        m_skipAnchor = false;
    }

    void plan(Sim& sim) {
        switch (m_phase) {
            case Phase::Start:
                if (m_in.empty()) {
                    m_phase = Phase::Done;
                    return;
                }
                m_phase = Phase::Baseline;
                startRun(sim, Kind::Baseline, -1, 0, m_lastTick + m_opt.horizon + 1);
                return;
            case Phase::Inputs:
                break;
            default:
                return;
        }

        if (m_i >= m_in.size()) {
            m_phase = Phase::Done;
            return;
        }
        if (!m_in[m_i].down && !m_opt.countReleases) {
            nextInput();
            return;
        }

        const int total = 1 + m_later + m_earlier;
        switch (m_stage) {
            case Stage::Anchor: {
                const int target = findAnchor(m_i);
                if (target > 0) {
                    m_anchorTarget = target;
                    startRun(sim, Kind::Advance, -1, 0, target);
                } else {
                    m_stage = Stage::Verify;
                }
                break;
            }
            case Stage::Verify:
                startRun(sim, Kind::Verify, -1, 0, horizonFor(m_i, 0));
                break;
            case Stage::Later:
                if (total >= m_opt.maxWindow || !validShift(m_i, m_shift)) {
                    m_stage = Stage::Earlier;
                    m_shift = -1;
                } else {
                    startRun(sim, Kind::Trial, static_cast<long>(m_i), m_shift, horizonFor(m_i, m_shift));
                }
                break;
            case Stage::Earlier:
                if (total >= m_opt.maxWindow || !validShift(m_i, m_shift)) {
                    finishInput();
                } else {
                    startRun(sim, Kind::Trial, static_cast<long>(m_i), m_shift, horizonFor(m_i, m_shift));
                }
                break;
        }
    }

    void endRun(Sim& sim, bool survived) {
        m_active = false;
        switch (m_kind) {
            case Kind::Baseline:
                if (!survived) {
                    fail("The macro dies at tick " + std::to_string(m_deathTick) +
                         " when played back unchanged, so its windows can't be counted. "
                         "Check the frame offset setting and that this is the right level.");
                    return;
                }
                m_phase = Phase::Inputs;
                m_i = 0;
                m_stage = Stage::Anchor;
                break;

            case Kind::Advance:
                if (survived && sim.tick() == m_anchorTarget) {
                    sim.checkpoint();
                    m_anchors.push_back(m_anchorTarget);
                    m_stage = Stage::Verify;
                } else if (survived) {
                    // overshot or the level ended: just carry on without a new checkpoint
                    m_skipAnchor = true;
                    m_stage = Stage::Verify;
                } else if (!m_anchors.empty()) {
                    dropAnchor(sim); // the checkpoint we replayed from is not faithful
                } else {
                    fail("The macro survived once but died at tick " + std::to_string(m_deathTick) +
                         " on a second identical playback: the level is not deterministic.");
                }
                break;

            case Kind::Verify:
                if (survived) {
                    m_stage = Stage::Later;
                    m_shift = 1;
                } else if (!m_anchors.empty()) {
                    dropAnchor(sim); // retry this input from an older checkpoint
                } else {
                    fail("The macro survived once but died at tick " + std::to_string(m_deathTick) +
                         " on a second identical playback: the level is not deterministic.");
                }
                break;

            case Kind::Trial:
                if (m_stage == Stage::Later) {
                    if (survived) {
                        ++m_later;
                        ++m_shift;
                    } else {
                        m_stage = Stage::Earlier;
                        m_shift = -1;
                    }
                } else {
                    if (survived) {
                        ++m_earlier;
                        --m_shift;
                    } else {
                        finishInput();
                    }
                }
                break;
        }
    }

    std::vector<Input> m_in;
    CountOptions m_opt;
    std::vector<int> m_win;
    std::vector<int> m_heldAfter;
    int m_lastTick = 0;

    Phase m_phase = Phase::Start;
    std::string m_error;

    // current input
    size_t m_i = 0;
    Stage m_stage = Stage::Anchor;
    int m_shift = 1;
    int m_later = 0;
    int m_earlier = 0;
    bool m_skipAnchor = false;

    // checkpoints
    std::vector<int> m_anchors;
    int m_anchorTarget = 0;
    int m_badAnchors = 0;

    // current run
    bool m_active = false;
    Kind m_kind = Kind::Baseline;
    size_t m_next = 0;
    long m_shiftIndex = -1;
    Input m_shifted;
    bool m_shiftDone = true;
    int m_until = 0;
    int m_deathTick = 0;
};

// ---------------------------------------------------------------------------
// Tallies and precision
// ---------------------------------------------------------------------------

// counts[w] = number of inputs with window w, for w in 1..maxWindow (index 0 unused)
inline std::vector<int> tally(const std::vector<int>& windows, int maxWindow) {
    std::vector<int> counts(static_cast<size_t>(std::max(maxWindow, 1)) + 1, 0);
    for (int w : windows) {
        if (w >= 1 && w <= maxWindow) counts[static_cast<size_t>(w)]++;
    }
    return counts;
}

// Precision estimate. Model: a player's timing error is normally distributed
// with standard deviation 1/L seconds; a timing with a window of w seconds is
// passed when the error is within +-w/2. The estimate is the L for which the
// expected time to complete the level (attempts end at the first failed
// timing) equals `targetHours`. Higher = the level demands more precise play.
// Windows of 0 (uncounted) or >= maxWindow ("free") are ignored.
inline double expectedHours(
    const std::vector<Input>& inputs, const std::vector<int>& windows, int maxWindow, double levelSeconds,
    double respawnSeconds, double L
) {
    double reach = 1.0;                // chance of getting this far in an attempt
    double attempt = respawnSeconds;   // expected length of one attempt
    double lastTime = 0.0;
    for (size_t k = 0; k < inputs.size() && k < windows.size(); ++k) {
        const int w = windows[k];
        if (w < 1 || w >= maxWindow) continue;
        const double t = inputs[k].tick / TPS;
        attempt += reach * std::max(t - lastTime, 0.0);
        lastTime = t;
        const double windowSeconds = w / TPS;
        reach *= std::erf(windowSeconds * L / (2.0 * std::sqrt(2.0)));
        if (reach <= 0.0) return INFINITY;
    }
    attempt += reach * std::max(levelSeconds - lastTime, 0.0);
    return attempt / reach / 3600.0;
}

inline double precisionEstimate(
    const std::vector<Input>& inputs, const std::vector<int>& windows, int maxWindow, double levelSeconds,
    double respawnSeconds = 1.0, double targetHours = 24.0
) {
    bool any = false;
    for (int w : windows) any = any || (w >= 1 && w < maxWindow);
    if (!any) return 0.0;

    double lo = 1e-3, hi = 1e7; // expected time falls as L rises
    if (expectedHours(inputs, windows, maxWindow, levelSeconds, respawnSeconds, lo) <= targetHours) return lo;
    for (int k = 0; k < 200; ++k) {
        const double mid = std::sqrt(lo * hi);
        if (expectedHours(inputs, windows, maxWindow, levelSeconds, respawnSeconds, mid) > targetHours) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return hi;
}

} // namespace fwc
