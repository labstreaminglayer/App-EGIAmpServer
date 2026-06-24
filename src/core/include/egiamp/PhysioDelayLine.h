#ifndef EGIAMP_PHYSIODELAYLINE_H
#define EGIAMP_PHYSIODELAYLINE_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace egiamp {

// Fixed-length delay line for the Physio16 (PIB / PNS) channels.
//
// The PIB acquisition path runs ahead of the FPGA-filtered EEG by a fixed
// amount in decimated mode (~32 ms; measured by scripts/delay_capture_sweep.py
// and notebooks/delay_inspection.ipynb). To make the combined EEG+Physio LSL
// stream sample-aligned, we hold each physio channel back by `delaySamples`
// samples, emitting zeros until the EEG has "caught up". Operates on the raw
// int32 ADC counts so the same delay serves both the native (int32) and the
// scaled-float output paths.
//
// One sample per channel is pushed per call to process(); all channels share a
// single write cursor and advance together.
class PhysioDelayLine {
public:
    // (Re)allocate for `channelCount` channels and a `delaySamples`-deep delay,
    // zero-filling the buffer so the warm-up region reads as 0. Pass
    // delaySamples == 0 to disable the delay (process() becomes a no-op).
    void configure(int channelCount, int delaySamples) {
        channelCount_ = channelCount > 0 ? channelCount : 0;
        delaySamples_ = delaySamples > 0 ? delaySamples : 0;
        pos_ = 0;
        buf_.assign(static_cast<size_t>(channelCount_) * delaySamples_, 0);
    }

    // Zero the delay line without changing its size (used on stream recovery so
    // pre-loss physio samples are not emitted into the post-recovery EEG).
    void reset() {
        pos_ = 0;
        std::fill(buf_.begin(), buf_.end(), 0);
    }

    [[nodiscard]] bool active() const { return delaySamples_ > 0 && channelCount_ > 0; }
    [[nodiscard]] int delaySamples() const { return delaySamples_; }

    // Advance the line by one sample. `data[0..count)` is replaced in place with
    // the values that were pushed `delaySamples` samples ago (zeros during
    // warm-up), and the incoming values are stored. `count` must equal the
    // configured channel count. A no-op when the delay is disabled.
    void process(int32_t* data, int count) {
        if (delaySamples_ == 0 || count <= 0) {
            return;
        }
        if (count > channelCount_) {
            count = channelCount_;
        }
        for (int ch = 0; ch < count; ++ch) {
            const size_t idx = static_cast<size_t>(ch) * delaySamples_ + pos_;
            const int32_t delayed = buf_[idx];
            buf_[idx] = data[ch];
            data[ch] = delayed;
        }
        pos_ = (pos_ + 1) % delaySamples_;
    }

private:
    std::vector<int32_t> buf_;
    int channelCount_ = 0;
    int delaySamples_ = 0;
    int pos_ = 0;
};

} // namespace egiamp

#endif // EGIAMP_PHYSIODELAYLINE_H
