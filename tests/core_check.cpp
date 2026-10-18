// Sanity checks for the portable FreeSurround core + stream_chunker on a 64-bit build.
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstdlib>
#include "freesurround_decoder.h"
#include "stream_chunker.h"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

// ---- stream_chunker: every emitted chunk is chunk_len long and the data stream is preserved in order
static void test_chunker() {
	const unsigned chunk_len = 8192;
	std::vector<float> emitted;
	int chunks = 0;
	stream_chunker<float> rc([&](float *p) { ++chunks; emitted.insert(emitted.end(), p, p + chunk_len); }, chunk_len);

	srand(1234);
	std::vector<float> all;
	for (int i = 0; i < 400; i++) {
		unsigned n = 1 + rand() % 20000;                 // arbitrary block sizes, incl. > chunk_len and tiny
		std::vector<float> blk(n);
		for (unsigned k = 0; k < n; k++) blk[k] = (float)all.size() + k;
		all.insert(all.end(), blk.begin(), blk.end());
		rc.append(&blk[0], n);
	}
	CHECK(emitted.size() == (size_t)chunks * chunk_len, "chunk sizes");
	bool same = emitted.size() <= all.size();
	for (size_t i = 0; same && i < emitted.size(); i++) same = (emitted[i] == all[i]);
	CHECK(same, "chunker reordered or lost samples");
	CHECK(emitted.size() + rc.buffered() <= all.size() && all.size() - emitted.size() - rc.buffered() <= chunk_len,
		"chunker accounting: emitted=%zu buffered=%u total=%zu", emitted.size(), rc.buffered(), all.size());
	printf("chunker: %d chunks of %u, %zu samples emitted, %u buffered of %zu fed\n", chunks, chunk_len, emitted.size(), rc.buffered(), all.size());
}

// ---- decoder: finite output for every setup + sane spatial behaviour
static void test_decoder() {
	const unsigned N = 4096;
	channel_setup setups[] = { cs_stereo, cs_3stereo, cs_4point1, cs_5point1, cs_5stereo, cs_legacy, cs_6point1, cs_7point1,
		cs_7point1_panorama, cs_7point1_tricenter, cs_8point1, cs_9point1_wrap, cs_9point1_densepanorama,
		cs_11point1_densewrap, cs_13point1_totalwrap, cs_16point1 };
	const char *names[] = { "stereo","3stereo","4.1","5.1","5stereo","legacy","6.1","7.1","7.1pano","7.1tri","8.1","9.1wrap","9.1dense","11.1","13.1","16.1" };

	for (unsigned si = 0; si < sizeof(setups) / sizeof(setups[0]); si++) {
		freesurround_decoder dec(setups[si], N);
		unsigned C = freesurround_decoder::num_channels(setups[si]);
		CHECK(C >= 2, "%s: channel count %u", names[si], C);
		std::vector<float> in(2 * N);
		std::vector<double> energy(C, 0.0);
		bool finite = true;
		for (int blk = 0; blk < 12; blk++) {
			for (unsigned k = 0; k < N; k++) {                      // identical L/R sine => dead-centre source
				float s = 0.5f * (float)sin(2 * 3.14159265358979 * 1000.0 * (blk * N + k) / 44100.0);
				in[2 * k] = s; in[2 * k + 1] = s;
			}
			float *out = dec.decode(&in[0]);
			if (blk < 2) continue;                                  // skip start-up transient
			for (unsigned k = 0; k < N; k++)
				for (unsigned c = 0; c < C; c++) {
					float v = out[k * C + c];
					if (!std::isfinite(v)) finite = false;
					energy[c] += (double)v * v;
				}
		}
		CHECK(finite, "%s: non-finite output", names[si]);
		double tot = 0; for (double e : energy) tot += e;
		CHECK(tot > 1.0, "%s: silent output (energy %g)", names[si], tot);
		// for centre-panned content the L and R fronts (channel 0 and last non-LFE front) must carry equal energy
		if (setups[si] == cs_5point1 || setups[si] == cs_3stereo) {
			unsigned cidx = 1, ridx = 2;                            // FL, FC, FR order
			CHECK(energy[cidx] > energy[0] && energy[cidx] > energy[ridx], "%s: centre not dominant for centre source", names[si]);
			double rel = fabs(energy[0] - energy[ridx]) / (energy[0] + energy[ridx] + 1e-12);
			CHECK(rel < 0.01, "%s: L/R asymmetry %.4f for mono source", names[si], rel);
		}
		printf("decoder %-9s C=%2u finite=%d  energy(first 3 ch)=%.3g %.3g %.3g\n", names[si], C, (int)finite, energy[0], energy[1 % C], energy[2 % C]);
	}

	// hard-left source in 5.1 must land in front-left
	freesurround_decoder dec(cs_5point1, N);
	std::vector<float> in(2 * N);
	double e[6] = { 0 };
	for (int blk = 0; blk < 12; blk++) {
		for (unsigned k = 0; k < N; k++) {
			in[2 * k] = 0.5f * (float)sin(2 * 3.14159265358979 * 800.0 * (blk * N + k) / 44100.0);
			in[2 * k + 1] = 0;
		}
		float *out = dec.decode(&in[0]);
		if (blk < 2) continue;
		for (unsigned k = 0; k < N; k++) for (unsigned c = 0; c < 6; c++) e[c] += (double)out[k * 6 + c] * out[k * 6 + c];
	}
	CHECK(e[0] > 4 * e[1] && e[0] > 4 * e[2] && e[0] > 4 * e[3] && e[0] > 4 * e[4], "5.1 hard-left not in front-left: %g %g %g %g %g", e[0], e[1], e[2], e[3], e[4]);
	printf("5.1 hard-left energy FL,FC,FR,BL,BR = %.3g %.3g %.3g %.3g %.3g\n", e[0], e[1], e[2], e[3], e[4]);
	printf("sizeof(void*)=%zu sizeof(size_t)=%zu\n", sizeof(void *), sizeof(size_t));
}

int main() {
	test_chunker();
	test_decoder();
	printf(failures ? "\n%d FAILURES\n" : "\nall checks passed\n", failures);
	return failures != 0;
}
