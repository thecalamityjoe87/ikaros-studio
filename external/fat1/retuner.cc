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
//  Ikaros: changed for Ikaros Studio (see README.ikaros and retuner.h).
//  Every change is marked "Ikaros:".

#include "retuner.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Ikaros: M_PI isn't defined by MSVC's <math.h> unless asked for.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace LV2AT;

Retuner::Retuner (int fsamp)
    : _fsamp (fsamp)
    , _refpitch (440.0f)
    , _notebias (0.0f)
    , _corrfilt (1.0f)
    , _corrgain (1.0f)
    , _corroffs (0.0f)
    , _notemask (0xFFF)
    , _fastmode (false)
    , _readahead (0)
    , _lastreadahead (0)
    , _voiced(false)
{
	int   i, h;
	float t, x, y;

	if (_fsamp < 64000) {
		// At 44.1 and 48 kHz resample to double rate.
		_upsamp = true;
		_ipsize = 4096;
		_fftlen = 2048;
		_frsize = 128;
		// Ikaros: a linear-phase half-band FIR from juce::dsp::Oversampling
		// instead of zita-resampler. Its delay is measured below and
		// reported with the latency (see get_upsampler_delay()).
	} else if (_fsamp < 128000) {
		// 88.2 or 96 kHz.
		_upsamp = false;
		_ipsize = 4096;
		_fftlen = 4096;
		_frsize = 256;
	} else {
		// 192 kHz, double time domain buffers sizes.
		_upsamp = false;
		_ipsize = 8192;
		_fftlen = 8192;
		_frsize = 512;
	}

	// Accepted correlation peak range, corresponding to 60..1200 Hz.
	_ifmin = _fsamp / 1200;
	_ifmax = _fsamp / 60;

	// Various buffers (Ikaros: std::vectors, zeroed as they're made)
	_ipbuff.assign (_ipsize + 3, 0.0f);       // Resampled or filtered input
	_xffunc.assign (_frsize, 0.0f);           // Crossfade function
	_fftTwind.assign (_fftlen, 0.0f);         // Window function
	_fftWcorr.assign (_fftlen, 0.0f);         // Autocorrelation of window
	_fftTdata.assign (2 * _fftlen, 0.0f);     // Time domain data for FFT, and its spectrum

	// Ikaros: one juce::dsp::FFT replaces the two FFTW plans. Its order is
	// log2 of the length (2048 -> 11).
	int order = 0;
	while ((1 << order) < _fftlen)
		++order;
	_fft = std::make_unique<juce::dsp::FFT> (order);

	// Ikaros: the upsampler, for blocks up to one fragment long. Its delay
	// is found by sending a click through it (the filter is linear phase,
	// so it's the same for every frequency).
	if (_upsamp) {
		_upsampler.initProcessing ((size_t)_frsize);
		std::vector<float> click ((size_t)_frsize, 0.0f);
		float              peak = 0.0f;
		int                peakat = 0;
		for (int block = 0; block < 8; ++block) {
			click[0]          = block == 0 ? 1.0f : 0.0f;
			const float* ch[] = { click.data () };
			auto         up   = _upsampler.processSamplesUp (juce::dsp::AudioBlock<const float> (ch, 1, (size_t)_frsize));
			for (int s = 0; s < 2 * _frsize; ++s) {
				const float v = fabsf (up.getSample (0, s));
				if (v > peak) {
					peak   = v;
					peakat = block * 2 * _frsize + s;
				}
			}
		}
		_updelay = peakat / 2.0;
		_upsampler.reset ();
	}

	// Create crossfade function, half of raised cosine.
	for (i = 0; i < _frsize; i++) {
		_xffunc[i] = 0.5 * (1 - cosf (M_PI * i / _frsize));
	}

	// Create window, raised cosine.
	for (i = 0; i < _fftlen; i++) {
		_fftTwind[i] = 0.5 * (1 - cosf (2 * M_PI * i / _fftlen));
	}

	// Compute window autocorrelation and normalise it.
	// Ikaros: in _fftTdata, which holds the spectrum as (re, im) pairs.
	std::copy (_fftTwind.begin (), _fftTwind.end (), _fftTdata.begin ());
	forward (_fftTdata.data ());
	h = _fftlen / 2;
	for (i = 0; i < h; i++) {
		x                    = _fftTdata[2 * i];
		y                    = _fftTdata[2 * i + 1];
		_fftTdata[2 * i]     = x * x + y * y;
		_fftTdata[2 * i + 1] = 0;
	}
	_fftTdata[2 * h]     = 0;
	_fftTdata[2 * h + 1] = 0;
	inverse (_fftTdata.data ());
	std::copy (_fftTdata.begin (), _fftTdata.begin () + _fftlen, _fftWcorr.begin ());
	t = _fftWcorr[0];
	for (i = 0; i < _fftlen; i++) {
		_fftWcorr[i] /= t;
	}

	// Initialise all counters and other state.
	_notebits = 0;
	_lastnote = -1;
	_count    = 0;
	_cycle    = _frsize;
	_error    = 0.0f;
	_ratio    = 1.0f;
	_xfade    = false;
	_ipindex  = 0;
	_frindex  = 0;
	_frcount  = 0;
	_rindex1  = _ipsize / 2;
	_rindex2  = 0;

	for (int i = 0; i < 12; ++i) {
		_notescale[i] = i;
	}
}

