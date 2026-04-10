#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "per/sdmmc.h"
#include "sys/fatfs.h"
#include "util/WavPlayer.h"
#include "ff.h"

#include <cmath>
#include <cstdlib>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

DaisyPatchSM patch;

static constexpr size_t kWavTransferSize = 16384;
static constexpr int    kNoisePairCount  = 3;

static DelayLine<float, 65536> mod_delay_l __attribute__((section(".sdram_bss")));
static DelayLine<float, 65536> mod_delay_r __attribute__((section(".sdram_bss")));

static SdmmcHandler               sdmmc;
static FatFSInterface             fatfs;
static WavPlayer<kWavTransferSize> tape_players[kNoisePairCount];
static WavPlayer<kWavTransferSize> vinyl_players[kNoisePairCount];
static WavPlayer<kWavTransferSize> mod_noise_stereo_player;
static WavPlayer<kWavTransferSize> mod_noise_l_player;
static WavPlayer<kWavTransferSize> mod_noise_r_player;

static bool sd_ready          = false;
static bool tape_loaded[kNoisePairCount]  = {false, false, false};
static bool vinyl_loaded[kNoisePairCount] = {false, false, false};
static bool mod_stereo_loaded = false;
static bool mod_l_loaded      = false;
static bool mod_r_loaded      = false;
static int  active_noise_pair = 0;

static LadderFilter haze_filter_l;
static LadderFilter haze_filter_r;
static LadderFilter output_hpf_l;
static LadderFilter output_hpf_r;
static Switch       toggle_hpf;
static Switch       cycle_noise_pair;

static float sample_rate        = 48000.0f;
static float wow_phase          = 0.0f;
static float flutter_phase_l    = 0.0f;
static float flutter_phase_r    = 0.37f;
static float wow_depth_smooth   = 0.0f;
static float wow_random_slew    = 0.0f;
static float flutter_noise_slew = 0.0f;
static float hyst_state_l       = 0.0f;
static float hyst_state_r       = 0.0f;

static float dropout_target_l = 1.0f;
static float dropout_target_r = 1.0f;
static float dropout_gain_l   = 1.0f;
static float dropout_gain_r   = 1.0f;
static int   dropout_count_l  = 0;
static int   dropout_count_r  = 0;

static float rms_env_left = 0.0f;
static float dust_cv_env  = 0.0f;

