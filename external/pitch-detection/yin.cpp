// Probabilistic YIN, from sevagh/pitch-detection
// (https://github.com/sevagh/pitch-detection), MIT License,
// Copyright (c) 2018 Sevag Hanssian. See LICENSE.
//
// Ikaros: src/yin.cpp, with the parts of src/autocorrelation.cpp and
// src/parabolic_interpolation.cpp it uses, changed for Ikaros Studio (see
// README.ikaros). Every change is marked "Ikaros:".

#include "pitch_detection.h"
#include <algorithm>
#include <cmath>

#define YIN_THRESHOLD 0.20
#define PYIN_PA 0.01
#define PYIN_N_THRESHOLDS 100
#define PYIN_MIN_THRESHOLD 0.025

// Beta distribution with mean 0.1 and beta 18
// generated with /misc/generate_beta_distribution.py, first parameter set '0'
static const float Beta_Distribution_0[100] = {0.000000, 0.029069, 0.048836,
    0.061422, 0.068542, 0.071571, 0.071607, 0.069516, 0.065976, 0.061512,
    0.056523, 0.051309, 0.046089, 0.041021, 0.036211, 0.031727, 0.027608,
    0.023871, 0.020517, 0.017534, 0.014903, 0.012601, 0.010601, 0.008875,
    0.007393, 0.006130, 0.005059, 0.004155, 0.003397, 0.002765, 0.002239,
    0.001806, 0.001449, 0.001157, 0.000920, 0.000727, 0.000572, 0.000448,
    0.000349, 0.000271, 0.000209, 0.000160, 0.000122, 0.000092, 0.000070,
    0.000052, 0.000039, 0.000029, 0.000021, 0.000015, 0.000011, 0.000008,
    0.000006, 0.000004, 0.000003, 0.000002, 0.000001, 0.000001, 0.000001,
    0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
    0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
    0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
    0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
    0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
    0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000};

