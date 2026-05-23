// Cheap Heat Guitar - Hothouse Edition
// Lo-fi tape warble effect for Cleveland Music Co Hothouse DSP Platform
// Mono guitar input, stereo output, 3 noise pairs generated in SDRAM

#include "hothouse.h"
#include "daisysp.h"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::System;
using daisysp::DelayLine;
using daisysp::fclamp;
using daisysp::LadderFilter;

Hothouse hw;

// Delay buffer in SDRAM (like the Hothouse examples)
static constexpr size_t kMaxDelay = 24000;  // 0.5 seconds at 48kHz
DelayLine<float, kMaxDelay> DSY_SDRAM_BSS mod_delay_l;
DelayLine<float, kMaxDelay> DSY_SDRAM_BSS mod_delay_r;

// Ladder filters for haze tone control
static LadderFilter haze_filter_l, haze_filter_r;
static LadderFilter output_hpf_l, output_hpf_r;

// State variables
static float sample_rate = 48000.0f;
static float wow_phase = 0.0f;
static float flutter_phase_l = 0.0f;
static float flutter_phase_r = 0.37f;
static float wow_depth_smooth = 0.0f;
static float wow_random_slew_l = 0.0f;
static float wow_random_slew_r = 0.0f;
static float flutter_noise_slew = 0.0f;
static float hyst_state_l = 0.0f;
static float hyst_state_r = 0.0f;

static float dropout_target_l = 1.0f;
static float dropout_target_r = 1.0f;
static float dropout_gain_l = 1.0f;
static float dropout_gain_r = 1.0f;
static int dropout_count_l = 0;
static int dropout_count_r = 0;

static float rms_env_left = 0.0f;
static float dust_cv_env = 0.0f;

// Bypass and boost state
static bool effect_bypass = true;
static bool boost_active = false;

// LED for bypass indication
Led led_bypass;

// Helper functions (defined before use)
inline float RandBi() { return 2.0f * ((float)rand() / RAND_MAX) - 1.0f; }
inline float Tri(float phase) { return 4.0f * fabsf(phase - 0.5f) - 1.0f; }

// Noise loop storage in SDRAM (64MB available)
// 3 pairs x 5 seconds x 48kHz x 2 channels (interleaved stereo) = ~5.76MB
// Using .sdram_bss linker section to place in SDRAM at 0xc0000000
static constexpr size_t kNoiseSamples = 48000 * 5; // 5 seconds at 48kHz

// SDRAM buffers for noise loops (runtime playback from SDRAM)
// Generated at boot - no QSPIFLASH needed, compatible with standard web flasher
float DSY_SDRAM_BSS sdram_tape_1[kNoiseSamples * 2];
float DSY_SDRAM_BSS sdram_vinyl_1[kNoiseSamples * 2];
float DSY_SDRAM_BSS sdram_tape_2[kNoiseSamples * 2];
float DSY_SDRAM_BSS sdram_vinyl_2[kNoiseSamples * 2];
float DSY_SDRAM_BSS sdram_tape_3[kNoiseSamples * 2];
float DSY_SDRAM_BSS sdram_vinyl_3[kNoiseSamples * 2];

// Noise player for SDRAM-based stereo loops
struct NoisePlayer {
    float* buffer;
    size_t position;
    size_t length;
    bool active;
    
    void Init(float* buf, size_t len) {
        buffer = buf;
        length = len;
        position = 0;
        active = true;
    }
    
    float Read() {
        if (!active || !buffer) return 0.0f;
        float out_l = buffer[position * 2];
        float out_r = buffer[position * 2 + 1];
        position++;
        if (position >= length) position = 0;
        return (out_l + out_r) * 0.5f; // Mono mix
    }
    
    float ReadLeft() {
        if (!active || !buffer) return 0.0f;
        return buffer[position * 2];
    }
    
    float ReadRight() {
        if (!active || !buffer) return 0.0f;
        return buffer[position * 2 + 1];
    }
    
    void Restart() { position = 0; }
};

static NoisePlayer tape_players[3];
static NoisePlayer vinyl_players[3];

