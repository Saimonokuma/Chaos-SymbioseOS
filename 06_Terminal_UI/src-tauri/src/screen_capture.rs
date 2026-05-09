// ═══════════════════════════════════════════════════════════════════════
// screen_capture.rs — UI-005 + UI-008: DXGI Desktop Duplication
//
// UI-005: Windows DXGI Desktop Duplication API for screen capture.
//         Writes frames to SHM ring slot, announces SCREEN_CAP via IRC.
//         Target: ≥15fps with <50ms latency.
//
// UI-008: Adaptive Screen Capture Idle Mode (§XVIII·3d).
//         When Moviola reports >99.9% sparsity for 3+ consecutive frames,
//         reduce capture from 15fps → 1fps. Resume instantly on activity.
//         Emits SCREEN_IDLE / SCREEN_ACTIVE to #telemetry.
//
// Reference: Interactive_Plan.md §XI UI-005, UI-008
// ═══════════════════════════════════════════════════════════════════════

use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

/// Screen capture configuration
pub struct ScreenCaptureConfig {
    /// Active capture FPS (default: 15)
    pub active_fps: u32,
    /// Idle capture FPS (default: 1)
    pub idle_fps: u32,
    /// Sparsity threshold for idle transition (default: 0.999)
    pub idle_sparsity_threshold: f64,
    /// Consecutive frames above threshold before idle (default: 3)
    pub idle_frame_count: u32,
}

impl Default for ScreenCaptureConfig {
    fn default() -> Self {
        Self {
            active_fps: 15,
            idle_fps: 1,
            idle_sparsity_threshold: 0.999,
            idle_frame_count: 3,
        }
    }
}

/// Captured screen frame
pub struct ScreenFrame {
    /// Raw BGRA pixel data (from DXGI)
    pub bgra_data: Vec<u8>,
    /// Frame width
    pub width: u32,
    /// Frame height
    pub height: u32,
    /// Capture latency (time from AcquireNextFrame to data copy complete)
    pub capture_latency_us: u64,
    /// Frame timestamp (nanoseconds)
    pub timestamp_ns: u64,
    /// Whether this frame was captured in idle mode
    pub is_idle_frame: bool,
}

/// Idle state (shared with Moviola and IRC client)
#[derive(Debug)]
pub struct ScreenIdleState {
    /// Current capture mode
    pub idle: AtomicBool,
    /// Current effective FPS
    pub current_fps: AtomicU32,
    /// Latest sparsity value from Moviola
    pub sparsity: std::sync::Mutex<f64>,
    /// Consecutive high-sparsity frame count
    pub high_sparsity_streak: std::sync::Mutex<u32>,
}

impl ScreenIdleState {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            idle: AtomicBool::new(false),
            current_fps: AtomicU32::new(15),
            sparsity: std::sync::Mutex::new(0.0),
            high_sparsity_streak: std::sync::Mutex::new(0),
        })
    }
}

/// Screen capture engine
pub struct ScreenCapture {
    running: Arc<AtomicBool>,
    config: ScreenCaptureConfig,
    idle_state: Arc<ScreenIdleState>,
}

impl ScreenCapture {
    pub fn new(config: ScreenCaptureConfig) -> Self {
        Self {
            running: Arc::new(AtomicBool::new(false)),
            idle_state: ScreenIdleState::new(),
            config,
        }
    }

    /// Get shared idle state handle (for Moviola and telemetry)
    pub fn idle_state(&self) -> Arc<ScreenIdleState> {
        self.idle_state.clone()
    }

