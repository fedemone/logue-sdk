#include "Mallet.h"

void Mallet::trigger(MalletType _type, float32_t _srate, float32_t freq)
{
	type = _type;
	srate = _srate;

	if (type == kImpulse) {
		impulse_filter.bp(srate, freq, 0.707);
		countdown = (int)(srate / 10.0); // countdown (100ms)
		impulse = 1.0;
		env = e_expff(-100.0 / srate);
	}
	// else {
	// 	playback_speed = sampler.wavesrate / srate;
	// 	playback = 0.0;
	// }
}

void Mallet::clear()
{
	countdown = 0;
	impulse = 0.0;
	// playback = INFINITY;
	impulse_filter.clear(0.0);
	// sample_filter.clear(0.0);
}

float32_t Mallet::process()
{
	auto sample = 0.0;

	if (type == kImpulse && countdown > 0) {
		sample = impulse_filter.df1(impulse) * 2.0;
		countdown -= 1;
		impulse *= env;
	}   // disable sampler for the moment
	// else if (type >= kUserFile && playback < sampler.waveform.size()) {
	// 	sample = sampler.waveCubic(playback);
	// 	playback += playback_speed * sampler.pitchfactor;

	// 	if (!disable_filter) {
	// 		sample = sample_filter.df1(sample);
	// 	}
	// }

	return sample;
}

// disable sampler for the moment
// void Mallet::setFilter(float32_t norm)
// {
// 	float32_t freq = 20.0 * fasterpowf(20000.0/20.0, norm < 0.0 ? 1 + norm : norm); // map 1..0 to 20..20000, with inverse scale for negative norm
	// disable_filter = norm == 0.0;
	// if (!disable_filter) {
	// 	if (norm < 0.0) {
	// 		sample_filter.lp(srate, freq, 0.707);
	// 	}
	// 	else {
	// 		sample_filter.hp(srate, freq, 0.707);
	// 	}
	// }
// }