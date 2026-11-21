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
// 2026-11-21 adding templates for 1, 8, 16 32 bit and range output
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
	
	template <typename T> T collect_bits(int count) {
		T result = 0;
		for (int i = 0; i < count; ++i) {
			result = static_cast<T>((result << 1) | step());
		}
		return result;
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
	 * Generates a random bit by stepping Rule 30 and
	 * collecting the center column bit.
	 */
	uint8_t next_bit() {
		return step();
	}

	/*
	 * Generates a random byte by stepping Rule 30 eight times and
	 * collecting the center column bit on each generation.
	 */
	uint8_t next_byte() {
		return collect_bits<uint8_t>(8);
	}

	/*
	 * Generates a random 16-bit unsigned integer by collecting 16 bits.
	 */
	uint16_t next_u16() {
		return collect_bits<uint16_t>(16);
	}

	/*
	 * Generates a random 32-bit unsigned integer by collecting 32 bits.
	 */
	uint32_t next_u32() {
		return collect_bits<uint32_t>(32);
	}

	/*
	 * Generates a uniform random int32_t in range [min, up).
	 * If min >= up, returns min.
	 */
	int32_t next_range(int32_t min, int32_t up) {
		if (min >= up) {
			return min;
		}

		// Compute span in unsigned 64-bit so even [INT32_MIN, INT32_MAX] cannot overflow
		const uint64_t span = static_cast<uint64_t>(up) - static_cast<uint64_t>(min);

		// Rejection sampling over 32-bit draws to eliminate modulo bias
		const uint64_t limit = (1ULL << 32) - ((1ULL << 32) % span);
		uint32_t r = 0;
		do {
			r = next_u32();
		} while (static_cast<uint64_t>(r) >= limit);

		const uint32_t offset = static_cast<uint32_t>(static_cast<uint64_t>(r) % span);
		return static_cast<int32_t>(static_cast<uint64_t>(min) + offset);
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
