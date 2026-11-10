#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
static std::mutex logMutex;
static std::atomic<bool> shouldStop{false};

static void send(const std::string& text) {
    std::lock_guard lk(logMutex);
    std::cout << text << '\n' << std::flush;
}
static std::string quoted(const std::string& v) {
    std::ostringstream o; o << '"';
    for (unsigned char c : v) {
        switch (c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default: if (c < 32) { o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c << std::dec; }
                 else o << (char)c;
        }
    }
    return o.str() + '"';
}
static std::string err(const std::string& msg) { return "{\"type\":\"error\",\"message\":" + quoted(msg) + "}"; }
struct Args {
    std::map<std::string, std::string> entries;
    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string k = argv[i];
            if (k.rfind("--", 0) != 0) throw std::runtime_error("Unknown option " + k);
            if (i + 1 >= argc || std::string(argv[i+1]).rfind("--", 0) == 0) throw std::runtime_error("Missing value for " + k);
            entries[k] = argv[++i];
        }
    }
    std::string get(const std::string& key, const std::string& def = "") const {
        if (auto it = entries.find(key); it != entries.end()) return it->second;
        return def;
    }
    int64_t integer(const std::string& key, int64_t def, int64_t min, int64_t max) const {
        std::string raw = get(key); if (raw.empty()) return def;
        size_t pos = 0; long long n = std::stoll(raw, &pos);
        if (pos != raw.size() || n < min || n > max) throw std::runtime_error("Invalid " + key);
        return n;
    }
};
static const std::vector<double> kMockRates{1000, 10000, 20000, 50000, 125000, 250000, 500000, 1000000};
static std::vector<int> channelsFromMask(int mask) {
    if (mask < 1 || mask > 15) throw std::runtime_error("Channel mask must be 1..15");
    std::vector<int> ch;
    for (int i = 0; i < 4; ++i) if ((mask >> i) & 1) ch.push_back(i);
    return ch;
}
static std::vector<std::string> split(const std::string& s, char sep) {
    std::stringstream ss(s); std::vector<std::string> result; std::string item;
    while (std::getline(ss, item, sep)) result.push_back(item);
    return result;
}
static std::string nowStamp() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream o; o << std::put_time(&tm, "%Y%m%d_%H%M%S"); return o.str();
}
static fs::path fromUtf8(const std::string& text) {
    std::u8string u8;
    u8.reserve(text.size());
    for (unsigned char c : text) u8.push_back(static_cast<char8_t>(c));
    return fs::path(u8);
}
static std::string utf8path(const fs::path& p) {
#ifdef _WIN32
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
#else
    return p.string();
#endif
}
static uint64_t nowMillis() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
// Canonical payload: interleaved little-endian float32 VOLTS in ascending enabled-channel order.
// Float volt conversion in the legacy adapter is provided by libwave.dll, NOT verified ADC calibration.
#pragma pack(push, 1)
struct Header {
    char magic[8]{'Z','W','R','A','W','0','1','\0'};
    uint32_t version = 1;
    uint32_t channelMask = 0;
    uint32_t channelCount = 0;
    uint32_t sampleFormat = 1; // 1 = interleaved float32 volts, little endian
    double sampleRateHz = 0;
    uint64_t startIndex = 0;
    uint64_t sampleCount = 0;
    uint64_t startUnixMs = 0;
    uint8_t reserved[8]{};
};
#pragma pack(pop)
static_assert(sizeof(Header) == 64);

class Device {
public:
    virtual ~Device() = default;
    virtual double connect(double requestRateHz, int mask) = 0;
    virtual size_t acquire(size_t maxFrames, std::vector<float>& interleaved) = 0;
    virtual std::string source() const = 0;
    virtual std::string continuity() const = 0;
};
class MockDevice : public Device {
    double rate_ = 1e6;
    std::vector<int> channels_;
    uint64_t index_ = 0;
    uint32_t random_ = 0x12345678;
    Clock::time_point started_;
public:
    double connect(double requested, int mask) override {
        if (std::find(kMockRates.begin(), kMockRates.end(), requested) == kMockRates.end())
            throw std::runtime_error("Mock rate is not in the supported rate list");
        rate_ = requested; channels_ = channelsFromMask(mask); started_ = Clock::now(); return rate_;
    }
    size_t acquire(size_t n, std::vector<float>& dst) override {
        dst.resize(n * channels_.size());
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < channels_.size(); ++j) {
                const auto channel = channels_[j];
                double cycles = (double)(index_ + i) * (450.0 + 35.0 * channel) / rate_ + channel * .11;
                double phase = cycles - std::floor(cycles);
                double tri = 1.0 - 4.0 * std::abs(phase - .5);
                random_ = random_ * 1664525u + 1013904223u;
                double noise = ((int)((random_ >> 16) & 65535) - 32768) / 32768.0 * .003;
                dst[i * channels_.size() + j] = (float)(tri * (.6 + .1 * channel) + noise);
            }
        }
        index_ += n;
        auto deadline = started_ + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>((double)index_ / rate_));
        if (Clock::now() < deadline) std::this_thread::sleep_until(deadline);
        return n;
    }
    std::string source() const override { return "synthetic"; }
    std::string continuity() const override { return "generated_contiguous"; }
};

