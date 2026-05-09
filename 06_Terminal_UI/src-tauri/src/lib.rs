// ═══════════════════════════════════════════════════════════════════════
// lib.rs — SymbioseTerminal Tauri Backend Module Root
//
// Exposes all backend modules to the Tauri application.
// ═══════════════════════════════════════════════════════════════════════

pub mod shm_ring_writer;
pub mod irc_client;
pub mod screen_capture;

#[cfg(feature = "camera")]
pub mod media_capture;

#[cfg(feature = "audio")]
pub mod tts_playback;

pub mod moviola_capture;