static inline float RandBi()
{
    return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

static inline float Tri(float phase)
{
    const float p = phase - floorf(phase);
    return 4.0f * fabsf(p - 0.5f) - 1.0f;
}

static inline float StageCurve(float x, float start, float end)
{
    const float t = fclamp((x - start) / (end - start), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

template <size_t N>
static bool InitLoopPlayer(WavPlayer<N>& player, const char* path)
{
    const auto res = player.Init(path);
    if(res != WavPlayer<N>::Result::Ok)
        return false;
    player.SetLooping(true);
    player.SetPlaying(true);
    player.Restart();
    return true;
}

static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
{
    patch.ProcessAnalogControls();

    const float knob_haze       = patch.GetAdcValue(CV_1);
    const float knob_wow_depth  = patch.GetAdcValue(CV_2);
    const float knob_flutter    = patch.GetAdcValue(CV_3);
    const float knob_noise_mode = patch.GetAdcValue(CV_4);

    const float cv_haze     = ((patch.GetAdcValue(CV_5) - 0.5f) * 2.0f) * 0.5f
                          + 0.5f;
    const float cv_wow_freq = ((patch.GetAdcValue(CV_6) - 0.5f) * 2.0f);
    const float cv_flutter    = ((patch.GetAdcValue(CV_7) - 0.5f) * 2.0f) * 0.5f;
    const float cv_drop_param = ((patch.GetAdcValue(CV_8) - 0.5f) * 2.0f) * 0.8f;

    const float wow_depth_raw = fclamp(knob_wow_depth, 0.0f, 1.0f);
    const float wow_deadband  = 0.03f;
    float       wow_depth_lin = 0.0f;
    if(wow_depth_raw > wow_deadband)
    {
        wow_depth_lin = (wow_depth_raw - wow_deadband) / (1.0f - wow_deadband);
    }
    wow_depth_smooth += 0.08f * (wow_depth_lin - wow_depth_smooth);
    const float wow_depth = fclamp(wow_depth_smooth, 0.0f, 1.0f);
    const float haze_ctrl = fclamp(knob_haze + cv_haze, 0.0f, 1.0f);

    const float haze_tone = StageCurve(haze_ctrl, 0.03f, 0.70f);
    const float haze_hyst = StageCurve(haze_ctrl, 0.18f, 0.86f);
    const float haze_sat = StageCurve(haze_ctrl, 0.42f, 1.00f);
    const float haze_collapse = StageCurve(haze_ctrl, 0.72f, 1.00f);
    const float flutter_amt = fclamp(knob_flutter + cv_flutter, 0.0f, 1.0f);

    const float wow_freq = fclamp(0.36f + cv_wow_freq * 0.16f, 0.20f, 0.60f);

    const float cv_drop_pos = fmaxf(cv_drop_param, 0.0f);
    const float cv_drop_neg = fmaxf(-cv_drop_param, 0.0f);

    const float dropout_depth
        = fclamp(0.08f + 0.60f * flutter_amt + 1.45f * cv_drop_pos - 0.20f * cv_drop_neg,
                 0.0f,
                 1.0f);
    const float dropout_rate_hz = fclamp(0.03f + 3.8f * flutter_amt + 10.0f * cv_drop_pos
                                             - 2.2f * cv_drop_neg,
                                         0.005f,
                                         15.0f);

    const float noise_centered = knob_noise_mode - 0.5f;
    const float noise_deadband = 0.10f;
    float       tape_mix       = 0.0f;
    float       vinyl_mix      = 0.0f;
    if(noise_centered < -noise_deadband)
    {
        tape_mix = fclamp((-noise_centered - noise_deadband) / (0.5f - noise_deadband),
                          0.0f,
                          1.0f);
    }
    else if(noise_centered > noise_deadband)
    {
        vinyl_mix = fclamp((noise_centered - noise_deadband) / (0.5f - noise_deadband),
                           0.0f,
                           1.0f);
    }

    const float wow_depth_shaped  = 0.10f * wow_depth + 0.90f * sqrtf(wow_depth);
    const float wow_depth_samples = wow_depth_shaped * 2200.0f;
    const float flutter_depth_samples = flutter_amt * 22.0f;

    const float haze_drive_l = 1.0f + 1.1f * haze_tone + 1.6f * haze_sat;
    const float haze_drive_r = 1.0f + 1.0f * haze_tone + 1.75f * haze_sat;
    const float haze_lowpass_hz
        = fclamp(19000.0f - 9000.0f * haze_tone - 5200.0f * haze_hyst - 3200.0f * haze_collapse,
                 450.0f,
                 20000.0f);
    const float haze_res = 0.12f + 0.30f * haze_tone + 0.24f * haze_collapse;
    const float haze_out_trim = fclamp(
        1.0f - 0.22f * haze_tone - 0.38f * haze_sat - 0.52f * haze_collapse,
        0.25f,
        1.0f);

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
    haze_filter_r.SetFreq(fclamp(haze_lowpass_hz * 0.92f, 800.0f, 20000.0f));
    haze_filter_l.SetRes(haze_res);
    haze_filter_r.SetRes(haze_res * 0.9f);

    toggle_hpf.Debounce();
    const bool hpf_enabled = toggle_hpf.Pressed();

    cycle_noise_pair.Debounce();
    if(cycle_noise_pair.RisingEdge() || patch.gate_in_1.Trig())
    {
        active_noise_pair = (active_noise_pair + 1) % kNoisePairCount;
        if(tape_loaded[active_noise_pair])
            tape_players[active_noise_pair].Restart();
        if(vinyl_loaded[active_noise_pair])
            vinyl_players[active_noise_pair].Restart();
    }

    const float hpf_freq = 120.0f;
    output_hpf_l.SetFreq(hpf_freq);
    output_hpf_r.SetFreq(hpf_freq);

    float left_sq_accum = 0.0f;

    const float wow_phase_inc       = wow_freq / sample_rate;
    const float flutter_phase_inc_l = ((4.8f + 2.2f * flutter_amt) * 0.8666667f) / sample_rate;
    const float flutter_phase_inc_r = ((6.1f + 2.4f * flutter_amt) * 0.8666667f) / sample_rate;
    const float dropout_prob_base    = dropout_rate_hz / sample_rate;

    for(size_t i = 0; i < size; i++)
    {
        const float in_l = IN_L[i];
        const float in_r = IN_R[i];

        float tape_samps[2]  = {0.0f, 0.0f};
        float vinyl_samps[2] = {0.0f, 0.0f};
        if(tape_loaded[active_noise_pair])
            tape_players[active_noise_pair].Stream(tape_samps, 2);
        if(vinyl_loaded[active_noise_pair])
            vinyl_players[active_noise_pair].Stream(vinyl_samps, 2);

        float mod_l = 0.0f;
        float mod_r = 0.0f;
        if(mod_stereo_loaded)
        {
            float mod_samps[2] = {0.0f, 0.0f};
            mod_noise_stereo_player.Stream(mod_samps, 2);
            mod_l = mod_samps[0];
            mod_r = mod_samps[1];
        }
        else
        {
            if(mod_l_loaded)
            {
                float mod_l_samp[1] = {0.0f};
                mod_noise_l_player.Stream(mod_l_samp, 1);
                mod_l = mod_l_samp[0];
            }
            if(mod_r_loaded)
            {
                float mod_r_samp[1] = {0.0f};
                mod_noise_r_player.Stream(mod_r_samp, 1);
                mod_r = mod_r_samp[0];
            }
        }

        const float mod_energy = fclamp((fabsf(mod_l) + fabsf(mod_r)) * 0.5f, 0.0f, 1.0f);

        wow_phase += wow_phase_inc;
        if(wow_phase >= 1.0f)
            wow_phase -= 1.0f;

        flutter_phase_l += flutter_phase_inc_l;
        flutter_phase_r += flutter_phase_inc_r;
        if(flutter_phase_l >= 1.0f)
            flutter_phase_l -= 1.0f;
        if(flutter_phase_r >= 1.0f)
            flutter_phase_r -= 1.0f;

        wow_random_slew += 0.00035f * ((0.5f * RandBi() + 0.8f * mod_l) - wow_random_slew);
        flutter_noise_slew += 0.0016f * ((0.4f * RandBi() + 0.9f * mod_r) - flutter_noise_slew);

        const float wow_tri_l  = Tri(wow_phase);
        const float wow_tri_r  = Tri(wow_phase + 0.25f);
        const float wow_rand_l = wow_random_slew;
        const float wow_rand_r = wow_random_slew;
        const float wow_lfo_l = fclamp(wow_tri_l * 0.70f + wow_rand_l * 0.30f, -1.0f, 1.0f);
        const float wow_lfo_r = fclamp(wow_tri_r * 0.68f + wow_rand_r * 0.32f, -1.0f, 1.0f);
        const float flutter_lfo_l = Tri(flutter_phase_l) * 0.8f + flutter_noise_slew * 0.45f;
        const float flutter_lfo_r = Tri(flutter_phase_r) * 0.8f - flutter_noise_slew * 0.42f;

        const float wow_mod_l = wow_lfo_l * wow_depth_samples;
        const float wow_mod_r = wow_lfo_r * wow_depth_samples;
        const float flutter_mod_l = flutter_lfo_l * flutter_depth_samples;
        const float flutter_mod_r = flutter_lfo_r * flutter_depth_samples;

        const float wow_center_samples = 2600.0f;
        const float mod_delay_samples_l
            = fclamp(wow_center_samples + wow_mod_l + flutter_mod_l, 32.0f, 9000.0f);
        const float mod_delay_samples_r
            = fclamp(wow_center_samples + wow_mod_r + flutter_mod_r, 32.0f, 9000.0f);

        float hazed_l = haze_filter_l.Process(in_l * haze_drive_l);
        float hazed_r = haze_filter_r.Process(in_r * haze_drive_r);

        hyst_state_l += haze_hyst_follow_l * (hazed_l - hyst_state_l);
        hyst_state_r += haze_hyst_follow_r * (hazed_r - hyst_state_r);
        hazed_l += (hazed_l - hyst_state_l) * haze_hyst_mix_l;
        hazed_r += (hazed_r - hyst_state_r) * haze_hyst_mix_r;

        hazed_l += haze_rough * (mod_l + 0.5f * RandBi());
        hazed_r += haze_rough * (mod_r + 0.5f * RandBi());

        hazed_l = (tanhf((hazed_l + sat_bias_l) * sat_drive_l)
                   - tanhf(sat_bias_l * sat_drive_l))
                  * haze_out_trim;
        hazed_r = (tanhf((hazed_r + sat_bias_r) * sat_drive_r)
                   - tanhf(sat_bias_r * sat_drive_r))
                 * haze_out_trim;

        mod_delay_l.Write(hazed_l);
        mod_delay_r.Write(hazed_r);

        float warped_l = mod_delay_l.Read(mod_delay_samples_l);
        float warped_r = mod_delay_r.Read(mod_delay_samples_r);

        const float dropout_prob = fclamp(dropout_prob_base * (1.0f + 2.4f * mod_energy),
                                          0.0f,
                                          0.05f);

        if(dropout_count_l <= 0 && ((float)rand() / (float)RAND_MAX) < dropout_prob)
        {
            dropout_count_l  = 20 + (int)(rand() % 520);
            dropout_target_l
                = fclamp(1.0f - dropout_depth * (0.72f + 0.52f * ((float)rand() / (float)RAND_MAX)),
                         0.0f,
                         1.0f);
        }
        if(dropout_count_r <= 0 && ((float)rand() / (float)RAND_MAX) < dropout_prob * 0.9f)
        {
            dropout_count_r  = 20 + (int)(rand() % 600);
            dropout_target_r
                = fclamp(1.0f - dropout_depth * (0.70f + 0.54f * ((float)rand() / (float)RAND_MAX)),
                         0.0f,
                         1.0f);
        }

        if(dropout_count_l > 0)
        {
            dropout_count_l--;
            if(dropout_count_l == 0)
                dropout_target_l = 1.0f;
        }
        if(dropout_count_r > 0)
        {
            dropout_count_r--;
            if(dropout_count_r == 0)
                dropout_target_r = 1.0f;
        }

        dropout_gain_l += 0.02f * (dropout_target_l - dropout_gain_l);
        dropout_gain_r += 0.02f * (dropout_target_r - dropout_gain_r);

        warped_l *= dropout_gain_l;
        warped_r *= dropout_gain_r;

        const float tape_noise_l  = tape_samps[0] * 0.21f;
        const float tape_noise_r  = tape_samps[1] * 0.21f;
        const float vinyl_noise_l = vinyl_samps[0] * 0.21f;
        const float vinyl_noise_r = vinyl_samps[1] * 0.21f;

        const float noise_l = tape_noise_l * tape_mix + vinyl_noise_l * vinyl_mix;
        const float noise_r = tape_noise_r * tape_mix + vinyl_noise_r * vinyl_mix;

        float out_l = warped_l + noise_l;
        float out_r = warped_r + noise_r;

        if(hpf_enabled)
        {
            out_l = output_hpf_l.Process(out_l);
            out_r = output_hpf_r.Process(out_r);
        }

        out_l = tanhf(out_l * 1.0f);
        out_r = tanhf(out_r * 1.0f);

        OUT_L[i] = out_l;
        OUT_R[i] = out_r;

        left_sq_accum += in_l * in_l;
        const float dust_cv_src = fclamp((fabsf(noise_l) + fabsf(noise_r)) * 4.0f + mod_energy * 2.2f,
                                         0.0f,
                                         1.0f);
        dust_cv_env += 0.02f * (dust_cv_src - dust_cv_env);
    }

    const float rms_left = sqrtf(left_sq_accum / (float)size);
    rms_env_left += 0.08f * (rms_left - rms_env_left);

    patch.WriteCvOut(CV_OUT_1, fclamp(dust_cv_env * 3.0f, 0.0f, 1.0f) * 5.0f);
    patch.WriteCvOut(CV_OUT_2, fclamp(rms_env_left * 8.0f, 0.0f, 1.0f) * 5.0f);
}

int main(void)
{
    patch.Init();

    sample_rate = patch.AudioSampleRate();
    patch.SetAudioBlockSize(48);

    mod_delay_l.Init();
    mod_delay_r.Init();

    haze_filter_l.Init(sample_rate);
    haze_filter_r.Init(sample_rate);
    haze_filter_l.SetFilterMode(LadderFilter::FilterMode::LP24);
    haze_filter_r.SetFilterMode(LadderFilter::FilterMode::LP24);
    haze_filter_l.SetInputDrive(1.0f);
    haze_filter_r.SetInputDrive(1.0f);
    haze_filter_l.SetPassbandGain(1.0f);
    haze_filter_r.SetPassbandGain(1.0f);

    output_hpf_l.Init(sample_rate);
    output_hpf_r.Init(sample_rate);
    output_hpf_l.SetFilterMode(LadderFilter::FilterMode::HP24);
    output_hpf_r.SetFilterMode(LadderFilter::FilterMode::HP24);
    output_hpf_l.SetRes(0.2f);
    output_hpf_r.SetRes(0.2f);
    output_hpf_l.SetInputDrive(1.0f);
    output_hpf_r.SetInputDrive(1.0f);
    output_hpf_l.SetPassbandGain(1.0f);
    output_hpf_r.SetPassbandGain(1.0f);

    toggle_hpf.Init(patch.B8, patch.AudioCallbackRate());
    cycle_noise_pair.Init(patch.B7, patch.AudioCallbackRate());

    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_cfg.speed = SdmmcHandler::Speed::STANDARD;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_1;

    if(sdmmc.Init(sd_cfg) == SdmmcHandler::Result::OK
       && fatfs.Init(FatFSInterface::Config::Media::MEDIA_SD)
              == FatFSInterface::Result::OK
       && f_mount(&fatfs.GetSDFileSystem(), "/", 1) == FR_OK)
    {
        sd_ready     = true;
        const char* tape_paths[kNoisePairCount] = {
            "cheapheataudio/cheapheat_tape.wav",
            "cheapheataudio/cheapheat_tape2.wav",
            "cheapheataudio/cheapheat_tape3.wav",
        };
        const char* vinyl_paths[kNoisePairCount] = {
            "cheapheataudio/cheapheat_vinyl.wav",
            "cheapheataudio/cheapheat_vinyl2.wav",
            "cheapheataudio/cheapheat_vinyl3.wav",
        };

        for(int i = 0; i < kNoisePairCount; i++)
        {
            tape_loaded[i]  = InitLoopPlayer(tape_players[i], tape_paths[i]);
            vinyl_loaded[i] = InitLoopPlayer(vinyl_players[i], vinyl_paths[i]);
        }

        mod_stereo_loaded
            = InitLoopPlayer(mod_noise_stereo_player,
                             "cheapheataudio/stereonoise.wav");
        if(!mod_stereo_loaded)
        {
            mod_l_loaded = InitLoopPlayer(mod_noise_l_player,
                                          "cheapheataudio/mononoiseL.wav");
            mod_r_loaded = InitLoopPlayer(mod_noise_r_player,
                                          "cheapheataudio/mononoiseR.wav");
        }
    }

    srand(1);
    patch.StartAudio(AudioCallback);

    while(1)
    {
        if(sd_ready)
        {
            for(int i = 0; i < kNoisePairCount; i++)
            {
                if(tape_loaded[i])
                    tape_players[i].Prepare();
                if(vinyl_loaded[i])
                    vinyl_players[i].Prepare();
            }
            if(mod_stereo_loaded)
            {
                mod_noise_stereo_player.Prepare();
            }
            else
            {
                if(mod_l_loaded)
                    mod_noise_l_player.Prepare();
                if(mod_r_loaded)
                    mod_noise_r_player.Prepare();
            }
        }
    }
}
