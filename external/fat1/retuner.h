// -----------------------------------------------------------------------
//
//  Copyright (C) 2009-2011 Fons Adriaensen <fons@linuxaudio.org>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// -----------------------------------------------------------------------
//
//  Ikaros: changed for Ikaros Studio (see README.ikaros). FFTW is replaced
//  by juce::dsp::FFT and the bundled zita-resampler by
//  juce::dsp::Oversampling; buffers are std::vectors. A few getters were
//  added for the effect's INPUT -> TARGET readout. Every change is marked
//  "Ikaros:".

#ifndef __RETUNER_H
#define __RETUNER_H

// Ikaros: JUCE's FFT and oversampling instead of FFTW and zita-resampler.
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace LV2AT {

class Retuner
{
public:
	// Ikaros: allocates everything it needs, so call it off the audio thread.
	Retuner (int fsamp);
	~Retuner (void);

	int process (int nfram, float const* inp, float* out);

	void
	set_refpitch (float v)
	{
		_refpitch = v;
	}

	void
	set_notebias (float v)
	{
		_notebias = v / 13.0f;
	}

	void
	set_corrfilt (float v)
	{
		_corrfilt = (4 * _frsize) / (v * _fsamp);
	}

	void
	set_corrgain (float v)
	{
		_corrgain = v;
	}

	void
	set_corroffs (float v)
	{
		_corroffs = v;
	}

	void
	set_notemask (int k)
	{
		_notemask = k;
	}

	void
	set_notescale (int i, float v)
	{
		_notescale[i] = i + v;
	}

	void
	set_fastmode (float v)
	{
		_fastmode = (bool)v;
	}

	int
	get_noteset (void)
	{
		int k     = _notebits;
		_notebits = 0;
		return k;
	}

	float
	get_error (void)
	{
		return 12.0f * _error;
	}

	bool
	upsample (void) const
	{
		return _upsamp;
	}

	// Ikaros: what the readout shows. While voiced, the input's pitch period
	// (in samples at fsamp) and the note it's being moved to (0 = C ... 11 = B).
	bool
	is_voiced (void) const
	{
		return _voiced;
	}

	float
	get_cycle (void) const
	{
		return _cycle;
	}

	int
	get_lastnote (void) const
	{
		return _lastnote;
	}

	// Ikaros: the delay from input to output, in samples at fsamp, without
	// fast mode (half the input buffer), and how much earlier fast mode
	// reads (the read-ahead, in samples at fsamp). The upsampler's own delay
	// is extra: see get_upsampler_delay().
	int
	get_latency (void) const
	{
		return _upsamp ? _ipsize / 4 : _ipsize / 2;
	}

	int
	get_fastmode_lead (void) const
	{
		return _upsamp ? (_ipsize * 7 / 16) / 2 : _ipsize * 7 / 16;
	}

	double
	get_upsampler_delay (void) const
	{
		return _updelay;
	}

private:
	float findcycle (void);
	void  finderror (void);

	static float cubic (float* v, float a);

	int   _fsamp;
	int   _ifmin;
	int   _ifmax;
	bool  _upsamp;
	int   _fftlen;
	int   _ipsize;
	int   _frsize;
	int   _ipindex;
	int   _frindex;
	int   _frcount;
	float _refpitch;
	float _notebias;
	float _corrfilt;
	float _corrgain;
	float _corroffs;
	int   _notemask;
	int   _notebits;
	int   _lastnote;
	int   _count;
	float _cycle;
	float _error;
	float _ratio;
	float _phase;
	bool  _xfade;
	float _rindex1;
	float _rindex2;

	// Ikaros: std::vectors instead of new[] and fftwf_malloc. _fftTdata is
	// twice the FFT length: juce::dsp::FFT works in place on interleaved
	// complex values (real, imaginary, real, imaginary...).
	std::vector<float> _ipbuff;   // Resampled or filtered input
	std::vector<float> _xffunc;   // Crossfade function
	std::vector<float> _fftTwind; // Window function
	std::vector<float> _fftWcorr; // Autocorrelation of window
	std::vector<float> _fftTdata; // Time domain data for FFT (and its spectrum)

	// Ikaros: replaces the FFTW plans.
	std::unique_ptr<juce::dsp::FFT> _fft;
	void forward (float* data);   // real -> complex, in place
	void inverse (float* data);   // complex -> real, in place, unnormalised like FFTW's

	// Ikaros: replaces LV2AT::Resampler (zita-resampler) for the 2x
	// upsampling at 44.1 and 48 kHz. Only its "up" half is used.
	juce::dsp::Oversampling<float> _upsampler {
		1, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false
	};
	double _updelay = 0.0; // its delay, in samples at fsamp

	float _notescale[12];
	bool  _fastmode;
	int   _readahead;
	int   _lastreadahead;
	bool  _voiced;
};

}; // namespace LV2AT

#endif
