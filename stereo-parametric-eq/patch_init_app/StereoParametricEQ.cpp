#include "daisy_patch_sm.h"
#include "daisysp.h"

#include <cmath>
#include <algorithm>

using namespace daisy;
using namespace daisysp;

using namespace patch_sm;

DaisyPatchSM patch;

struct BiquadPeakingEQ {
    float b0, b1, b2, a1, a2;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
    
    void SetPeakingEQ(float freq, float q, float gain_db, float sr) {
        const float A = powf(10.0f, gain_db / 40.0f);
        const float w0 = 2.0f * M_PI * freq / sr;
        const float sin_w0 = sinf(w0);
        const float cos_w0 = cosf(w0);
        const float alpha = sin_w0 / (2.0f * q);
        
        const float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cos_w0) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cos_w0) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }
    
    float Process(float x) {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

static BiquadPeakingEQ eq_low_l;
static BiquadPeakingEQ eq_low_r;
static BiquadPeakingEQ eq_high_l;
static BiquadPeakingEQ eq_high_r;

static Switch freeze_button;
static Switch animate_toggle;

static float low_freq = 100.0f;
static float high_freq = 5000.0f;
static float low_gain = 0.0f;
static float high_gain = 0.0f;

static bool freeze_active_l = false;
static float frozen_low_freq_offset_l = 0.0f;
static float frozen_low_gain_offset_l = 0.0f;
static float frozen_high_freq_offset_l = 0.0f;
static float frozen_high_gain_offset_l = 0.0f;
static float held_low_freq_l = 100.0f;
static float held_low_gain_l = 0.0f;
static float held_high_freq_l = 5000.0f;
static float held_high_gain_l = 0.0f;
static uint8_t b7_tap_count = 0;
static uint32_t b7_samples_since_tap = 0;
static const uint32_t B7_DOUBLE_TAP_WINDOW_SAMPLES = 24000;

static bool animate_mode = false;

struct SlicedSampleHold {
    float value = 0.5f;
    uint32_t sample_count = 0;
    uint32_t hold_period = 0;
    float target = 0.5f;
    float slew_rate = 0.0f;
    uint32_t min_hold_samples = 0;
    uint32_t max_hold_samples = 0;
    
    void Init(float sr, float min_hold_sec, float max_hold_sec, float slew_time_sec) {
        min_hold_samples = (uint32_t)(min_hold_sec * sr);
        max_hold_samples = (uint32_t)(max_hold_sec * sr);
        slew_rate = 1.0f / (slew_time_sec * sr);
        hold_period = min_hold_samples + (rand() % (max_hold_samples - min_hold_samples + 1));
        target = (rand() % 1000) / 1000.0f;
        sample_count = 0;
    }
    
    float Process(uint32_t step_samples = 1) {
        sample_count += step_samples;
        
        if(sample_count >= hold_period) {
            target = (rand() % 1000) / 1000.0f;
            hold_period = min_hold_samples + (rand() % (max_hold_samples - min_hold_samples + 1));
            sample_count = 0;
        }
        
        if(value < target) {
            value += slew_rate;
            if(value > target) value = target;
        } else if(value > target) {
            value -= slew_rate;
            if(value < target) value = target;
        }
        
        return value;
    }
};

static SlicedSampleHold lfo_high_freq_l;
static SlicedSampleHold lfo_high_freq_r;

static float rms_level = 0.0f;
static float rms_env = 0.0f;
static float rms_accumulator = 0.0f;
static uint32_t rms_sample_count = 0;
static bool right_input_present = true;
static uint32_t right_input_silence_samples = 0;

static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
{
    patch.ProcessAnalogControls();

    const float sr = patch.AudioSampleRate();

    const float pot_low_freq = patch.GetAdcValue(CV_1);
    const float pot_low_gain = patch.GetAdcValue(CV_3);
    const float pot_high_freq = patch.GetAdcValue(CV_2);
    const float pot_high_gain = patch.GetAdcValue(CV_4);

    const float cv_low_freq = ((patch.GetAdcValue(CV_5) - 0.5f) * 2.0f) * 0.2f;
    const float cv_low_gain = ((patch.GetAdcValue(CV_7) - 0.5f) * 2.0f) * 0.2f;
    const float cv_high_freq = ((patch.GetAdcValue(CV_6) - 0.5f) * 2.0f) * 0.2f;
    const float cv_high_gain = ((patch.GetAdcValue(CV_8) - 0.5f) * 2.0f) * 0.2f;

    freeze_button.Debounce();
    animate_toggle.Debounce();

    animate_mode = animate_toggle.Pressed();

    float low_freq_norm = pot_low_freq + cv_low_freq;
    float high_freq_norm = pot_high_freq + cv_high_freq;
    float high_freq_norm_l = high_freq_norm;

    low_gain = (pot_low_gain + cv_low_gain - 0.5f) * 2.0f;
    low_gain = fclamp(low_gain, -1.0f, 1.0f) * 30.0f;

    high_gain = (pot_high_gain + cv_high_gain - 0.5f) * 2.0f;
    high_gain = fclamp(high_gain, -1.0f, 1.0f) * 30.0f;

    if(animate_mode)
    {
        const float lfo_high_freq_val_l = lfo_high_freq_l.Process(static_cast<uint32_t>(size));
        const float lfo_high_freq_val_r = lfo_high_freq_r.Process(static_cast<uint32_t>(size));

        high_freq_norm_l += (lfo_high_freq_val_l - 0.5f) * 1.2f;
        high_freq_norm += (lfo_high_freq_val_r - 0.5f) * 1.2f;
    }

    low_freq_norm = fclamp(low_freq_norm, 0.0f, 1.0f);
    high_freq_norm = fclamp(high_freq_norm, 0.0f, 1.0f);
    high_freq_norm_l = fclamp(high_freq_norm_l, 0.0f, 1.0f);

    low_freq = 60.0f * powf(700.0f / 60.0f, low_freq_norm);
    high_freq = 400.0f * powf(12000.0f / 400.0f, high_freq_norm);
    const float high_freq_l = 400.0f * powf(12000.0f / 400.0f, high_freq_norm_l);

    b7_samples_since_tap += size;
    if(b7_samples_since_tap > B7_DOUBLE_TAP_WINDOW_SAMPLES)
    {
        b7_tap_count = 0;
    }

    if(freeze_button.RisingEdge())
    {
        if(b7_samples_since_tap > B7_DOUBLE_TAP_WINDOW_SAMPLES)
        {
            b7_tap_count = 0;
        }

        b7_tap_count++;
        b7_samples_since_tap = 0;

        if(b7_tap_count >= 2)
        {
            frozen_low_freq_offset_l = 0.0f;
            frozen_low_gain_offset_l = 0.0f;
            frozen_high_freq_offset_l = 0.0f;
            frozen_high_gain_offset_l = 0.0f;

            held_low_freq_l = low_freq;
            held_low_gain_l = low_gain;
            held_high_freq_l = high_freq;
            held_high_gain_l = high_gain;

            b7_tap_count = 0;
        }
        else
        {
            held_low_freq_l = low_freq + frozen_low_freq_offset_l;
            held_low_gain_l = low_gain + frozen_low_gain_offset_l;
            held_high_freq_l = high_freq_l + frozen_high_freq_offset_l;
            held_high_gain_l = high_gain + frozen_high_gain_offset_l;
        }
    }

    if(freeze_button.FallingEdge())
    {
        frozen_low_freq_offset_l = held_low_freq_l - low_freq;
        frozen_low_gain_offset_l = held_low_gain_l - low_gain;
        frozen_high_freq_offset_l = held_high_freq_l - high_freq;
        frozen_high_gain_offset_l = held_high_gain_l - high_gain;
    }

    freeze_active_l = freeze_button.Pressed();

    float active_low_freq_l = low_freq + frozen_low_freq_offset_l;
    float active_low_gain_l = low_gain + frozen_low_gain_offset_l;
    float active_high_freq_l = high_freq_l + frozen_high_freq_offset_l;
    float active_high_gain_l = high_gain + frozen_high_gain_offset_l;

    if(freeze_active_l)
    {
        active_low_freq_l = held_low_freq_l;
        active_low_gain_l = held_low_gain_l;
        active_high_freq_l = held_high_freq_l;
        active_high_gain_l = held_high_gain_l;
    }

    active_low_freq_l = fclamp(active_low_freq_l, 60.0f, 700.0f);
    active_high_freq_l = fclamp(active_high_freq_l, 400.0f, 12000.0f);
    active_low_gain_l = fclamp(active_low_gain_l, -30.0f, 30.0f);
    active_high_gain_l = fclamp(active_high_gain_l, -30.0f, 30.0f);

    const float low_q = 1.2f;
    const float high_q = 1.0f;

    eq_low_l.SetPeakingEQ(active_low_freq_l, low_q, active_low_gain_l, sr);
    eq_high_l.SetPeakingEQ(active_high_freq_l, high_q, active_high_gain_l, sr);

    eq_low_r.SetPeakingEQ(low_freq, low_q, low_gain, sr);
    eq_high_r.SetPeakingEQ(high_freq, high_q, high_gain, sr);

    const float right_input_on_threshold = 0.01f;
    const float right_input_off_threshold = 0.003f;
    const uint32_t right_input_hold_samples = static_cast<uint32_t>(0.35f * sr);
    float right_input_peak = 0.0f;

    for(size_t i = 0; i < size; i++)
    {
        const float in_l = IN_L[i];
        const float in_r_raw = IN_R[i];
        const float in_r = right_input_present ? in_r_raw : in_l;
        right_input_peak = std::max(right_input_peak, fabsf(in_r_raw));

        float out_l = eq_low_l.Process(in_l);
        out_l = eq_high_l.Process(out_l);

        float out_r = eq_low_r.Process(in_r);
        out_r = eq_high_r.Process(out_r);

        OUT_L[i] = out_l;
        OUT_R[i] = out_r;

        rms_accumulator += (in_l * in_l + in_r * in_r) * 0.5f;
        rms_sample_count++;
    }

    if(right_input_present)
    {
        if(right_input_peak < right_input_off_threshold)
        {
            right_input_silence_samples += size;
            if(right_input_silence_samples >= right_input_hold_samples)
            {
                right_input_present = false;
                right_input_silence_samples = 0;
            }
        }
        else
        {
            right_input_silence_samples = 0;
        }
    }
    else
    {
        if(right_input_peak > right_input_on_threshold)
        {
            right_input_present = true;
            right_input_silence_samples = 0;
        }
    }

    if(rms_sample_count >= 4800)
    {
        rms_level = sqrtf(rms_accumulator / rms_sample_count);
        rms_env = 0.9f * rms_env + 0.1f * rms_level;
        rms_accumulator = 0.0f;
        rms_sample_count = 0;
    }

    const float meter = fclamp(rms_env * 16.0f, 0.0f, 1.0f);
    patch.WriteCvOut(CV_OUT_1, meter * 5.0f);
    patch.WriteCvOut(CV_OUT_2, meter * 5.0f);

}

int main(void)
{
    patch.Init();

    const float sr = patch.AudioSampleRate();

    freeze_button.Init(patch.B7, patch.AudioCallbackRate());
    animate_toggle.Init(patch.B8, patch.AudioCallbackRate());

    lfo_high_freq_l.Init(sr, 1.2f, 2.0f, 0.15f);
    lfo_high_freq_r.Init(sr, 1.4f, 2.2f, 0.15f);

    patch.StartAudio(AudioCallback);

    while(1)
    {
    }
}