#ifdef _WIN32
class LegacyDevice : public Device {
    HMODULE soft_ = nullptr, hard_ = nullptr, wave_ = nullptr;
    std::vector<int> channels_;
    double rate_ = 0;
    // The old libwave DLL ABI differs from the archived C++ source. Resolve the deployed DLL API.
    using Search = int(*)();
    using Init = bool(*)(unsigned short*, unsigned short*, unsigned short*, unsigned short, unsigned short, unsigned short, unsigned long, unsigned short);
    using Read = int(*)(int, float*, float*, float*, float*);
    using Rate = double(*)();
    using Lookup = double(*)(int, int);
    Read read_ = nullptr;
    std::array<std::vector<float>, 4> channelBuf_;
    void loadLibraries() {
        if (wave_) return;
        wchar_t exeBuf[32768]{};
        if (!GetModuleFileNameW(nullptr, exeBuf, 32768)) throw std::runtime_error("Cannot locate acquisition executable");
        fs::path folder = fs::path(exeBuf).parent_path();
        auto load = [&](const wchar_t* name) -> HMODULE {
            HMODULE h = LoadLibraryExW((folder / name).c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!h) throw std::runtime_error("Cannot load SDK DLL beside zw_acq.exe; check vendor files, x64 and MSVC runtime");
            return h;
        };
        soft_ = load(L"HTSoftDll.dll");
        hard_ = load(L"HTHardDll.dll");
        wave_ = load(L"libwave.dll");
    }
    FARPROC get(const char* name) {
        auto f = GetProcAddress(wave_, name);
        if (!f) throw std::runtime_error(std::string("Missing libwave export: ") + name);
        return f;
    }
public:
    std::vector<double> rateChoices(int mask) {
        channelsFromMask(mask);
        loadLibraries();
        if (reinterpret_cast<Search>(get("SearchDevice"))() <= 0)
            throw std::runtime_error("HT6004BX not found; install driver and connect the device");
        auto lookup = reinterpret_cast<Lookup>(get("LookupSampleRateHz"));
        // The archived wrapper does not prove it physically disables unselected inputs.
        // Use the conservative 4-channel mode table for every channel selection.
        std::vector<double> rates;
        for (int i = 0; i < 36; ++i) {
            double r = lookup(i, 4);
            if (std::isfinite(r) && r >= 100 && r <= 1000000000) rates.push_back(r);
        }
        std::sort(rates.begin(), rates.end(), std::greater<double>());
        rates.erase(std::unique(rates.begin(), rates.end()), rates.end());
        if (rates.empty()) throw std::runtime_error("Legacy DLL returned no valid sampling rates");
        return rates;
    }
    ~LegacyDevice() override { if (wave_) FreeLibrary(wave_); if (hard_) FreeLibrary(hard_); if (soft_) FreeLibrary(soft_); }
    double connect(double requested, int mask) override {
        channels_ = channelsFromMask(mask);
        loadLibraries();
        auto search = reinterpret_cast<Search>(get("SearchDevice"));
        auto init = reinterpret_cast<Init>(get("InitDevice"));
        read_ = reinterpret_cast<Read>(get("GetAllChannelData"));
        auto rate = reinterpret_cast<Rate>(get("GetSampleRate"));
        auto lookup = reinterpret_cast<Lookup>(get("LookupSampleRateHz"));
        if (search() <= 0) throw std::runtime_error("HT6004BX not found");
        // Conservative: no hardware sampling mode is inferred from the requested number alone.
        // Select only a genuine timebase-table entry; never silently round an unsupported rate.
        // Physical channel enable is unverified in the old wrapper, so use the 4-channel table.
        int mode = 4;
        int bestIdx = -1;
        for (int i = 0; i < 36; ++i) {
            const double candidate = lookup(i, mode);
            if (std::isfinite(candidate) && std::abs(candidate-requested) <= std::max(1e-8, requested*1e-8)) {
                bestIdx = i;
                break;
            }
        }
        if (bestIdx < 0) throw std::runtime_error("Unsupported sampling rate for this channel mode; query capabilities first");
        unsigned short volts[4]{7,7,7,7}, coupling[4]{0,0,0,0}, bandwidth[4]{0,0,0,0};
        if (!init(volts, coupling, bandwidth, (unsigned short)bestIdx, 0, 0, 20000, 0)) throw std::runtime_error("InitDevice failed");
        rate_ = rate();
        if (!(rate_ > 0 && std::isfinite(rate_))) throw std::runtime_error("SDK did not return valid sample rate");
        return rate_;
    }
    size_t acquire(size_t maxFrames, std::vector<float>& dst) override {
        size_t requested = std::min<size_t>(maxFrames, 10000);
        for (auto& b : channelBuf_) b.resize(requested);
        int n = read_((int)requested, channelBuf_[0].data(), channelBuf_[1].data(), channelBuf_[2].data(), channelBuf_[3].data());
        if (n <= 0 || n > (int)requested) throw std::runtime_error("Legacy GetAllChannelData returned invalid length");
        dst.resize((size_t)n * channels_.size());
        for (int i = 0; i < n; ++i) for (size_t j = 0; j < channels_.size(); ++j) dst[(size_t)i * channels_.size() + j] = channelBuf_[channels_[j]][i];
        return (size_t)n;
    }
    std::string source() const override { return "legacy_libwave_unverified"; }
    std::string continuity() const override { return "UNVERIFIED_legacy_poll_may_have_gaps_or_repeats"; }
};
#endif
struct Segment {
    std::string name;
    uint64_t startIndex = 0;
    uint64_t count = 0;
};
struct Block { uint64_t start = 0; size_t frames = 0; std::vector<float> data; };
class BlockQueue {
    std::mutex mu_; std::condition_variable hasItems_, hasRoom_;
    std::vector<Block> queue_;
    size_t read_ = 0, write_ = 0, used_ = 0;
    bool done_ = false;
public:
    explicit BlockQueue(size_t slots) : queue_(slots) {}
    bool push(Block&& b) {
        std::unique_lock lk(mu_);
        if (!hasRoom_.wait_for(lk, std::chrono::seconds(2), [&]{ return used_ < queue_.size() || done_; })) return false;
        if (done_) return false;
        queue_[write_] = std::move(b); write_ = (write_ + 1) % queue_.size(); ++used_;
        hasItems_.notify_one(); return true;
    }
    bool pop(Block& b) {
        std::unique_lock lk(mu_);
        hasItems_.wait(lk, [&]{ return used_ > 0 || done_; });
        if (used_ == 0) return false;
        b = std::move(queue_[read_]); read_ = (read_ + 1) % queue_.size(); --used_;
        hasRoom_.notify_one(); return true;
    }
    void finish() {
        std::lock_guard lk(mu_); done_ = true; hasItems_.notify_all(); hasRoom_.notify_all();
    }
    size_t size() { std::lock_guard lk(mu_); return used_; }
};
static void preview(const Block& b, int mask, double rate) {
    auto channels = channelsFromMask(mask); constexpr size_t buckets = 180;
    std::ostringstream o; o << "{\"type\":\"preview\",\"start_index\":" << b.start << ",\"rate\":" << rate << ",\"data\":[";
    for (size_t j = 0; j < channels.size(); ++j) {
        if (j) o << ',';
        o << '[';
        for (size_t k = 0; k < buckets; ++k) {
            size_t first = k * b.frames / buckets;
            size_t end = std::max(first+1, (k+1) * b.frames / buckets);
            float lo = std::numeric_limits<float>::infinity(), hi = -std::numeric_limits<float>::infinity();
            for (size_t i = first; i < std::min(end, b.frames); ++i) {
                float v = b.data[i * channels.size() + j]; lo = std::min(lo, v); hi = std::max(hi, v);
            }
            if (k) o << ',';
            o << '[' << std::setprecision(6) << lo << ',' << hi << ']';
        }
        o << ']';
    }
    send(o.str() + "]}");
}
static void writeMetadata(const fs::path& folder, const std::string& source, const std::string& continuity,
                          const std::vector<std::string>& names, int mask, double requested, double actual,
                          const std::string& unit, uint64_t total, const std::vector<Segment>& seg) {
    std::ofstream f(folder / "metadata.json", std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot write metadata.json");
    f << "{\n  \"format\": \"zwraw-v1\",\n  \"source\": " << quoted(source)
      << ",\n  \"continuity\": " << quoted(continuity)
      << ",\n  \"channel_mask\": " << mask
      << ",\n  \"requested_rate_hz\": " << std::setprecision(14) << requested
      << ",\n  \"actual_rate_hz\": " << actual
      << ",\n  \"canonical_unit\": \"V\",\n  \"display_unit\": " << quoted(unit)
      << ",\n  \"total_frames\": " << total << ",\n  \"channel_names\": [";
    for (size_t i=0;i<names.size();++i) { if (i) f << ','; f << quoted(names[i]); }
    f << "],\n  \"segments\": [";
    for (size_t i=0;i<seg.size();++i) {
        if (i) f << ',';
        f << "\n    {\"file\": " << quoted(seg[i].name) << ", \"start_index\": " << seg[i].startIndex << ", \"frames\": " << seg[i].count << "}";
    }
    f << "\n  ]\n}\n";
    if (!f.good()) throw std::runtime_error("Failed writing metadata.json");
}
static int record(const Args& args) {
    const std::string source = args.get("--source", "mock");
    const fs::path root = fromUtf8(args.get("--output", "recordings"));
    const int mask = (int)args.integer("--mask", 3, 1, 15);
    const auto ch = channelsFromMask(mask);
    const double requested = (double)args.integer("--rate", 1000000, 100, 1000000000);
    const auto limit = (uint64_t)args.integer("--max-seconds", 0, 0, 86400);
    const int segSec = (int)args.integer("--segment-seconds", 60, 1, 3600);
    const int segMb = (int)args.integer("--segment-mb", 128, 1, 4096);
    const std::string unit = args.get("--unit", "mV");
    if (unit != "V" && unit != "mV") throw std::runtime_error("Unit must be V or mV");
    auto names = split(args.get("--names", "CH1|CH2|CH3|CH4"), '|');
    if (names.size() != 4) throw std::runtime_error("--names expects exactly four pipe-separated values");
    std::unique_ptr<Device> device;
    if (source == "mock") device = std::make_unique<MockDevice>();
#ifdef _WIN32
    else if (source == "legacy") device = std::make_unique<LegacyDevice>();
#endif
    else throw std::runtime_error("Unknown source or unsupported OS: " + source);
    const double rate = device->connect(requested, mask);
    // No 'continuous verified' statement is made for the legacy polling SDK.
    if (!std::isfinite(rate) || rate <= 0) throw std::runtime_error("Invalid acquired sample rate");
    fs::create_directories(root);
    fs::path folder = root / ("record_" + nowStamp());
    for (int i = 2; fs::exists(folder); ++i) folder = root / ("record_" + nowStamp() + "_" + std::to_string(i));
    fs::create_directories(folder);
    const uint64_t startMs = nowMillis();
    uint64_t framesPerFile = (uint64_t)std::max(1.0, std::floor(rate * segSec));
    uint64_t bytesFrames = (uint64_t)segMb * 1024ULL * 1024ULL / (ch.size() * sizeof(float));
    framesPerFile = std::max<uint64_t>(1, std::min(framesPerFile, bytesFrames));
    const size_t blockFrames = (source == "legacy") ? 10000 : 8192;
    // 512 blocks ~ 16 MiB for 4 channels at 2k; actual capacity is 64MiB for 8k*4*4*128.
    BlockQueue queue(128);
    std::vector<Segment> segments;
    std::atomic<uint64_t> written{0};
    std::exception_ptr writerError;
    std::exception_ptr captureError;
    std::thread writer([&]() {
        try {
            std::ofstream file;
            Header h;
            uint64_t inFile = 0; int serial = 0;
            auto close = [&]() {
                if (!file.is_open()) return;
                h.sampleCount = inFile;
                file.seekp(0, std::ios::beg); file.write(reinterpret_cast<const char*>(&h), sizeof(h));
                file.flush(); if (!file.good()) throw std::runtime_error("Disk I/O error while closing segment");
                file.close();
                segments.back().count = inFile;
            };
            auto open = [&](uint64_t index) {
                ++serial;
                std::ostringstream name; name << "part_" << std::setw(4) << std::setfill('0') << serial << ".zwraw";
                segments.push_back({name.str(), index, 0});
                file.open(folder / name.str(), std::ios::binary | std::ios::trunc);
                if (!file) throw std::runtime_error("Cannot open recording segment for writing");
                h = Header{}; h.channelMask = mask; h.channelCount = (uint32_t)ch.size();
                h.sampleRateHz = rate; h.startIndex = index; h.startUnixMs = startMs;
                file.write(reinterpret_cast<const char*>(&h), sizeof(h));
                inFile = 0;
            };
            Block b;
            while (queue.pop(b)) {
                size_t offset = 0;
                while (offset < b.frames) {
                    if (!file.is_open()) open(b.start + offset);
                    uint64_t n = std::min<uint64_t>(framesPerFile - inFile, b.frames - offset);
                    const auto* bytes = reinterpret_cast<const char*>(b.data.data() + offset * ch.size());
                    file.write(bytes, (std::streamsize)(n * ch.size() * sizeof(float)));
                    if (!file.good()) throw std::runtime_error("Disk I/O error: cannot write raw data");
                    offset += (size_t)n; inFile += n; written += n;
                    if (inFile == framesPerFile) close();
                }
            }
            close();
            writeMetadata(folder, device->source(), device->continuity(), names, mask, requested, rate, unit, written, segments);
        } catch (...) { writerError = std::current_exception(); shouldStop = true; }
    });
    send("{\"type\":\"started\",\"folder\":" + quoted(utf8path(folder)) + ",\"rate\":" + std::to_string(rate) +
         ",\"source\":" + quoted(device->source()) + ",\"continuity\":" + quoted(device->continuity()) + "}");
    // Process exit terminates the detached stdin monitor; it never touches device state.
    std::thread([] { std::string line; while (std::getline(std::cin, line)) if (line == "stop") { shouldStop = true; break; } }).detach();
    uint64_t frames = 0; auto lastPreview = Clock::now(); auto lastProgress = lastPreview;
    auto started = Clock::now();
    try {
        while (!shouldStop) {
            Block b; b.start = frames;
            b.frames = device->acquire(blockFrames, b.data);
            if (b.frames == 0) throw std::runtime_error("Device returned no samples");
            // Validate data before queue; a broken buffer must not be recorded as valid.
            if (b.data.size() != b.frames * ch.size()) throw std::runtime_error("Device buffer length mismatch");
            frames += b.frames;
            auto now = Clock::now();
            if (now - lastPreview >= std::chrono::milliseconds(100)) { preview(b, mask, rate); lastPreview = now; }
            if (!queue.push(std::move(b))) throw std::runtime_error("Recorder buffer overflow: disk too slow, recording stopped");
            if (now - lastProgress >= std::chrono::seconds(1)) {
                std::ostringstream o;
                o << "{\"type\":\"stats\",\"captured\":" << frames << ",\"written\":" << written.load()
                  << ",\"pending_blocks\":" << queue.size() << ",\"elapsed_s\":"
                  << std::fixed << std::setprecision(2) << std::chrono::duration<double>(now-started).count() << '}';
                send(o.str()); lastProgress = now;
            }
            if (limit && now - started >= std::chrono::seconds(limit)) shouldStop = true;
        }
    } catch (...) {
        captureError = std::current_exception(); shouldStop = true;
    }
    queue.finish(); writer.join();
    if (writerError) std::rethrow_exception(writerError);
    if (captureError) std::rethrow_exception(captureError);
    send("{\"type\":\"stopped\",\"captured\":" + std::to_string(frames) + ",\"written\":" + std::to_string(written.load()) + ",\"folder\":" + quoted(utf8path(folder)) + "}");
    return 0;
}
struct FileDescriptor { fs::path path; Header h; uint64_t frames = 0; };
static std::vector<FileDescriptor> inspect(const fs::path& input) {
    std::vector<fs::path> paths;
    if (fs::is_regular_file(input)) paths.push_back(input);
    else if (fs::is_directory(input)) {
        for (const auto& e : fs::directory_iterator(input)) if (e.is_regular_file() && e.path().extension() == ".zwraw") paths.push_back(e.path());
    } else throw std::runtime_error("Input file or session directory not found");
    std::vector<FileDescriptor> files;
    for (const auto& p : paths) {
        std::ifstream f(p, std::ios::binary); Header h;
        if (!f.read(reinterpret_cast<char*>(&h), sizeof(h))) throw std::runtime_error("Truncated header: " + p.string());
        if (std::memcmp(h.magic, "ZWRAW01", 7) || h.version != 1 || h.sampleFormat != 1 || h.channelCount != channelsFromMask((int)h.channelMask).size())
            throw std::runtime_error("Invalid zwraw header: " + p.string());
        uint64_t expected = (uint64_t)sizeof(h) + h.sampleCount * h.channelCount * sizeof(float);
        if (h.sampleCount == 0 || fs::file_size(p) != expected) throw std::runtime_error("Truncated or unfinished raw segment: " + p.string());
        files.push_back({p,h,h.sampleCount});
    }
    if (files.empty()) throw std::runtime_error("No .zwraw files found");
    std::sort(files.begin(), files.end(), [](auto& a, auto& b){return a.h.startIndex < b.h.startIndex;});
    auto base = files.front().h;
    uint64_t next = base.startIndex;
    for (const auto& d : files) {
        if (d.h.channelMask != base.channelMask || d.h.channelCount != base.channelCount || d.h.sampleRateHz != base.sampleRateHz)
            throw std::runtime_error("Segment channel/rate configuration mismatch");
        if (d.h.startIndex != next) throw std::runtime_error("Gap or overlap in segment indices");
        next += d.frames;
    }
    return files;
}
static int exportCsv(const Args& args) {
    fs::path input = fromUtf8(args.get("--input")); if (input.empty()) throw std::runtime_error("--input required");
    fs::path out = fromUtf8(args.get("--output", "exports")); fs::create_directories(out);
    auto files = inspect(input); auto base = files.front().h;
    int requestedMask = (int)args.integer("--mask", base.channelMask, 1, 15);
    if ((requestedMask & (int)base.channelMask) != requestedMask) throw std::runtime_error("Selected channel is not present in recording");
    auto req = channelsFromMask(requestedMask), all = channelsFromMask((int)base.channelMask);
    uint64_t total = 0; for (auto& f : files) total += f.frames;
    uint64_t first = (uint64_t)args.integer("--start", 0, 0, (int64_t)total);
    uint64_t available = total - first;
    uint64_t count = (uint64_t)args.integer("--count", 0, 0, (int64_t)total);
    if (!count || count > available) count = available;
    uint64_t chunk = (uint64_t)args.integer("--points-per-file", 100000, 256, 200000);
    std::string unit = args.get("--unit", "mV"); if (unit != "V" && unit != "mV") throw std::runtime_error("Invalid unit");
    std::array<std::string,4> labels{"X1", "Y1", "X2", "Y2"};
    std::vector<int> positions;
    for (int channel : req) {
        auto it = std::find(all.begin(), all.end(), channel);
        positions.push_back((int)std::distance(all.begin(), it));
    }
    std::ofstream output; uint64_t currentCsvFrames = 0; uint64_t emitted = 0; int csvIndex = 0;
    auto close = [&]{ if (output.is_open()) { output.flush(); if (!output.good()) throw std::runtime_error("CSV write failed"); output.close(); } };
    auto open = [&](uint64_t globalStart) {
        ++csvIndex; std::ostringstream n; n << "export_" << std::setw(4) << std::setfill('0') << csvIndex << ".csv";
        output.open(out / n.str(), std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot open CSV output");
        output << "meta_key,meta_value,sample_index";
        for (int c : req) output << ',' << labels[c];
        output << "\n";
        auto meta = [&](const std::string& key, const std::string& val) {
            output << key << ',' << val << ',';
            for (size_t j=0;j<req.size();++j) output << ',';
            output << '\n';
        };
        meta("format", "zhuru-wave-v1");
        meta("sample_rate_hz", std::to_string(base.sampleRateHz));
        meta("channel_count", std::to_string(req.size()));
        std::string names; for (int c : req) { if (!names.empty()) names += '|'; names += labels[c]; }
        meta("channel_names", names);
        meta("amplitude_unit", unit);
        meta("synchronized", "true"); // frame indices aligned; hardware continuity is independent and may be unverified.
        meta("source", "zwraw-v1");
        meta("original_start_index", std::to_string(globalStart));
        currentCsvFrames = 0;
    };
    const double scale = (unit == "mV") ? 1000.0 : 1.0;
    uint64_t position = 0;
    std::vector<float> buf;
    for (auto& desc : files) {
        if (emitted >= count) break;
        if (position + desc.frames <= first) { position += desc.frames; continue; }
        uint64_t localStart = first > position ? first - position : 0;
        std::ifstream in(desc.path, std::ios::binary);
        in.seekg((std::streamoff)(sizeof(Header) + localStart * all.size() * sizeof(float)));
        uint64_t left = desc.frames - localStart;
        while (left && emitted < count) {
            size_t n = (size_t)std::min<uint64_t>({left, count-emitted, 8192});
            buf.resize(n * all.size());
            if (!in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)(buf.size() * sizeof(float)))) throw std::runtime_error("Input read error");
            for (size_t i=0;i<n;++i) {
                if (!output.is_open()) open(first + emitted);
                output << ",," << currentCsvFrames;
                for (int p : positions) output << ',' << std::fixed << std::setprecision(6) << buf[i*all.size()+(size_t)p] * scale;
                output << '\n';
                ++currentCsvFrames; ++emitted;
                if (currentCsvFrames >= chunk) close();
            }
            left -= n;
        }
        position += desc.frames;
    }
    close();
    send("{\"type\":\"export_done\",\"files\":" + std::to_string(csvIndex) + ",\"frames\":" + std::to_string(emitted) + ",\"output\":" + quoted(utf8path(out)) + "}");
    return 0;
}
static int programMain(int argc, char** argv) {
    try {
        Args args(argc, argv);
        const auto mode = args.get("--mode", "record");
        if (mode == "record") return record(args);
        if (mode == "export") return exportCsv(args);
        if (mode == "capabilities") {
            const std::string source = args.get("--source", "mock");
            const int mask = (int)args.integer("--mask", 3, 1, 15);
            std::vector<double> rates;
            std::string note;
            if (source == "mock") {
                rates = kMockRates;
                std::reverse(rates.begin(), rates.end());
                note = "模拟采集：采样连续性仅供软件测试";
            }
#ifdef _WIN32
            else if (source == "legacy") {
                LegacyDevice dev;
                rates = dev.rateChoices(mask);
                note = "旧 libwave 四通道时基表；实际连续传输能力和物理通道开关未验证";
            }
#endif
            else throw std::runtime_error("Unknown device source or unsupported operating system");
            double recommended = rates.front(), distance = std::abs(recommended - 1000000.0);
            for (double r : rates) if (std::abs(r - 1000000.0) < distance) {
                recommended = r; distance = std::abs(r - 1000000.0);
            }
            std::ostringstream o;
            o << "{\"type\":\"capabilities\",\"source\":" << quoted(source)
              << ",\"channel_mask\":" << mask << ",\"rates_hz\":[";
            for (size_t i = 0; i < rates.size(); ++i) { if (i) o << ','; o << std::setprecision(12) << rates[i]; }
            o << "],\"recommended_hz\":" << recommended << ",\"note\":" << quoted(note) << "}";
            send(o.str());
            return 0;
        }
        if (mode == "probe") {
            send("{\"type\":\"probe\",\"mock\":true,\"legacy_available\":" + std::string(fs::exists(fs::path(argv[0]).parent_path() / "libwave.dll") ? "true" : "false") + "}");
            return 0;
        }
        throw std::runtime_error("Unknown mode");
    } catch (const std::exception& e) { send(err(e.what())); return 1; }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** wideArgv) {
    // MSVC's Unicode entry point preserves non-ASCII output directories and channel labels.
    std::vector<std::string> utf8;
    std::vector<char*> args;
    utf8.reserve((size_t)argc);
    for (int i = 0; i < argc; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideArgv[i], -1, nullptr, 0, nullptr, nullptr);
        if (!bytes) { send(err("Invalid UTF-16 command argument")); return 1; }
        std::string value((size_t)bytes, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideArgv[i], -1, value.data(), bytes, nullptr, nullptr))
            return 1;
        value.resize((size_t)bytes-1);
        utf8.push_back(std::move(value));
    }
    for (auto& value : utf8) args.push_back(value.data());
    return programMain(argc, args.data());
}
#else
int main(int argc, char** argv) { return programMain(argc, argv); }
#endif
