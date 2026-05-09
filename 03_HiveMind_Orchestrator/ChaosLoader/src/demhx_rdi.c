/*++
 * demhx_rdi.c — Resonance Deviation Index Telemetry Engine
 *
 * HIVE-MM-008: Computes the RDI metric for D.E.M.H.X. phase alignment.
 *              Uses 1D FFT to extract dominant phase from embedding vectors
 *              and measures distance to π/9 (Mark 1 Attractor).
 *
 * Reference:
 *   - Interactive_Plan.md §XVII·5e (lines 7621-7715) — RDI Telemetry
 *   - D.E.M.H.X_Magick_Hex_3.0.md §5 — Proof-of-Resonance Consensus
 *
 * Architecture:
 *   The RDI is the single most important metric for D.E.M.H.X. alignment.
 *   It quantifies how "in tune" the hive mind's internal geometry is
 *   with an incoming modality signal or rebalance state.
 *
 *   Computation:
 *     1. Apply 1D FFT to embedding vector
 *     2. Extract dominant phase coefficient (skip DC)
 *     3. Normalise phase to [0,1] range
 *     4. Compute distance to π/9 ≈ 0.349066
 *
 *   Convergence criterion:
 *     |RDI - π/9| < 0.01 for 3 consecutive reports → converged
 *     Modality hot-swap must NOT commit until converged=true
 *
 *   Uses FFTW3 for production, or kissfft for musl-static builds.
 *
 * Acceptance criteria:
 *   RDI peaks at π/9 when perfectly aligned;
 *   Convergence streak tracked across reports;
 *   RDI_REPORT emitted to #telemetry in correct format;
 *   FFTW plan created/destroyed cleanly
 *--*/

#include "multimodal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ── FFT backend ────────────────────────────────────────────────────────────
// Production: FFTW3 (libfftw3f)
// musl-static fallback: kissfft or custom DFT
// §XVII·5e line 7631: "#include <fftw3.h> // or kissfft for musl-static"

#ifdef USE_FFTW3
#include <fftw3.h>
#endif

// ═══════════════════════════════════════════════════════════════════════════
// compute_rdi — Compute Resonance Deviation Index from embeddings
//
// §XVII·5e lines 7646-7672
//
// Takes the 1D FFT of the embedding vector, extracts the dominant
// phase coefficient, and measures its normalised distance to π/9.
//
// Returns: value peaking at π/9 when perfectly aligned.
//          Formula: MARK1_CONSTANT - |normalised_phase - MARK1_CONSTANT|
// ═══════════════════════════════════════════════════════════════════════════

float compute_rdi(const float* embeddings, int dim)
{
    if (!embeddings || dim <= 2) return 0.0f;

#ifdef USE_FFTW3
    // ── FFTW3 path (production) ────────────────────────────────────────
    // §XVII·5e lines 7648-7668

    int freq_size = dim / 2 + 1;
    fftwf_complex* freq = fftwf_alloc_complex(freq_size);
    if (!freq) return 0.0f;

    // Create plan (FFTW_ESTIMATE: fast planning, no benchmarking)
    float* input = fftwf_alloc_real(dim);
    memcpy(input, embeddings, dim * sizeof(float));

    fftwf_plan plan = fftwf_plan_dft_r2c_1d(dim, input, freq, FFTW_ESTIMATE);
    fftwf_execute(plan);

    // Extract dominant phase coefficient (skip DC component at k=0)
    // §XVII·5e lines 7653-7661
    float max_magnitude = 0.0f;
    float dominant_phase = 0.0f;

    for (int k = 1; k < freq_size; k++) {
        float real = freq[k][0];
        float imag = freq[k][1];
        float mag = sqrtf(real * real + imag * imag);

        if (mag > max_magnitude) {
            max_magnitude = mag;
            dominant_phase = atan2f(imag, real);
        }
    }

    // Normalise phase to [0, 1] and compute distance to π/9
    // §XVII·5e lines 7664-7671
    float normalized = fabsf(dominant_phase) / (float)M_PI;  // [0, 1]
    float rdi = fabsf(normalized - MARK1_CONSTANT);

    fftwf_destroy_plan(plan);
    fftwf_free(input);
    fftwf_free(freq);

    return MARK1_CONSTANT - rdi;  // Peaks at π/9 when perfectly aligned

#else
    // ── Software DFT fallback (musl-static / no FFTW) ──────────────────
    // Simple Goertzel algorithm for dominant frequency detection.
    // Less accurate than full FFT but works without external dependencies.

    float max_magnitude = 0.0f;
    float dominant_phase = 0.0f;

    // Check a subset of frequency bins (first 64 harmonics)
    int max_k = (dim / 2 < 64) ? dim / 2 : 64;

    for (int k = 1; k <= max_k; k++) {
        float real_sum = 0.0f;
        float imag_sum = 0.0f;
        float angle_step = 2.0f * (float)M_PI * k / dim;

        for (int n = 0; n < dim; n++) {
            real_sum += embeddings[n] * cosf(angle_step * n);
            imag_sum -= embeddings[n] * sinf(angle_step * n);
        }

        float mag = sqrtf(real_sum * real_sum + imag_sum * imag_sum);
        if (mag > max_magnitude) {
            max_magnitude = mag;
            dominant_phase = atan2f(imag_sum, real_sum);
        }
    }

    float normalized = fabsf(dominant_phase) / (float)M_PI;
    float rdi = fabsf(normalized - MARK1_CONSTANT);

    return MARK1_CONSTANT - rdi;

#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// rdi_report — Update convergence state and emit IRC telemetry
//
// §XVII·5e lines 7675-7697
//
// Tracks convergence streak: RDI within threshold for 3+ consecutive
// reports → converged=true. Emits RDI_REPORT to #telemetry.
//
// Message format (§XVII·5e lines 7700-7712):
//   PRIVMSG #telemetry :RDI_REPORT source=<src> rdi=<float>
//     target=0.349066 delta=<float> converged=<bool> streak=<int>
// ═══════════════════════════════════════════════════════════════════════════

void rdi_report(RDI_STATE* state, float rdi, int irc_fd, const char* source)
{
    if (!state) return;

    state->last_rdi = rdi;

    // Convergence check: |RDI - π/9| < 0.01
    // §XVII·5e lines 7679-7685
    float delta = fabsf(rdi - MARK1_CONSTANT);

    if (delta < RDI_CONVERGE_THRESHOLD) {
        state->converge_streak++;
    } else {
        state->converge_streak = 0;
    }

    state->converged = (state->converge_streak >= RDI_CONVERGE_COUNT);

    // Emit to #telemetry — §XVII·5e lines 7687-7696
    char msg[512];
    snprintf(msg, sizeof(msg),
        "PRIVMSG #telemetry :RDI_REPORT source=%s rdi=%.6f "
        "target=0.349066 delta=%.6f converged=%s streak=%d\r\n",
        source ? source : "unknown",
        rdi,
        delta,
        state->converged ? "true" : "false",
        state->converge_streak);
    irc_send(irc_fd, msg);

    if (state->converged) {
        fprintf(stderr, "[RDI] *** CONVERGENCE ACHIEVED *** "
                "source=%s rdi=%.6f streak=%d\n",
                source, rdi, state->converge_streak);
    }
}