namespace pitch_alloc
{

// Ikaros: the allocation that BaseAlloc and Yin's constructors did, with
// juce::dsp::FFT in place of FFTS. The FFT is twice the frame (or more), so
// the correlation below doesn't wrap around.
Yin::Yin(int audio_buffer_size, int sample_rate_, float min_pitch, float max_pitch)
    : nfft(audio_buffer_size), sample_rate(sample_rate_),
      yin_buffer_size(audio_buffer_size / 2),
      yin_buffer(std::vector<float>(audio_buffer_size / 2))
{
	tau_min = std::max(2, (int)std::floor(sample_rate / max_pitch));
	tau_max = std::min(yin_buffer_size - 2, (int)std::ceil(sample_rate / min_pitch));

	int order = 1;
	while ((1 << order) < nfft + yin_buffer_size)
		++order;

	fft = std::make_unique<juce::dsp::FFT>(order);
	frame_spectrum.assign((size_t)(2 << order), 0.0f);
	half_spectrum.assign((size_t)(2 << order), 0.0f);
	energy.assign((size_t)nfft + 1, 0.0);
	running_min.assign((size_t)yin_buffer_size, 1.0f);
}

// Ikaros: the YIN difference function
//     d(tau) = sum over j < W of (x[j] - x[j + tau])^2,   W = half the frame
//            = energy of x[0, W) + energy of x[tau, tau + W) - 2 r(tau)
// with r(tau) = sum over j < W of x[j] x[j + tau], the correlation of the
// frame's first half with the whole frame, found with FFTs. The original
// used a circular autocorrelation of the whole frame, and out_real[0] +
// out_real[1] for both energy terms; this is the exact function.
void
Yin::difference(const float *audio_buffer)
{
	const int size = fft->getSize();

	std::fill(frame_spectrum.begin(), frame_spectrum.end(), 0.0f);
	std::fill(half_spectrum.begin(), half_spectrum.end(), 0.0f);
	std::copy(audio_buffer, audio_buffer + nfft, frame_spectrum.begin());
	std::copy(audio_buffer, audio_buffer + yin_buffer_size, half_spectrum.begin());

	fft->performRealOnlyForwardTransform(frame_spectrum.data(), true);
	fft->performRealOnlyForwardTransform(half_spectrum.data(), true);

	// frame * conj(half), bin by bin (bins 0 to size / 2, as (re, im) pairs)
	for (int i = 0; i <= size / 2; ++i) {
		const float a = frame_spectrum[2 * i], b = frame_spectrum[2 * i + 1];
		const float c = half_spectrum[2 * i], d = half_spectrum[2 * i + 1];
		frame_spectrum[2 * i] = a * c + b * d;
		frame_spectrum[2 * i + 1] = b * c - a * d;
	}

	fft->performRealOnlyInverseTransform(frame_spectrum.data());   // divides by size: r(tau) as it is

	energy[0] = 0.0;
	for (int j = 0; j < nfft; ++j)
		energy[(size_t)j + 1] = energy[(size_t)j] + (double)audio_buffer[j] * audio_buffer[j];

	const double first = energy[(size_t)yin_buffer_size];

	for (int tau = 0; tau < yin_buffer_size; tau++) {
		const double shifted = energy[(size_t)(tau + yin_buffer_size)] - energy[(size_t)tau];
		yin_buffer[(size_t)tau] = (float)std::max(0.0, first + shifted - 2.0 * frame_spectrum[(size_t)tau]);
	}
}

void
Yin::cumulative_mean_normalized_difference()
{
	float running_sum = 0.0f;

	yin_buffer[0] = 1;

	for (int tau = 1; tau < signed(yin_buffer.size()); tau++) {
		running_sum += yin_buffer[tau];
		// Ikaros: silence gives 0 / 0; call it not periodic at all.
		yin_buffer[tau] = running_sum > 0.0f ? yin_buffer[tau] * tau / running_sum : 1.0f;
	}
}

// Ikaros: within tau_min..tau_max (the original looked from tau = 2 to the end).
int
Yin::absolute_threshold() const
{
	int tau;
	for (tau = tau_min; tau <= tau_max; tau++) {
		if (yin_buffer[tau] < YIN_THRESHOLD) {
			while (tau + 1 <= tau_max && yin_buffer[tau + 1] < yin_buffer[tau]) {
				tau++;
			}
			break;
		}
	}
	return (tau > tau_max || yin_buffer[tau] >= YIN_THRESHOLD) ? -1 : tau;
}

// From src/parabolic_interpolation.cpp: the dip's position between samples.
// Ikaros: returns only the position (the original also returned the value).
float
Yin::parabolic_interpolation(int x_) const
{
	const auto &array = yin_buffer;
	int x_adjusted;
	float x = (float)x_;

	if (x_ < 1) {
		x_adjusted = (array[x_] <= array[x_ + 1]) ? x_ : x_ + 1;
	} else if (x_ >= signed(array.size()) - 1) {
		x_adjusted = (array[x_] <= array[x_ - 1]) ? x_ : x_ - 1;
	} else {
		float den = array[x_ + 1] + array[x_ - 1] - 2 * array[x_];
		float delta = array[x_ - 1] - array[x_ + 1];
		return (!den) ? x : x + delta / (2 * den);
	}
	return (float)x_adjusted;
}

float
Yin::pitch(const float *audio_buffer)
{
	int tau_estimate;

	difference(audio_buffer);

	cumulative_mean_normalized_difference();
	tau_estimate = absolute_threshold();

	return (tau_estimate != -1) ? sample_rate / parabolic_interpolation(tau_estimate) : -1;
}

// pYIN's first stage: for each of 100 thresholds, the first dip below it
// (walked down to its bottom) is a candidate period, weighted by the
// threshold's probability under a beta distribution. A frame where the
// candidates' dips were found has a high probability of being pitched.
//
// Ikaros: (1) the candidates are found with a running minimum and a binary
// search instead of scanning from the start for every threshold (the same
// candidates, a hundred times fewer comparisons); (2) when no dip is below a
// threshold, the lowest point gets that threshold's weight times PYIN_PA,
// as pYIN describes (the original read past the buffer's end there);
// (3) instead of mlpack's HMM, the most probable candidate is the frame's
// pitch, and the probabilities of the dips found make its voicing
// probability.
Yin::Result
Yin::probabilistic_pitch(const float *audio_buffer)
{
	difference(audio_buffer);

	cumulative_mean_normalized_difference();

	// running_min[tau]: the lowest value from tau_min up to tau. It never
	// rises, so "the first tau below a threshold" is a binary search.
	int lowest = tau_min;
	for (int tau = tau_min; tau <= tau_max; tau++) {
		if (yin_buffer[tau] < yin_buffer[lowest])
			lowest = tau;
		running_min[tau] = yin_buffer[lowest];
	}

	// (tau, probability) pairs; there are only ever a few different taus.
	std::pair<int, float> candidates[PYIN_N_THRESHOLDS];
	int num_candidates = 0;
	float voiced_probability = 0.0f;

	auto add = [&](int tau, float probability) {
		for (int i = 0; i < num_candidates; ++i) {
			if (candidates[i].first == tau) {
				candidates[i].second += probability;
				return;
			}
		}
		candidates[num_candidates++] = {tau, probability};
	};

	for (int n = 0; n < PYIN_N_THRESHOLDS; ++n) {
		const float threshold = (n + 1) * (float)PYIN_MIN_THRESHOLD;

		if (running_min[tau_max] >= threshold) {
			add(lowest, (float)PYIN_PA * Beta_Distribution_0[n]);
			continue;
		}

		const auto first = std::upper_bound(running_min.begin() + tau_min, running_min.begin() + tau_max + 1,
		                                    threshold, [](float t, float v) { return v < t; });
		int tau = (int)(first - running_min.begin());

		while (tau + 1 <= tau_max && yin_buffer[tau + 1] < yin_buffer[tau]) {
			tau++;
		}

		add(tau, Beta_Distribution_0[n]);
		voiced_probability += Beta_Distribution_0[n];
	}

	Result result;
	float best = -1.0f;

	for (int i = 0; i < num_candidates; ++i) {
		if (candidates[i].second > best) {
			best = candidates[i].second;
			result.pitch = sample_rate / parabolic_interpolation(candidates[i].first);
			result.aperiodicity = yin_buffer[candidates[i].first];
		}
	}

	result.probability = std::min(1.0f, voiced_probability);
	return result;
}

} // namespace pitch_alloc
