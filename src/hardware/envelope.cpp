/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2020-2020  The dosbox-staging team
 *  Copyright (c) 2019-2020  Kevin R Croft <krcroft@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

// #define DEBUG 1

#include "support.h"
#include "envelope.h"

void Envelope::Reactivate()
{
	edge = 0u;
	frames_done = 0u;
	process = &Envelope::Apply;
}

void Envelope::Update(const uint32_t frame_rate,
                      const uint32_t peak_magnitude,
                      const uint8_t expansion_phase_ms,
                      const uint8_t expire_after_seconds)
{
	if (!frame_rate || !peak_magnitude || !expansion_phase_ms)
		return;

	// How many frames should we inspect before expiring?
	expire_after_frames = expire_after_seconds * frame_rate;
	assert(expire_after_frames > 0);

	// The furtherest allowed edge is the peak sample magnitude.
	edge_limit = peak_magnitude;

	// Permit the envelop to achieve peak volume within the expansion_phase
	// (in ms) if the samples happen to constantly press on the edges.
	const uint32_t expansion_phase_frames = ceil_udivide(frame_rate * expansion_phase_ms,
	                                                     1000u);

	// Calculate how much the envelope's edge will grow after a frame
	// presses against it.
	assert(expansion_phase_frames);
	edge_increment = ceil_udivide(peak_magnitude, expansion_phase_frames);
#ifdef DEBUG
	LOG_MSG("ENVELOPE: Expires after %u frames. Edge increments by %u for "
	        "each of the %u expansion frames, up to peak of %u",
	        expire_after_frames, edge_increment, expansion_phase_frames,
	        edge_limit);
#endif
}

bool Envelope::ClampSample(intptr_t &sample, const intptr_t lip)
{
	if (abs(sample) > edge) {
		sample = clamp(sample, -lip, lip);
		return true;
	}
	return false;
}

void Envelope::Apply(const bool is_stereo,
                     const bool is_interpolated,
                     intptr_t prev[],
                     intptr_t next[])
{
	// Only start the envelope once our samples have actual values
	if (prev[0] == 0 && frames_done == 0u)
		return;

	// beyond the edge is the lip. Do any samples walk out onto the lip?
	const intptr_t lip = edge + edge_increment;
	const bool on_lip = (ClampSample(prev[0], lip)) || (is_stereo &&
	                     ClampSample(prev[1], lip)) || (is_interpolated &&
	                     (ClampSample(next[0], lip) || (is_stereo &&
	                      ClampSample(next[1], lip))));

	// If any of the samples are out on the lip, then march the edge forward
	if (on_lip)
		edge += edge_increment;

	// Should we deactivate the envelope?
	if (++frames_done > expire_after_frames || edge >= edge_limit)
		process = &Envelope::Skip;
}

void Envelope::Skip(const bool is_stereo,
                    const bool is_interpolated,
                    intptr_t prev[],
                    intptr_t next[])
{
	(void)is_stereo;       // unused
	(void)is_interpolated; // unused
	(void)prev;            // unused
	(void)next;            // unused
}
