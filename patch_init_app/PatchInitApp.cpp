#include "daisy_patch_sm.h"
#include "daisysp.h"

#include <cmath>
#include <algorithm>

using namespace daisy;
using namespace daisysp;

using namespace patch_sm;

DaisyPatchSM patch;

static LadderFilter lpf_l;
static LadderFilter lpf_r;

static DelayLine<float, 1920000> delay_l __attribute__((section(".sdram_bss")));
static DelayLine<float, 1920000> delay_r __attribute__((section(".sdram_bss")));
static OnePole coloration_l;
static OnePole coloration_r;

static Switch tap_button;
static float tap_tempo_bpm = 120.0f;
static uint32_t sample_count = 0;
static uint32_t led_pulse_sample = 0;
static bool tapping = false;
static bool averaging = false;
static float led_brightness = 0.0f;
static float blocks_per_second = 0.0f;

static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
{
    patch.ProcessAnalogControls();

    // ===== FILTER SECTION =====
    float freq_hz;
    float res;

    const float pot_cutoff  = (patch.GetAdcValue(CV_1) + 1.0f) * 0.5f;
    const float pot_res     = (patch.GetAdcValue(CV_2) + 1.0f) * 0.5f;

    const float cv_freq = ((patch.GetAdcValue(CV_5) - 0.5f) * 2.0f) * 0.2f;
    const float cv_res  = ((patch.GetAdcValue(CV_6) - 0.5f) * 2.0f) * 0.2f;

    const float cutoff_norm = fclamp(pot_cutoff + (cv_freq * 0.5f), 0.0f, 1.0f);
    const float res_norm    = fclamp(pot_res + (cv_res * 0.35f), 0.0f, 1.0f);

    const float min_hz = 15.0f;
    const float max_hz = 15000.0f;
    const float ratio  = max_hz / min_hz;
    freq_hz            = min_hz * powf(ratio, cutoff_norm);

    res = res_norm * 1.55f;

    lpf_l.SetFreq(freq_hz);
    lpf_r.SetFreq(freq_hz);
    lpf_l.SetRes(res);
    lpf_r.SetRes(res);

    // ===== DELAY SECTION =====
    const float pot_send     = patch.GetAdcValue(CV_3);
    const float pot_feedback = patch.GetAdcValue(CV_4);
    const float cv_time_mod  = ((patch.GetAdcValue(CV_7) - 0.5f) * 2.0f) * 0.15f;

    const uint32_t led_pulse_period_samples = (uint32_t)((60000.0f / tap_tempo_bpm) * (patch.AudioSampleRate() / 1000.0f));
    const float led_pulse_phase = (float)led_pulse_sample / (float)led_pulse_period_samples;
    const float led_pulse = (sinf(led_pulse_phase * 2.0f * M_PI) + 1.0f) * 0.5f;

    const float quarter_note_ms = (60000.0f / tap_tempo_bpm);
    const float delay_time_ms = quarter_note_ms * (1.0f + cv_time_mod * 0.1f);
    const float delay_samples = (delay_time_ms / 1000.0f) * patch.AudioSampleRate();

    const float feedback = pot_feedback;
    const float wet_amount = pot_send;
    const float dry_amount = 1.0f - wet_amount;

    for(size_t i = 0; i < size; i++)
    {
        const float x_l = lpf_l.Process(IN_L[i]);
        const float x_r = lpf_r.Process(IN_R[i]);

        const float delayed_l = delay_l.Read(delay_samples);
        const float delayed_r = delay_r.Read(delay_samples);

        const float saturated_l = tanhf(delayed_l * 0.8f);
        const float saturated_r = tanhf(delayed_r * 0.8f);

        const float colored_l = coloration_l.Process(saturated_l);
        const float colored_r = coloration_r.Process(saturated_r);

        delay_l.Write(x_l * wet_amount + colored_r * feedback);
        delay_r.Write(x_r * wet_amount + colored_l * feedback);

        OUT_L[i] = x_l * dry_amount + colored_l * 0.6f * wet_amount;
        OUT_R[i] = x_r * dry_amount + colored_r * 0.6f * wet_amount;
    }

    tap_button.Debounce();
    sample_count += size;
    led_pulse_sample += size;

    if(tapping)
    {
        if(tap_button.RisingEdge() && sample_count > 2400)
        {
            const float sr = patch.AudioSampleRate();
            const float interval_bpm = (sr / (float)sample_count) * 60.0f;
            if(averaging)
            {
                tap_tempo_bpm = 0.5f * tap_tempo_bpm + 0.5f * interval_bpm;
            }
            else
            {
                tap_tempo_bpm = interval_bpm;
                averaging = true;
            }
            tap_tempo_bpm = fclamp(tap_tempo_bpm, 40.0f, 240.0f);
            sample_count = 0;
        }
        else if(sample_count > 144000)
        {
            tapping = false;
            averaging = false;
            sample_count = 0;
        }
    }
    else if(tap_button.RisingEdge())
    {
        tapping = true;
        averaging = false;
        sample_count = 0;
    }

    if(led_pulse_sample >= led_pulse_period_samples)
        led_pulse_sample = 0;

    led_brightness = led_pulse * 0.8f;

    patch.WriteCvOut(CV_OUT_2, led_brightness * 5.0f);
}

int main(void)
{
    patch.Init();

    const float sr = patch.AudioSampleRate();
    
    lpf_l.Init(sr);
    lpf_r.Init(sr);
    lpf_l.SetFilterMode(LadderFilter::FilterMode::LP24);
    lpf_r.SetFilterMode(LadderFilter::FilterMode::LP24);
    lpf_l.SetInputDrive(1.0f);
    lpf_r.SetInputDrive(1.0f);
    lpf_l.SetPassbandGain(1.0f);
    lpf_r.SetPassbandGain(1.0f);

    delay_l.Init();
    delay_r.Init();
    coloration_l.Init();
    coloration_r.Init();
    coloration_l.SetFrequency(0.1f);
    coloration_r.SetFrequency(0.1f);

    tap_button.Init(patch.B7, patch.AudioCallbackRate());
    blocks_per_second = sr / 64.0f;

    patch.StartAudio(AudioCallback);

    while(1)
    {
    }
}