void InitNoiseLoops() {
    // Generate noise loops directly in SDRAM at boot
    // No QSPIFLASH needed - fits in standard 128KB flash binary
    
    // Tape 1: gentle tape hiss (pink-ish filtered noise)
    float lp_state = 0.0f;
    for (size_t i = 0; i < kNoiseSamples * 2; i++) {
        float n = RandBi() * 0.08f;
        lp_state += 0.15f * (n - lp_state);  // simple lowpass for pink-ish character
        sdram_tape_1[i] = lp_state;
    }
    // Vinyl 1: sparse crackle
    for (size_t i = 0; i < kNoiseSamples * 2; i++) {
        sdram_vinyl_1[i] = (rand() % 1200 < 5) ? RandBi() * 0.25f : 0.0f;
    }
    
    // Tape 2: heavier cassette noise
    lp_state = 0.0f;
    for (size_t i = 0; i < kNoiseSamples * 2; i++) {
        float n = RandBi() * 0.12f;
        lp_state += 0.12f * (n - lp_state);
        sdram_tape_2[i] = lp_state;
    }
    // Vinyl 2: denser dusty vinyl
    for (size_t i = 0; i < kNoiseSamples * 2; i++) {
        sdram_vinyl_2[i] = (rand() % 800 < 8) ? RandBi() * 0.2f : 0.0f;
    }
    
    // Tape 3: extreme reel-to-reel hum + noise
    lp_state = 0.0f;
    for (size_t i = 0; i < kNoiseSamples * 2; i++) {
        float n = RandBi() * 0.15f;
        lp_state += 0.10f * (n - lp_state);
        // Add low-frequency hum component
        float hum = 0.04f * sinf(2.0f * 3.14159f * 50.0f * (float)i / 96000.0f);
        sdram_tape_3[i] = lp_state + hum;
    }
    // Vinyl 3: heavy warped record crackle
    for (size_t i = 0; i < kNoiseSamples * 2; i++) {
        sdram_vinyl_3[i] = (rand() % 600 < 12) ? RandBi() * 0.3f : 0.0f;
    }
    
    // Initialize noise players with SDRAM buffers
    tape_players[0].Init(sdram_tape_1, kNoiseSamples);
    vinyl_players[0].Init(sdram_vinyl_1, kNoiseSamples);
    tape_players[1].Init(sdram_tape_2, kNoiseSamples);
    vinyl_players[1].Init(sdram_vinyl_2, kNoiseSamples);
    tape_players[2].Init(sdram_tape_3, kNoiseSamples);
    vinyl_players[2].Init(sdram_vinyl_3, kNoiseSamples);
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    hw.ProcessAllControls();
    
    const float sr = sample_rate;
    const float sr_recip = 1.0f / sr;
    
    // Read controls via Hothouse HAL
    const float haze_knob = fclamp(hw.GetKnobValue(Hothouse::KNOB_1), 0.0f, 1.0f);
    const float wow_knob = fclamp(hw.GetKnobValue(Hothouse::KNOB_2), 0.0f, 1.0f);
    const float flutter_knob = fclamp(hw.GetKnobValue(Hothouse::KNOB_3), 0.0f, 1.0f);
    // KNOB_4 (noise_knob) is read below with tape/vinyl blend logic
    // KNOB_5 = Global LFO speed (was master output)
    const float global_speed = 0.5f + fclamp(hw.GetKnobValue(Hothouse::KNOB_5), 0.0f, 1.0f) * 1.5f;
    // KNOB_6 = Wet/dry blend for chorus effects (0 = dry/clean, 1 = full wet)
    const float wet_dry_blend = fclamp(hw.GetKnobValue(Hothouse::KNOB_6), 0.0f, 1.0f);
    
    // Toggle 1 selects which noise pair to use (3-position: up/mid/down)
    // UP = Pair 1, MIDDLE = Pair 2, DOWN = Pair 3
    Hothouse::ToggleswitchPosition toggle1_pos = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1);
    int selected_pair = 0;  // Default to pair 1
    if (toggle1_pos == Hothouse::TOGGLESWITCH_MIDDLE) {
        selected_pair = 1;  // Pair 2
    } else if (toggle1_pos == Hothouse::TOGGLESWITCH_DOWN) {
        selected_pair = 2;  // Pair 3
    }
    
    // Toggle 2 controls output HPF (3-position)
    // DOWN = no HPF, MIDDLE = 120Hz, UP = 240Hz (guitar-optimized, more aggressive)
    Hothouse::ToggleswitchPosition toggle2_pos = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2);
    float hpf_freq = 0.0f;  // 0 = bypass
    if (toggle2_pos == Hothouse::TOGGLESWITCH_MIDDLE) {
        hpf_freq = 120.0f;  // Standard guitar HPF
    } else if (toggle2_pos == Hothouse::TOGGLESWITCH_UP) {
        hpf_freq = 240.0f;  // Aggressive HPF for less bass-heavy guitar sound
    }
    // Update HPF frequency if active
    if (hpf_freq > 0.0f) {
        output_hpf_l.SetFreq(hpf_freq);
        output_hpf_r.SetFreq(hpf_freq);
    }
    
    // Knob 4: noise blend - noon = no noise, CCW = tape, CW = vinyl
    // Map 0..1 to -1..1 where 0 = full tape, 0.5 = silence, 1 = full vinyl
    const float noise_knob = fclamp(hw.GetKnobValue(Hothouse::KNOB_4), 0.0f, 1.0f);
    float tape_amount = 0.0f;
    float vinyl_amount = 0.0f;
    const float noise_deadband = 0.05f;  // Small deadband around noon
    if (noise_knob < 0.5f - noise_deadband * 0.5f) {
        // CCW side = tape, scaled 1.0 at 0.0 to 0.0 at center
        tape_amount = (0.5f - noise_deadband * 0.5f - noise_knob) / (0.5f - noise_deadband * 0.5f);
    } else if (noise_knob > 0.5f + noise_deadband * 0.5f) {
        // CW side = vinyl, scaled 0.0 at center to 1.0 at 1.0
        vinyl_amount = (noise_knob - 0.5f - noise_deadband * 0.5f) / (0.5f - noise_deadband * 0.5f);
    }
    // If in deadband, both remain 0 (no noise)
    
    // Footswitch 2 = bypass toggle (standard Hothouse pattern)
    effect_bypass ^= hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge();
    // Footswitch 1 = +4dB boost toggle
    boost_active ^= hw.switches[Hothouse::FOOTSWITCH_1].RisingEdge();
    
    const float boost_gain = boost_active ? 1.585f : 1.0f; // +4dB
    
    // Derived parameters with global speed scaling
    // Wow frequency is fixed at 0.35Hz (like tape flutter) - only scaled by global speed knob
    // Depth is controlled separately by wow_knob with sqrt shaping for musical control
    const float wow_freq_base = 0.35f * global_speed;
    const float wow_phase_inc = wow_freq_base * sr_recip;
    
    const float flutter_freq_l = (3.8f + 2.2f * flutter_knob) * 0.8667f * global_speed;
    const float flutter_freq_r = (3.8f + 2.2f * flutter_knob) * 1.1547f * global_speed;
    const float flutter_phase_inc_l = flutter_freq_l * sr_recip;
    const float flutter_phase_inc_r = flutter_freq_r * sr_recip;
    
    const float flutter_depth_samples = flutter_knob * 22.0f;
    
    // Haze staging
    const float haze_tone = haze_knob;
    const float haze_sat = haze_knob * haze_knob;
    const float haze_hyst = haze_knob * 0.8f;
    const float haze_collapse = (haze_knob > 0.65f) ? (haze_knob - 0.65f) / 0.35f : 0.0f;
    
    const float haze_drive_l = 1.0f + 1.1f * haze_tone + 1.6f * haze_sat;
    const float haze_drive_r = 1.0f + 1.0f * haze_tone + 1.75f * haze_sat;
    const float haze_lowpass_hz = fclamp(
        19000.0f - 9000.0f * haze_tone - 5200.0f * haze_hyst - 3200.0f * haze_collapse,
        450.0f, 20000.0f);
    const float haze_res = 0.12f + 0.30f * haze_tone + 0.24f * haze_collapse;
    const float haze_out_trim = fclamp(
        1.0f - 0.22f * haze_tone - 0.38f * haze_sat - 0.52f * haze_collapse,
        0.25f, 1.0f);
    
    const float haze_hyst_follow_l = 0.030f - 0.020f * haze_hyst;
    const float haze_hyst_follow_r = 0.032f - 0.021f * haze_hyst;
    const float haze_hyst_mix_l = 0.05f + 0.55f * haze_hyst + 0.20f * haze_collapse;
    const float haze_hyst_mix_r = 0.05f + 0.50f * haze_hyst + 0.22f * haze_collapse;
    
    const float sat_bias_l = 0.008f + 0.030f * haze_sat + 0.045f * haze_collapse;
    const float sat_bias_r = 0.007f + 0.026f * haze_sat + 0.050f * haze_collapse;
    const float sat_drive_l = 1.0f + 0.85f * haze_tone + 1.25f * haze_sat + 0.45f * haze_collapse;
    const float sat_drive_r = 1.0f + 0.78f * haze_tone + 1.35f * haze_sat + 0.40f * haze_collapse;
    const float haze_rough = 0.002f + 0.016f * haze_collapse;
    
    haze_filter_l.SetFreq(haze_lowpass_hz);
    haze_filter_l.SetRes(haze_res);
    haze_filter_r.SetFreq(haze_lowpass_hz);
    haze_filter_r.SetRes(haze_res);
    
    // Update filter resonance/character here if needed
    
    for (size_t i = 0; i < size; i++) {
        // Mono input (guitar), duplicate to stereo processing
        float in_mono = in[0][i];
        
        if (effect_bypass) {
            out[0][i] = in_mono * boost_gain;
            out[1][i] = in_mono * boost_gain;
            continue;
        }
        
        // Duplicate mono to stereo for processing
        float in_l = in_mono;
        float in_r = in_mono;
        
        // Noise reading from selected pair only
        float mod_l = 0.0f, mod_r = 0.0f;
        float noise_l = 0.0f, noise_r = 0.0f;
        
        // Read from selected pair and blend tape/vinyl based on knob 4
        float t = tape_players[selected_pair].Read();
        float v = vinyl_players[selected_pair].Read();
        
        // Mix tape and vinyl based on knob position
        // noon = 0 noise, CCW = tape, CW = vinyl
        noise_l = t * tape_amount + v * vinyl_amount;
        noise_r = noise_l;  // Mono noise
        
        // Modulation energy for wow/flutter (mono mix for modulation)
        mod_l = noise_l;
        mod_r = noise_r;
        const float mod_energy = fclamp((fabsf(mod_l) + fabsf(mod_r)) * 0.5f, 0.0f, 1.0f);
        
        // Wow LFO update
        wow_phase += wow_phase_inc;
        if (wow_phase >= 1.0f) wow_phase -= 1.0f;
        
        // Flutter LFO update
        flutter_phase_l += flutter_phase_inc_l;
        flutter_phase_r += flutter_phase_inc_r;
        if (flutter_phase_l >= 1.0f) flutter_phase_l -= 1.0f;
        if (flutter_phase_r >= 1.0f) flutter_phase_r -= 1.0f;
        
        // Independent random slew for stereo wow
        wow_random_slew_l += 0.00028f * ((0.6f * RandBi() + 0.7f * mod_l) - wow_random_slew_l);
        wow_random_slew_r += 0.00022f * ((0.6f * RandBi() + 0.7f * mod_r) - wow_random_slew_r);
        flutter_noise_slew += 0.0016f * ((0.4f * RandBi() + 0.9f * mod_r) - flutter_noise_slew);
        
        // Wow LFO shapes - stereo independent random (more random than triangle)
        const float wow_tri_l = Tri(wow_phase);
        const float wow_tri_r = Tri(wow_phase + 0.28f);
        const float wow_rand_l = wow_random_slew_l;
        const float wow_rand_r = wow_random_slew_r;
        // 30% triangle, 70% random slew - more organic tape feel
        const float wow_lfo_l = fclamp(wow_tri_l * 0.30f + wow_rand_l * 0.70f, -1.0f, 1.0f);
        const float wow_lfo_r = fclamp(wow_tri_r * 0.28f + wow_rand_r * 0.72f, -1.0f, 1.0f);
        
        // Flutter LFO
        const float flutter_lfo_l = Tri(flutter_phase_l) * 0.8f + flutter_noise_slew * 0.45f;
        const float flutter_lfo_r = Tri(flutter_phase_r) * 0.8f - flutter_noise_slew * 0.42f;
        
        // Wow depth with deadband, smoothing, and sqrt shaping (like patch.init)
        const float wow_depth_raw = fclamp(wow_knob, 0.0f, 1.0f);
        const float wow_deadband = 0.03f;
        float wow_depth_lin = 0.0f;
        if (wow_depth_raw > wow_deadband) {
            wow_depth_lin = (wow_depth_raw - wow_deadband) / (1.0f - wow_deadband);
        }
        wow_depth_smooth += 0.08f * (wow_depth_lin - wow_depth_smooth);
        const float wow_depth = fclamp(wow_depth_smooth, 0.0f, 1.0f);
        
        // Sqrt shaping gives more depth at lower knob settings for better control
        const float wow_depth_shaped = 0.10f * wow_depth + 0.90f * sqrtf(wow_depth);
        // Reduced max depth (800 samples = ~17ms max) for subtler, more musical wow
        const float wow_depth_samples = wow_depth_shaped * 800.0f;
        
        // Modulation values
        const float wow_mod_l = wow_lfo_l * wow_depth_samples;
        const float wow_mod_r = wow_lfo_r * wow_depth_samples;
        const float flutter_mod_l = flutter_lfo_l * flutter_depth_samples;
        const float flutter_mod_r = flutter_lfo_r * flutter_depth_samples;
        
        // Small center delay - modulation only, not audible echo
        // With wet/dry blend, we need small center (4ms) to avoid slapback
        // Modulation swings +/- from this center point
        const float wow_center_samples = 200.0f;  // ~4ms at 48kHz
        const float mod_delay_samples_l = fclamp(
            wow_center_samples + wow_mod_l + flutter_mod_l, 32.0f, 6000.0f);
        const float mod_delay_samples_r = fclamp(
            wow_center_samples + wow_mod_r + flutter_mod_r, 32.0f, 6000.0f);
        
        // Haze processing with dynamic drive
        // Add pre-gain to compensate for guitar vs Eurorack level difference
        // Guitar is ~10-20x quieter than Eurorack (±100mV vs ±5V)
        constexpr float haze_pre_gain = 12.0f;  // ~22dB boost to get guitar into saturation range
        
        const float in_energy_l = fabsf(in_l * haze_pre_gain);
        const float in_energy_r = fabsf(in_r * haze_pre_gain);
        const float bass_crumble_l = 1.0f + 2.5f * haze_collapse * in_energy_l;
        const float bass_crumble_r = 1.0f + 2.5f * haze_collapse * in_energy_r;
        
        float hazed_l = haze_filter_l.Process(in_l * haze_pre_gain * haze_drive_l);
        float hazed_r = haze_filter_r.Process(in_r * haze_pre_gain * haze_drive_r);
        
        // Hysteresis
        hyst_state_l += haze_hyst_follow_l * (hazed_l - hyst_state_l);
        hyst_state_r += haze_hyst_follow_r * (hazed_r - hyst_state_r);
        hazed_l += (hazed_l - hyst_state_l) * haze_hyst_mix_l;
        hazed_r += (hazed_r - hyst_state_r) * haze_hyst_mix_r;
        
        // Modulation noise injection
        hazed_l += haze_rough * (mod_l + 0.5f * RandBi());
        hazed_r += haze_rough * (mod_r + 0.5f * RandBi());
        
        // Saturation with dynamic bass crumble
        hazed_l = (tanhf((hazed_l + sat_bias_l) * sat_drive_l * bass_crumble_l)
                   - tanhf(sat_bias_l * sat_drive_l)) * haze_out_trim;
        hazed_r = (tanhf((hazed_r + sat_bias_r) * sat_drive_r * bass_crumble_r)
                   - tanhf(sat_bias_r * sat_drive_r)) * haze_out_trim;
        
        // Write to mod delay
        mod_delay_l.Write(hazed_l);
        mod_delay_r.Write(hazed_r);
        
        // Read modulated delay
        float warped_l = mod_delay_l.Read(mod_delay_samples_l);
        float warped_r = mod_delay_r.Read(mod_delay_samples_r);
        
        // Dropouts
        const float dropout_prob_base = 0.0003f;
        const float dropout_prob = fclamp(dropout_prob_base * (1.0f + 2.4f * mod_energy), 0.0f, 0.015f);
        const float dropout_depth = 0.85f;
        
        if (dropout_count_l > 0) {
            dropout_count_l--;
            dropout_target_l = dropout_depth;
        } else if (RandBi() > 1.0f - 2.0f * dropout_prob) {
            dropout_count_l = 400 + (int)(RandBi() * 300);
        } else {
            dropout_target_l = 1.0f;
        }
        
        if (dropout_count_r > 0) {
            dropout_count_r--;
            dropout_target_r = dropout_depth;
        } else if (RandBi() > 1.0f - 2.0f * dropout_prob) {
            dropout_count_r = 400 + (int)(RandBi() * 300);
        } else {
            dropout_target_r = 1.0f;
        }
        
        dropout_gain_l += 0.008f * (dropout_target_l - dropout_gain_l);
        dropout_gain_r += 0.008f * (dropout_target_r - dropout_gain_r);
        
        warped_l *= dropout_gain_l;
        warped_r *= dropout_gain_r;
        
        // Wet signal with noise added (noise already blended via knob 4 above)
        // Apply noise at fixed level - blend amount is controlled by knob 4 (tape_amount/vinyl_amount)
        const float noise_out_level = 0.18f;
        float wet_l = warped_l + noise_l * noise_out_level;
        float wet_r = warped_r + noise_r * noise_out_level;
        
        // Pad wet signal to compensate for haze pre-gain (12x boost)
        // Attenuate to bring wet signal closer to dry level
        constexpr float wet_pad = 0.18f;  // -15dB pad
        wet_l *= wet_pad;
        wet_r *= wet_pad;
        
        // Wet/dry mix using knob 6 (0 = clean/dry, 1 = full warble effect)
        // This allows chorus-like sounds by mixing dry signal with wow/flutter
        float mix_l = fclamp(in_l + wet_dry_blend * (wet_l - in_l), -1.0f, 1.0f);
        float mix_r = fclamp(in_r + wet_dry_blend * (wet_r - in_r), -1.0f, 1.0f);
        
        // Output HPF - 3-state via Toggle 2 (DOWN=bypass, MIDDLE=80Hz, UP=120Hz)
        float out_l, out_r;
        if (hpf_freq > 0.0f) {
            out_l = output_hpf_l.Process(mix_l);
            out_r = output_hpf_r.Process(mix_r);
        } else {
            out_l = mix_l;  // HPF bypassed
            out_r = mix_r;
        }
        
        // Output level with boost (unity gain, boost adds +4dB when active)
        out[0][i] = out_l * boost_gain;
        out[1][i] = out_r * boost_gain;
        
        // Update RMS and dust CV
        rms_env_left += 0.001f * (fabsf(in_l) - rms_env_left);
        dust_cv_env += 0.08f * (mod_energy - dust_cv_env);
    }
}