Retuner::~Retuner (void)
{
	// Ikaros: the vectors, the FFT and the upsampler free themselves.
}

// Ikaros: FFTW's r2c transform. JUCE leaves (re, im) pairs for bins 0 to
// _fftlen / 2 in the same array.
void
Retuner::forward (float* data)
{
	_fft->performRealOnlyForwardTransform (data, true);
}

// Ikaros: FFTW's c2r transform. It reads bins 0 to _fftlen / 2. JUCE divides
// by the length on the way back and FFTW doesn't; the code below expects
// FFTW's scale (it adds a fixed 0.1 to the energy in findcycle()).
void
Retuner::inverse (float* data)
{
	_fft->performRealOnlyInverseTransform (data);
	for (int i = 0; i < _fftlen; i++)
		data[i] *= (float)_fftlen;
}

int
Retuner::process (int nfram, float const* inp, float* out)
{
	int   i, ii, k, fi;
	float ph, dp, r1, r2, dr, u1, u2, v;

	// Pitch shifting is done by resampling the input at the
	// required ratio, and eventually jumping forward or back
	// by one or more pitch period(s). Processing is done in
	// fragments of '_frsize' frames, and the decision to jump
	// forward or back is taken at the start of each fragment.
	// If a jump happens we crossfade over one fragment size.
	// Every 4 fragments a new pitch estimate is made. Since
	// _fftsize = 16 * _frsize, the estimation window moves
	// by 1/4 of the FFT length.

	fi = _frindex; // Write index in current fragment.
	r1 = _rindex1; // Read index for current input frame.
	r2 = _rindex2; // Second read index while crossfading.

	// No assumptions are made about fragments being aligned
	// with process() calls, so we may be in the middle of
	// a fragment here.

	while (nfram) {
		// Don't go past the end of the current fragment.
		k = _frsize - fi;
		if (nfram < k)
			k = nfram;
		nfram -= k;

		// At 44.1 and 48 kHz upsample by 2.
		if (_upsamp) {
			// Ikaros: juce::dsp::Oversampling instead of zita-resampler.
			// It returns a view of its own buffer, 2 * k samples long.
			const float* ch[] = { inp };
			auto         up   = _upsampler.processSamplesUp (juce::dsp::AudioBlock<const float> (ch, 1, (size_t)k));
			memcpy (_ipbuff.data () + _ipindex, up.getChannelPointer (0), 2 * k * sizeof (float));
			_ipindex += 2 * k;
		}
		// At higher sample rates apply lowpass filter.
		else {
			// Not implemented yet, just copy.
			memcpy (_ipbuff.data () + _ipindex, inp, k * sizeof (float));
			_ipindex += k;
		}

		// Extra samples for interpolation.
		_ipbuff[_ipsize + 0] = _ipbuff[0];
		_ipbuff[_ipsize + 1] = _ipbuff[1];
		_ipbuff[_ipsize + 2] = _ipbuff[2];
		inp += k;
		if (_ipindex == _ipsize)
			_ipindex = 0;

		// Process available samples.
		dr = _ratio;
		if (_upsamp)
			dr *= 2;
		if (_xfade) {
			// Interpolate and crossfade.
			while (k--) {
				i  = (int)r1;
				ii = i + _lastreadahead;
				if (ii >= _ipsize)
					ii -= _ipsize;
				u1 = cubic (_ipbuff.data () + ii, r1 - i);

				i  = (int)r2;
				ii = i + _readahead;
				if (ii >= _ipsize)
					ii -= _ipsize;
				u2 = cubic (_ipbuff.data () + ii, r2 - i);

				v      = _xffunc[fi++];
				*out++ = (1 - v) * u1 + v * u2;
				r1 += dr;
				if (r1 >= _ipsize)
					r1 -= _ipsize;
				r2 += dr;
				if (r2 >= _ipsize)
					r2 -= _ipsize;
			}
		} else if (0 == _readahead && 0 == _lastreadahead) {
			// Interpolation only.
			fi += k;
			while (k--) {
				i      = (int)r1;
				*out++ = cubic (_ipbuff.data () + i, r1 - i);
				r1 += dr;
				if (r1 >= _ipsize)
					r1 -= _ipsize;
			}
		} else if (_readahead != _lastreadahead) {
			// crossfade from/to fastmode
			while (k--) {
				i  = (int)r1;
				ii = i + _lastreadahead;
				if (ii >= _ipsize)
					ii -= _ipsize;
				u1 = cubic (_ipbuff.data () + ii, r1 - i);

				i  = (int)r1;
				ii = i + _readahead;
				if (ii >= _ipsize)
					ii -= _ipsize;
				u2 = cubic (_ipbuff.data () + ii, r1 - i);

				v      = _xffunc[fi++];
				*out++ = (1 - v) * u1 + v * u2;
				r1 += dr;
				if (r1 >= _ipsize)
					r1 -= _ipsize;
			}
		} else {
			// Interpolation only (fastmode).
			fi += k;
			while (k--) {
				i  = (int)r1;
				ii = i + _readahead;
				if (ii >= _ipsize)
					ii -= _ipsize;

				*out++ = cubic (_ipbuff.data () + ii, r1 - i);
				r1 += dr;
				if (r1 >= _ipsize)
					r1 -= _ipsize;
			}
		}

		// If at end of fragment check for jump.
		if (fi == _frsize) {
			fi = 0;

			// Estimate the pitch every 4th fragment.
			if (++_frcount == 4) {
				_frcount    = 0;
				float cycle = findcycle ();
				if (cycle > 0) {
					// If the pitch estimate succeeds, find the
					// nearest note and required resampling ratio.
					_cycle = cycle;
					_voiced = true;
					_count = 0;
					finderror ();
				} else if (++_count > 5) {
					// If the pitch estimate fails, the current
					// ratio is kept for 5 fragments. After that
					// the signal is considered unvoiced and the
					// pitch error is reset.
					_count = 5;
					_cycle = _frsize;
					_voiced = false;
					_error = 0;
				} else if (_count == 2) {
					// Bias is removed after two unvoiced fragments.
					_lastnote = -1;
				}

				_ratio = powf (2.0f, _corroffs / 12.0f - _error * _corrgain);
			}

			// If the previous fragment was crossfading,
			// the end of the new fragment that was faded
			// in becomes the current read position.
			if (_xfade)
				r1 = r2;

			// A jump must correspond to an integer number
			// of pitch periods, and to avoid reading outside
			// the circular input buffer limits it must be at
			// least one fragment size.
			dr = _cycle * (int)(ceilf (_frsize / _cycle));
			dp = dr / _frsize;
			ph = r1 - _ipindex;
			if (ph < 0)
				ph += _ipsize;
			if (_upsamp) {
				ph /= 2;
				dr *= 2;
			}
			ph = ph / _frsize + 2 * _ratio - 10;
			if (!_voiced) {
				// If signal is unvoiced, reset playhead position and crossfade
				// to avoid jitter and get a consistent output latency
				_xfade = true;
				r2 = _ipindex - _ipsize / 2;
			} else if (ph > 0.5f) {
				// Jump back by 'dr' frames and crossfade.
				_xfade = true;
				r2     = r1 - dr;
			} else if (ph + dp < 0.5f) {
				// Jump forward by 'dr' frames and crossfade.
				_xfade = true;
				r2     = r1 + dr;
			} else
				_xfade = false;

			if (r2 < 0)
				r2 += _ipsize;

			// Fast mode allows outputting the signal before the pitch has been computed,
			// resulting in a lower latency at the cost of a small delay between the correction
			// and the pitch analysis. This is useful in live situation.
			_lastreadahead = _readahead;

			if (_fastmode)
				_readahead = _ipsize * 7 / 16;
			else
				_readahead = 0;
		}
	}

	// Save local state.
	_frindex = fi;
	_rindex1 = r1;
	_rindex2 = r2;

	return 0;
}

