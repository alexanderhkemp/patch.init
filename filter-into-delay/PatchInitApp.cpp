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
static LadderFilter hpf_send_l;
static LadderFilter hpf_send_r;

static DelayLine<float, 1920000> delay_l __attribute__((section(".sdram_bss")));
static DelayLine<float, 1920000> delay_r __attribute__((section(".sdram_bss")));
static OnePole coloration_l;
static OnePole coloration_r;

static Switch tap_button;
static Switch toggle_mode;
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
    float freq_hz_l;
    float freq_hz_r;
    float res;

    const float pot_cutoff  = (patch.GetAdcValue(CV_1) + 1.0f) * 0.5f;
    const float pot_res     = (patch.GetAdcValue(CV_2) + 1.0f) * 0.5f;

    const float cv_freq = ((patch.GetAdcValue(CV_5) - 0.5f) * 2.0f) * 0.2f;
    const float cv_res  = ((patch.GetAdcValue(CV_6) - 0.5f) * 2.0f) * 0.2f;
    const float cv_stereo = ((patch.GetAdcValue(CV_8) - 0.5f) * 2.0f) * 0.20f;

    const float cutoff_norm = fclamp(pot_cutoff + cv_freq, 0.0f, 1.0f);
    const float res_norm    = fclamp(pot_res + cv_res, 0.0f, 1.0f);

    const float min_hz = 15.0f;
    const float max_hz = 15000.0f;
    const float ratio  = max_hz / min_hz;
    const float freq_hz_center = min_hz * powf(ratio, cutoff_norm);
    
    freq_hz_l = freq_hz_center * powf(ratio, cv_stereo * 0.1f);
    freq_hz_r = freq_hz_center * powf(ratio, -cv_stereo * 0.1f);

    res = res_norm * 1.25f;

    lpf_l.SetFreq(freq_hz_l);
    lpf_r.SetFreq(freq_hz_r);
    lpf_l.SetRes(res);
    lpf_r.SetRes(res);

    // ===== DELAY SECTION =====
    const float pot_send     = patch.GetAdcValue(CV_3);
    const float pot_feedback = patch.GetAdcValue(CV_4);
    const float cv_time_mod  = ((patch.GetAdcValue(CV_7) - 0.5f) * 2.0f) * 0.15f;

    const uint32_t led_pulse_period_samples = (uint32_t)((60000.0f / tap_tempo_bpm) * (patch.AudioSampleRate() / 1000.0f));
    const uint32_t ramp_period_samples = led_pulse_period_samples * 2;
    const float led_pulse_phase = (float)led_pulse_sample / (float)led_pulse_period_samples;
    const float ramp_phase = fmodf((float)led_pulse_sample / (float)ramp_period_samples, 1.0f);
    const float led_pulse = (sinf(led_pulse_phase * 2.0f * M_PI) + 1.0f) * 0.5f;
    const float ramp_lfo = ramp_phase;

    const float quarter_note_ms = (60000.0f / tap_tempo_bpm);
    const float delay_time_ms = quarter_note_ms * (1.0f + cv_time_mod * 0.1f);
    const float delay_samples = (delay_time_ms / 1000.0f) * patch.AudioSampleRate();

    float send_level = pot_send * 1.25f;
    if(pot_send < 0.05f)
        send_level = 0.0f;
    
    const float feedback = pot_feedback * 1.5f;
    
    float dry_fader = 1.0f;
    if(pot_send >= 0.6f)
    {
        const float fader_range = (pot_send - 0.6f) / 0.4f;
        dry_fader = 1.0f - (fader_range * 0.40f);
    }

    toggle_mode.Debounce();
    const bool hpf_enabled = toggle_mode.Pressed();

    for(size_t i = 0; i < size; i++)
    {
        const float x_l = lpf_l.Process(IN_L[i]);
        const float x_r = lpf_r.Process(IN_R[i]);

        const float saturated_l = tanhf(x_l * 1.0f);
        const float saturated_r = tanhf(x_r * 1.0f);

        const float colored_l = coloration_l.Process(saturated_l);
        const float colored_r = coloration_r.Process(saturated_r);

        float send_signal_l = colored_l;
        float send_signal_r = colored_r;
        if(hpf_enabled)
        {
            send_signal_l = hpf_send_l.Process(colored_l);
            send_signal_r = hpf_send_r.Process(colored_r);
        }

        const float delayed_l = delay_l.Read(delay_samples);
        const float delayed_r = delay_r.Read(delay_samples);

        delay_l.Write(send_signal_l * send_level + delayed_r * feedback);
        delay_r.Write(send_signal_r * send_level + delayed_l * feedback);

        const float dry_mix_l = x_l * dry_fader;
        const float dry_mix_r = x_r * dry_fader;
        const float wet_mix_l = delayed_l * 1.44f;
        const float wet_mix_r = delayed_r * 1.44f;

        const float output_l = dry_mix_l + wet_mix_l;
        const float output_r = dry_mix_r + wet_mix_r;

        OUT_L[i] = tanhf(output_l * 0.5f) * 2.0f;
        OUT_R[i] = tanhf(output_r * 0.5f) * 2.0f;
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

    patch.WriteCvOut(CV_OUT_1, ramp_lfo * 2.5f);
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

    hpf_send_l.Init(sr);
    hpf_send_r.Init(sr);
    hpf_send_l.SetFilterMode(LadderFilter::FilterMode::HP24);
    hpf_send_r.SetFilterMode(LadderFilter::FilterMode::HP24);
    hpf_send_l.SetFreq(250.0f);
    hpf_send_r.SetFreq(250.0f);
    hpf_send_l.SetInputDrive(1.0f);
    hpf_send_r.SetInputDrive(1.0f);
    hpf_send_l.SetPassbandGain(1.0f);
    hpf_send_r.SetPassbandGain(1.0f);

    delay_l.Init();
    delay_r.Init();
    coloration_l.Init();
    coloration_r.Init();
    coloration_l.SetFrequency(0.1f);
    coloration_r.SetFrequency(0.1f);

    tap_button.Init(patch.B7, patch.AudioCallbackRate());
    toggle_mode.Init(patch.B8, patch.AudioCallbackRate());
    blocks_per_second = sr / 64.0f;

    patch.StartAudio(AudioCallback);

    while(1)
    {
    }
}
