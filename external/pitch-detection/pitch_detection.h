// Probabilistic YIN, from sevagh/pitch-detection
// (https://github.com/sevagh/pitch-detection), MIT License,
// Copyright (c) 2018 Sevag Hanssian. See LICENSE.
//
// Ikaros: changed for Ikaros Studio (see README.ikaros). Only YIN and its
// probabilistic version are kept, for one frame size at a time. FFTS is
// replaced by juce::dsp::FFT, and mlpack's HMM by picking the most probable
// candidate (the pitch analysis smooths across frames itself). Every change
// is marked "Ikaros:".

#ifndef PITCH_DETECTION_H
#define PITCH_DETECTION_H

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace pitch_alloc
{

/*
 * Allocate the buffers for YIN for re-use.
 * Intended for multiple consistently-sized audio buffers.
 *
 * Usage: pitch_alloc::Yin ya(2048)
 *
 * Ikaros: not a template (float only), and not thread-safe: use one per thread.
 */
class Yin
{
  public:
	// Ikaros: what probabilistic_pitch() finds in one frame.
	struct Result {
		float pitch = -1.0f;        // Hz, or -1 if there's no candidate at all
		float probability = 0.0f;   // how likely the frame is pitched (0..1): the
		                            // thresholds' probabilities where a dip was found
		float aperiodicity = 1.0f;  // the normalised difference at the chosen
		                            // period: 0 = perfectly periodic, ~1 = noise
	};

	// Ikaros: the pitch range to look in (Hz) is fixed per instance, so
	// every frame looks only at periods that matter.
	Yin(int audio_buffer_size, int sample_rate, float min_pitch, float max_pitch);

	// Plain YIN: the pitch in Hz, or -1.
	float
	pitch(const float *audio_buffer);

	// The probabilistic version (pYIN's first stage).
	Result
	probabilistic_pitch(const float *audio_buffer);

	int
	buffer_size() const
	{
		return nfft;
	}

  private:
	int nfft;                       // the frame length
	int sample_rate;
	int tau_min, tau_max;           // Ikaros: the periods looked at, in samples
	int yin_buffer_size;
	std::vector<float> yin_buffer;

	// Ikaros: the difference function, through juce::dsp::FFT (see difference()).
	std::unique_ptr<juce::dsp::FFT> fft;
	std::vector<float> frame_spectrum, half_spectrum;
	std::vector<double> energy;     // running sum of the squared samples
	std::vector<float> running_min; // the lowest yin_buffer value from tau_min up to each tau

	void difference(const float *audio_buffer);
	void cumulative_mean_normalized_difference();
	int absolute_threshold() const;
	float parabolic_interpolation(int tau) const;
};

} // namespace pitch_alloc

#endif /* PITCH_DETECTION_H */
