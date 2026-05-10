/*++
 * node_score.c — Composite Node Scoring (HIVE-MOSIX-004)
 *
 * NOTE: The canonical implementations of node_score(), pick_best_node(),
 * and hive_mind_rebalance() live in migrate.c, which owns the global
 * node registry (g_NodeRegistry, g_NodeCount, g_IrcFd, g_ModelConfig).
 *
 * This file is intentionally empty to avoid multiple definition errors.
 * All scoring logic is integrated directly into migrate.c per the
 * original architecture (§VIII·2).
 *
 * If the scoring algorithm needs to be upgraded (e.g., from simple
 * VRAM/thermal weights to the full Mark 1 harmonic rebalancer from
 * §VIII·4a), modify the functions in migrate.c directly.
 *--*/

/* No code — all functions provided by migrate.c */