float
Retuner::findcycle (void)
{
	int   d, h, i, j, k;
	float f, m, t, x, y, z;
	float cycle = 0;
#ifdef MOD
	float rms = 0;
#endif

	d = _upsamp ? 2 : 1;
	h = _fftlen / 2;
	j = _ipindex;
	k = _ipsize - 1;
	for (i = 0; i < _fftlen; i++) {
		int jk       = j & k;
		_fftTdata[i] = _fftTwind[i] * _ipbuff[jk];
#ifdef MOD
		rms += _ipbuff[jk] * _ipbuff[jk];
#endif
		j += d;
	}
#ifdef MOD                                  // ignore noise-floor
	if (rms < _fftlen * 0.000031623f) { // signal < -45dBFS/RMS
		return 0;
	}
#endif
	// Ikaros: in place in _fftTdata, as (re, im) pairs.
	forward (_fftTdata.data ());
	f = _fsamp / (_fftlen * 3e3f);
	for (i = 0; i < h; i++) {
		x                    = _fftTdata[2 * i];
		y                    = _fftTdata[2 * i + 1];
		m                    = i * f;
		_fftTdata[2 * i]     = (x * x + y * y) / (1 + m * m);
		_fftTdata[2 * i + 1] = 0;
	}
	_fftTdata[2 * h]     = 0;
	_fftTdata[2 * h + 1] = 0;
	inverse (_fftTdata.data ());
	t = _fftTdata[0] + 0.1f;
	for (i = 0; i < h; i++)
		_fftTdata[i] /= (t * _fftWcorr[i]);
	x = _fftTdata[0];
	for (i = 4; i < _ifmax; i += 4) {
		y = _fftTdata[i];
		if (y > x)
			break;
		x = y;
	}
	i -= 4;
	if (i >= _ifmax) {
		return 0;
	}

	if (i < _ifmin) {
		i = _ifmin;
	}
	x = _fftTdata[--i];
	y = _fftTdata[++i];
	m = 0;
	j = 0;
	while (i <= _ifmax) {
		t = y * _fftWcorr[i];
		z = _fftTdata[++i];
		if ((t > m) && (y >= x) && (y >= z) && (y > 0.8f)) {
			j = i - 1;
			m = t;
		}
		x = y;
		y = z;
	}
	if (j) {
		x = _fftTdata[j - 1];
		y = _fftTdata[j];
		z = _fftTdata[j + 1];
		if (fabsf (z - 2 * y + x) > 2e-9f) {
			cycle = j + 0.5f * (x - z) / (z - 2 * y + x - 1e-9f);
		}
	}
	return cycle;
}

void
Retuner::finderror (void)
{
	int   i, m, im;
	float a, am, d, dm, f;

	if (!_notemask) {
		_error    = 0;
		_lastnote = -1;
		return;
	}

	f  = log2f (_fsamp / (_cycle * _refpitch));
	dm = 0;
	am = 1;
	im = -1;
	for (i = 0, m = 1; i < 12; i++, m <<= 1) {
		if (_notemask & m) {
			d = f - (_notescale[i] - 9) / 12.0f;
			d -= floorf (d + 0.5f);
			a = fabsf (d);
			if (i == _lastnote)
				a -= _notebias;
			if (a < am) {
				am = a;
				dm = d;
				im = i;
			}
		}
	}

	if (_lastnote == im) {
		_error += _corrfilt * (dm - _error);
	} else {
		_error    = dm;
		_lastnote = im;
	}

	// For display only.
	_notebits |= 1 << im;
}

float
Retuner::cubic (float* v, float a)
{
	float b, c;

	b = 1 - a;
	c = a * b;
	return (1.0f + 1.5f * c) * (v[1] * b + v[2] * a) - 0.5f * c * (v[0] * b + v[1] + v[2] + v[3] * a);
}