int main() {
    hw.Init();
    hw.SetAudioBlockSize(4);  // Small block size keeps latency low
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    sample_rate = hw.AudioSampleRate();
    
    srand(System::GetNow());
    
    // Initialize delay lines
    // Note: No SetDelay here - delay is set dynamically in AudioCallback
    // Initial delay is 0/minimal to avoid latency when effect is bypassed
    mod_delay_l.Init();
    mod_delay_r.Init();
    
    // Initialize filters
    haze_filter_l.Init(sample_rate);
    haze_filter_l.SetFilterMode(LadderFilter::FilterMode::LP24);
    haze_filter_r.Init(sample_rate);
    haze_filter_r.SetFilterMode(LadderFilter::FilterMode::LP24);
    
    output_hpf_l.Init(sample_rate);
    output_hpf_l.SetFilterMode(LadderFilter::FilterMode::HP12);
    output_hpf_l.SetFreq(35.0f);
    output_hpf_r.Init(sample_rate);
    output_hpf_r.SetFilterMode(LadderFilter::FilterMode::HP12);
    output_hpf_r.SetFreq(35.0f);
    
    // Initialize noise loops in SDRAM
    InitNoiseLoops();
    
    // LED 2 indicates whether the effect is engaged
    led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);
    
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    
    while (true) {
        led_bypass.Set(effect_bypass ? 0.0f : 1.0f);
        led_bypass.Update();
        System::Delay(10);
        
        // Standard Hothouse idiom: hold left footswitch 2s for DFU mode
        hw.CheckResetToBootloader();
    }
    return 0;
}