    /// Start screen capture loop.
    ///
    /// Uses Windows DXGI Desktop Duplication API.
    /// Delivers frames via callback, adapts FPS based on idle state.
    pub fn start<F>(&mut self, mut on_frame: F) -> Result<(), String>
    where
        F: FnMut(ScreenFrame) + Send + 'static,
    {
        if self.running.load(Ordering::Relaxed) {
            return Err("Already running".into());
        }

        self.running.store(true, Ordering::Release);
        let running = self.running.clone();
        let idle_state = self.idle_state.clone();
        let active_fps = self.config.active_fps;
        let idle_fps = self.config.idle_fps;

        std::thread::spawn(move || {
            // ── DXGI Desktop Duplication initialization ──────────────
            // In production, this uses:
            //   1. CreateDXGIFactory1() → IDXGIFactory1
            //   2. EnumAdapters1(0) → IDXGIAdapter1
            //   3. EnumOutputs(0) → IDXGIOutput → IDXGIOutput1
            //   4. DuplicateOutput() → IDXGIOutputDuplication
            //
            // For development, we simulate frame capture.

            while running.load(Ordering::Acquire) {
                let capture_start = Instant::now();

                // Determine current FPS from idle state
                let is_idle = idle_state.idle.load(Ordering::Relaxed);
                let fps = if is_idle { idle_fps } else { active_fps };
                idle_state.current_fps.store(fps, Ordering::Relaxed);
                let frame_interval = Duration::from_millis(1000 / fps as u64);

                // ── DXGI AcquireNextFrame ────────────────────────────
                // In production:
                //   let frame_result = dupl.AcquireNextFrame(
                //       frame_interval.as_millis() as u32,
                //       &mut frame_info,
                //       &mut desktop_resource,
                //   );
                //   let texture = desktop_resource.QueryInterface::<ID3D11Texture2D>();
                //   context.CopyResource(staging_texture, texture);
                //   let mapped = context.Map(staging_texture, 0, D3D11_MAP_READ);
                //   let bgra_data = slice::from_raw_parts(mapped.pData, ...);

                // Simulated frame data (1920×1080 BGRA)
                let width = 1920u32;
                let height = 1080u32;
                let bgra_data = vec![0u8; (width * height * 4) as usize];

                let capture_latency = capture_start.elapsed();
                let timestamp_ns = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_nanos() as u64;

                on_frame(ScreenFrame {
                    bgra_data,
                    width,
                    height,
                    capture_latency_us: capture_latency.as_micros() as u64,
                    timestamp_ns,
                    is_idle_frame: is_idle,
                });

                // Sleep to maintain target FPS
                let elapsed = capture_start.elapsed();
                if elapsed < frame_interval {
                    std::thread::sleep(frame_interval - elapsed);
                }
            }
        });

        Ok(())
    }

    /// Report Moviola sparsity — triggers idle mode transitions (UI-008).
    ///
    /// Called by moviola_capture.rs after computing delta sparsity.
    pub fn report_sparsity(&self, sparsity: f64) {
        *self.idle_state.sparsity.lock().unwrap() = sparsity;

        let config_threshold = 0.999; // screen_idle_sparsity
        let config_streak = 3; // consecutive frames

        let mut streak = self.idle_state.high_sparsity_streak.lock().unwrap();

        if sparsity > config_threshold {
            *streak += 1;

            // Transition to IDLE if streak threshold met
            if *streak >= config_streak && !self.idle_state.idle.load(Ordering::Relaxed) {
                self.idle_state.idle.store(true, Ordering::Release);
                self.idle_state.current_fps.store(
                    self.config.idle_fps, Ordering::Relaxed
                );
                // IRC SCREEN_IDLE emission handled by caller
            }
        } else {
            // Any activity → instant resume
            *streak = 0;
            if self.idle_state.idle.load(Ordering::Relaxed) {
                self.idle_state.idle.store(false, Ordering::Release);
                self.idle_state.current_fps.store(
                    self.config.active_fps, Ordering::Relaxed
                );
                // IRC SCREEN_ACTIVE emission handled by caller
            }
        }
    }

    /// Stop capture.
    pub fn stop(&self) {
        self.running.store(false, Ordering::Release);
    }

    /// Check if capturing.
    pub fn is_running(&self) -> bool {
        self.running.load(Ordering::Relaxed)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    #[test]
    fn test_idle_mode_transition() {
        let capture = ScreenCapture::new(ScreenCaptureConfig::default());
        let state = capture.idle_state();

        // Initially active
        assert!(!state.idle.load(Ordering::Relaxed));

        // Report high sparsity 3 times → should go idle
        capture.report_sparsity(0.9995);
        assert!(!state.idle.load(Ordering::Relaxed));
        capture.report_sparsity(0.9998);
        assert!(!state.idle.load(Ordering::Relaxed));
        capture.report_sparsity(0.9999);
        assert!(state.idle.load(Ordering::Relaxed)); // Now idle

        // Any activity → instant resume
        capture.report_sparsity(0.5);
        assert!(!state.idle.load(Ordering::Relaxed));
    }

    #[test]
    fn test_capture_delivers_frames() {
        let mut capture = ScreenCapture::new(ScreenCaptureConfig {
            active_fps: 30, // Higher FPS for faster test
            ..Default::default()
        });

        let frames = Arc::new(Mutex::new(Vec::new()));
        let frames_clone = frames.clone();

        capture.start(move |frame| {
            frames_clone.lock().unwrap().push(frame.width);
        }).unwrap();

        std::thread::sleep(Duration::from_millis(150));
        capture.stop();

        let count = frames.lock().unwrap().len();
        assert!(count >= 2, "Should have captured frames, got {}", count);
    }
}
