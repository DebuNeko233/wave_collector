#pragma once
// Direct HT6004BX SDK acquisition adapter.
// Compiled only for Windows. Functions are imported from the vendor .lib files.
// Vendor NORMAL mode is a sequence of independently triggered memory captures;
// it is NOT proven to be sample-contiguous across acquisitions.
#ifndef _WIN32
#error "Hantek device adapter is Windows-only"
#endif
#include "HTHardDll.h"
#include <bit>

extern "C" __declspec(dllimport) ULONG WINAPI dsoSFGetSampleRate(WORD timeDiv, WORD alt, WORD channelMask, WORD channel);

class HantekDevice final : public Device {
    static constexpr WORD kVPos = 128;
    static constexpr WORD kVoltDiv = 7; // 500 mV/div per official SDK range table.
    static constexpr ULONG kReadLen = 4096; // safe within 64K shared hardware memory.
    WORD deviceIndex_ = 0;
    CONTROLDATA control_{};
    RELAYCONTROL relay_{};
    std::array<std::vector<WORD>, 4> raw_{};
    std::vector<int> selected_;
    double sampleRateHz_ = 0;
    bool ready_ = false;

    static void check(WORD status, const char* op) {
        if (!status) throw std::runtime_error(std::string("HT6004BX SDK call failed: ") + op);
    }
    static std::vector<std::pair<double, WORD>> rateTable(int mask) {
        const int channelCount = static_cast<int>(channelsFromMask(mask).size());
        // HT6004BX SDK user manual, Table 1 and Table 2 (timebase indices 0..35).
        // Listing nominal sample-rate options does not need a USB connection.
        constexpr std::array<double, 36> timebaseSeconds = {{
            2e-9, 5e-9, 10e-9, 20e-9, 50e-9, 100e-9, 200e-9, 500e-9,
            1e-6, 2e-6, 5e-6, 10e-6, 20e-6, 50e-6, 100e-6, 200e-6, 500e-6,
            1e-3, 2e-3, 5e-3, 10e-3, 20e-3, 50e-3, 100e-3, 200e-3, 500e-3,
            1, 2, 5, 10, 20, 50, 100, 200, 500, 1000
        }};
        static_assert(timebaseSeconds.size() == MAX_TIMEDIV_NUM);
        std::vector<std::pair<double, WORD>> out;
        for (WORD index = 0; index < timebaseSeconds.size(); ++index) {
            double rate;
            if (index <= 6) rate = channelCount == 1 ? 1e9 : channelCount == 2 ? 5e8 : 2.5e8;
            else if (index == 7) rate = channelCount <= 2 ? 5e8 : 2.5e8;
            else rate = std::round(250.0 / timebaseSeconds[index]);
            if (rate >= 100.0) out.emplace_back(rate, index);
        }
        std::sort(out.begin(), out.end(), [](auto a, auto b) { return a.first > b.first; });
        out.erase(std::unique(out.begin(), out.end(), [](auto a, auto b) { return a.first == b.first; }), out.end());
        return out;
    }
public:
    std::vector<double> rateChoices(int mask) {
        const auto table = rateTable(mask);
        std::vector<double> rates;
        for (const auto& item : table) rates.push_back(item.first);
        return rates;
    }
    double connect(double requested, int mask) override {
        selected_ = channelsFromMask(mask);
        const auto rates = rateTable(mask);
        const auto match = std::find_if(rates.begin(), rates.end(), [&](const auto& p) {
            return std::abs(p.first - requested) <= std::max(1e-7, requested * 1e-8);
        });
        if (match == rates.end()) throw std::runtime_error("Unsupported sample rate for selected channels");

        short deviceInfo[32]{};
        check(dsoHTSearchDevice(deviceInfo), "dsoHTSearchDevice");
        bool connected = false;
        for (WORD index = 0; index < 32; ++index) {
            if (dsoHTDeviceConnect(index)) { deviceIndex_ = index; connected = true; break; }
        }
        if (!connected) throw std::runtime_error("Failed to connect any HT6004BX USB device");

        control_ = {};
        relay_ = {};
        control_.nCHSet = static_cast<WORD>(mask);
        control_.nTimeDIV = match->second;
        control_.nTriggerSource = static_cast<WORD>(selected_.front());
        control_.nHTriggerPos = 50;
        control_.nVTriggerPos = kVPos;
        control_.nTriggerSlope = RISE;
        control_.nBufferLen = kReadLen;
        control_.nReadDataLen = kReadLen;
        control_.nAlreadyReadLen = 0;
        control_.nLastAddress = 0;
        relay_.nTrigSource = control_.nTriggerSource;
        for (int ch = 0; ch < 4; ++ch) {
            relay_.bCHEnable[ch] = ((mask >> ch) & 1) ? 1U : 0U;
            relay_.nCHVoltDIV[ch] = kVoltDiv;
            relay_.nCHCoupling[ch] = DC;
            relay_.bCHBWLimit[ch] = 0;
            raw_[ch].resize(kReadLen);
        }

        check(dsoInitHard(deviceIndex_), "dsoInitHard");
        // Conservative 4-channel gain path; physical relay.enable honors mask.
        check(dsoHTADCCHModGain(deviceIndex_, 4), "dsoHTADCCHModGain");
        check(dsoHTSetCHAndTrigger(deviceIndex_, &relay_, control_.nTimeDIV), "dsoHTSetCHAndTrigger");
        check(dsoHTSetSampleRate(deviceIndex_, YT_NORMAL, &relay_, &control_), "dsoHTSetSampleRate");
        WORD volts[4]{kVoltDiv,kVoltDiv,kVoltDiv,kVoltDiv};
        WORD pos[4]{kVPos,kVPos,kVPos,kVPos};
        check(dsoHTSetAmpCalibrate(deviceIndex_, control_.nCHSet, control_.nTimeDIV, volts, pos),
              "dsoHTSetAmpCalibrate");
        for (int ch : selected_) {
            check(dsoHTSetCHPos(deviceIndex_, kVoltDiv, kVPos, static_cast<WORD>(ch), 4),
                  "dsoHTSetCHPos");
        }
        check(dsoHTSetTrigerMode(deviceIndex_, EDGE, RISE, DC), "dsoHTSetTrigerMode");
        check(dsoHTSetVTriggerLevel(deviceIndex_, kVPos, 20), "dsoHTSetVTriggerLevel");
        check(dsoHTSetRamAndTrigerControl(deviceIndex_, control_.nTimeDIV, control_.nCHSet,
                                          control_.nTriggerSource, 0), "dsoHTSetRamAndTrigerControl");
        sampleRateHz_ = static_cast<double>(dsoGetSampleRate(deviceIndex_));
        if (!(std::isfinite(sampleRateHz_) && sampleRateHz_ > 0))
            throw std::runtime_error("Vendor SDK did not report a valid applied sample rate");
        ready_ = true;
        return sampleRateHz_;
    }
    size_t acquire(size_t maxFrames, std::vector<float>& dst) override {
        if (!ready_) throw std::runtime_error("HT6004BX not initialized");
        // NORMAL/GETDATA returns a complete trigger window, not a streaming
        // buffer. Restart and wait for each window rather than rereading stale memory.
        control_.nReadDataLen = static_cast<ULONG>(std::min<size_t>(maxFrames, kReadLen));
        control_.nBufferLen = kReadLen;
        check(dsoHTStartCollectData(deviceIndex_, 0), "dsoHTStartCollectData");
        const auto deadline = Clock::now() + std::chrono::seconds(8);
        bool completed = false;
        while (Clock::now() < deadline) {
            if (shouldStop.load()) return 0;
            WORD state = dsoHTGetState(deviceIndex_);
            if (state & 0x02) { completed = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!completed) throw std::runtime_error("Timed out waiting for HT6004BX acquisition");
        check(dsoHTGetData(deviceIndex_, raw_[0].data(), raw_[1].data(),
                           raw_[2].data(), raw_[3].data(), &control_), "dsoHTGetData");
        const size_t n = control_.nReadDataLen;
        if (n == 0 || n > kReadLen || n > maxFrames)
            throw std::runtime_error("HT6004BX returned an invalid capture length");
        dst.resize(n * selected_.size());
        constexpr float voltsPerDivision = 0.5f; // kVoltDiv=7, nominal only.
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < selected_.size(); ++j)
                dst[i * selected_.size() + j] =
                    (static_cast<float>(raw_[selected_[j]][i]) - kVPos) * (voltsPerDivision / 32.0f);
        return n;
    }
    std::string source() const override { return "hantek_sdk_direct_normal"; }
    std::string continuity() const override { return "UNVERIFIED_vendor_normal_trigger_windows_may_have_gaps"; }
};
