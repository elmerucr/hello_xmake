// ---------------------------------------------------------------------
// rca.hpp
// hydra
//
// Copyright © 2012-2026 elmerucr. All rights reserved.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// rca: random number generation with cellular automaton
//
// 2012-01-21 c version
// 2013-11-11 c++ version
// 2026-11-20 64-bit / 1-bit-per-step upgrade (google gemini AI agent)
// ---------------------------------------------------------------------

#ifndef RCA_HPP
#define RCA_HPP

#include <cstdint>

class rca_t {
private:
	/*
	 * 61-cell PRNG inside a 64-bit register.
	 * Uses bits 2..62 (61 cells, prime width avoids small harmonic sub-cycles).
	 * Bit 63 (left) and bits 0..1 (right) are used as periodic wrap-around buffers.
	 */
	uint64_t stat;

	static constexpr uint64_t ACTIVE_MASK = 0x7ffffffffffffffcULL;

	/*
	 * Advances the cellular automaton by one generation using Wolfram's Rule 30
	 * and returns the center column bit (bit 32).
	 */
	inline uint8_t step() {
		// Clear wrap-around guard bits
		stat &= ACTIVE_MASK;

		// Periodic boundary conditions for 61 cells:
		// Bit 1 gets a copy of bit 62; bit 63 gets a copy of bit 2.
		uint64_t wrap_right = stat >> 61;
		uint64_t wrap_left  = stat << 61;
		stat = stat | wrap_right | wrap_left;

		// Apply Wolfram's Rule 30: next_center = left ^ (center | right)
		uint64_t right = stat >> 1;
		uint64_t left  = stat << 1;
		stat = (stat | right) ^ left;

		// Extract center column (bit 32)
		return static_cast<uint8_t>((stat >> 32) & 1ULL);
	}

public:
	/*
	 * Default constructor: turns on 1 bit in the center column (bit 32).
	 */
	rca_t() {
		stat = (1ULL << 32);
	}

	/*
	 * Alternative constructor: uses a uint64_t seed.
	 * Falls back to default seed if masked state would be all zeros.
	 */
	rca_t(uint64_t number) {
		stat = number & ACTIVE_MASK;
		if (stat == 0) {
			stat = (1ULL << 32);
		}
	}

	/*
	 * Generates a random byte by stepping Rule 30 eight times and
	 * collecting the center column bit on each generation.
	 */
	uint8_t byte() {
		uint8_t result = 0;
		for (int i = 0; i < 8; ++i) {
			result = static_cast<uint8_t>((result << 1) | step());
		}
		return result;
	}

	/*
	 * Returns current masked status (e.g. to seed another generator)
	 * without advancing the automaton.
	 */
	uint64_t status() const {
		return stat & ACTIVE_MASK;
	}
};

#endif
