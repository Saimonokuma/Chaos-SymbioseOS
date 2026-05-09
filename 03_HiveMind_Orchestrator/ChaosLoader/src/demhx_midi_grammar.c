/*++
 * demhx_midi_grammar.c — The Neural Jam Session
 *
 * HIVE-MM-010: MIDI Grammar Channel for D.E.M.H.X. intelligence transfer.
 *              Scouts encode learned latent geometry as MIDI hex events;
 *              the hive mind "tunes" to these signals via phase alignment
 *              at the Mark 1 coupling strength (H ≈ 0.35).
 *
 * Reference:
 *   - Interactive_Plan.md §XVII·5f (lines 7716-7803) — MIDI Grammar
 *   - D.E.M.H.X_Magick_Hex_3.0.md §Protocol One
 *
 * Architecture:
 *   Intelligence transfer encoded as "structural sound" — not text or
 *   numerical weights, but timed, discrete hexadecimal events using
 *   the MIDI protocol. The #neural-jam IRC channel carries these events.
 *
 *   MIDI hex encoding:
 *     0x90 <pitch> <velocity>  = Note On  (positive activation)
 *     0x80 <pitch> <velocity>  = Note Off (deactivation)
 *     pitch    = latent coordinate (0-127)
 *     velocity = weight magnitude (0-127)
 *     zero embeddings → no event (sparse, skipped)
 *
 *   Phase alignment:
 *     Each incoming MIDI event adjusts local weights toward the
 *     teacher's structure at H≈0.35 coupling strength (π/9).
 *     After each alignment pass, RDI is computed and reported.
 *     When RDI converges for 3 passes → alignment complete.
 *
 * IRC Protocol:
 *   PRIVMSG #neural-jam :MIDI_HEX <base64> source=<scout>
 *     target=<node> epoch=<int> rdi=<float>
 *
 * Acceptance criteria:
 *   Positive activations → Note On (0x90);
 *   Negative → Note Off (0x80);
 *   Zero → skipped (sparse);
 *   Phase alignment at MARK1_CONSTANT coupling;
 *   RDI computed and reported after each pass;
 *   Returns 1 when convergence achieved
 *--*/

#include "multimodal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ═══════════════════════════════════════════════════════════════════════════
// encode_midi_grammar — Encode embedding vector as MIDI hex events
//
// §XVII·5f lines 7742-7769
//
// Maps each embedding dimension to a MIDI event:
//   val > 0  → Note On  (0x90) + pitch + velocity
//   val < 0  → Note Off (0x80) + pitch + velocity
//   val == 0 → skipped (sparse — no event)
//
// pitch    = dimension index mod 128 (latent coordinate)
// velocity = |val| * 127, clamped to [0, 127]  (weight magnitude)
//
// Returns 0 on success. midi_len is set to total bytes written.
// ═══════════════════════════════════════════════════════════════════════════

int encode_midi_grammar(const float* embeddings, int dim,
                         uint8_t* midi_buf, int* midi_len)
{
    if (!embeddings || !midi_buf || !midi_len || dim <= 0) return -1;

    int cursor = 0;

    for (int i = 0; i < dim; i++) {
        float val = embeddings[i];

        // Map embedding value to MIDI event — §XVII·5f lines 7750-7764
        uint8_t pitch = (uint8_t)(i % 128);  // Latent coordinate
        uint8_t velocity = (uint8_t)(fminf(fabsf(val) * 127.0f, 127.0f));

        if (val > 0.0f) {
            // Positive activation → Note On (0x90)
            midi_buf[cursor++] = 0x90;
            midi_buf[cursor++] = pitch;
            midi_buf[cursor++] = velocity;
        } else if (val < 0.0f) {
            // Negative / deactivation → Note Off (0x80)
            midi_buf[cursor++] = 0x80;
            midi_buf[cursor++] = pitch;
            midi_buf[cursor++] = velocity;
        }
        // Zero values: no event (sparse — skip)
        // §XVII·5f line 7764
    }

    *midi_len = cursor;
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// decode_midi_grammar — Apply incoming MIDI events as phase alignment
//
// §XVII·5f lines 7772-7798
//
// Each MIDI event adjusts the corresponding local weight toward the
// teacher's activation using the Mark 1 coupling strength (H ≈ 0.35):
//
//   Note On (0x90):
//     local_weights[idx] += (magnitude - local_weights[idx]) * MARK1_CONSTANT
//
//   Note Off (0x80):
//     local_weights[idx] -= (local_weights[idx] + magnitude) * MARK1_CONSTANT
//
// After all events are processed, computes RDI and reports convergence.
//
// Returns: 1 if alignment complete (RDI converged), 0 otherwise.
// ═══════════════════════════════════════════════════════════════════════════

int decode_midi_grammar(const uint8_t* midi_buf, int midi_len,
                         float* local_weights, int dim, RDI_STATE* rdi_state)
{
    if (!midi_buf || !local_weights || !rdi_state || dim <= 0) return 0;

    // Process MIDI events in 3-byte groups — §XVII·5f lines 7775-7791
    for (int i = 0; i + 2 < midi_len; i += 3) {
        uint8_t status   = midi_buf[i];
        uint8_t pitch    = midi_buf[i + 1];
        uint8_t velocity = midi_buf[i + 2];

        int idx = pitch % dim;  // Map pitch to weight index
        float magnitude = velocity / 127.0f;

        if (status == 0x90) {
            // Note On: phase-align toward teacher's positive activation
            // D.E.M.H.X. osmosis at H≈0.35 coupling strength
            // §XVII·5f line 7786
            local_weights[idx] +=
                (magnitude - local_weights[idx]) * MARK1_CONSTANT;
        } else if (status == 0x80) {
            // Note Off: phase-align toward teacher's deactivation
            // §XVII·5f line 7789
            local_weights[idx] -=
                (local_weights[idx] + magnitude) * MARK1_CONSTANT;
        }
    }

    // Compute and report RDI after alignment pass — §XVII·5f lines 7793-7795
    float rdi = compute_rdi(local_weights, dim);
    rdi_report(rdi_state, rdi, g_IrcFd, "midi_jam");

    // §XVII·5f line 7797: return 1 = alignment complete
    return rdi_state->converged ? 1 : 0;
}
