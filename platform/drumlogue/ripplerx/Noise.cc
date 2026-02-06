#include "Noise.h"
#include "constants.h"

void Noise::init(float32_t _srate, int filterMode, float32_t _freq, float32_t _q, float32_t att, float32_t dec, float32_t sus, float32_t rel, float32_t _vel_freq, float32_t _vel_q)
{
	srate = _srate;
	fmode = filterMode;
	freq = _freq;
	q = _q;
	vel_freq = _vel_freq;
	vel_q = _vel_q;
	initFilter();
	env.init(srate, att, dec, sus, rel, 0.4, 0.4, 0.4);
}

void Noise::attack(float32_t _vel)
{
	vel = _vel;
	initFilter();
	env.attack(1.0);
}

/**
 * @brief optimized version bit bit manipulation:
 * To use this, we change the base from $e$ to $2$.
 * $e^x = 2^{x \cdot \log_2(e)}$ where $\log_2(e) \approx 1.442695$.
 *
 */
void Noise::initFilter() {
    // Constants
    const float log2e = 1.44269504089f;

    // Instead of e^log_f, we calculate 2^(log2e * log_f)
    // We can pre-multiply the range constant by log2e!
    float32_t offset = vel * (vel_freq * (c_noise_filter_log_range * log2e));

    // f = freq * 2^offset
    // We use the bit-shift trick to skip the exp function entirely
    union { float f; int32_t i; } u;
    u.i = (int32_t)(offset * 8388608.0f) + 1065353216;
    float32_t f = freq * u.f;

    // Clamping using ARM VFP instructions (VMIN/VMAX)
    f = fminf(c_noise_filter_freq_max, fmaxf(c_noise_filter_freq_min, f));

    // Resonance
    float32_t res = q + (vel * vel_q * c_noise_filter_res_range);
    res = fminf(4.0f, fmaxf(0.707f, res));

    // Logic Dispatch
    filter_active = (fmode == 1) || (fmode == 0 && f < 20000.0f) || (fmode == 2 && f > 20.0f);
    if (!filter_active) return;

    // Jump Table / Switch
    switch (fmode) {
        case 1:  filter.bp(srate, f, res); break;
        case 2:  filter.hp(srate, f, res); break;
        default: filter.lp(srate, f, res); break;
    }
}

void Noise::release()
{
	env.release();
}

void Noise::clear()
{
	env.reset();
	filter.clear(0.0f);
}
