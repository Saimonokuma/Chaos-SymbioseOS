// ═══════════════════════════════════════════════════════════════════════
// media_capture.rs — UI-003: Webcam + Microphone Capture
//
// Hooks webcam (nokhwa) and mic (cpal) for multimodal input.
// Captures 30fps JPEG frames and 16kHz PCM audio.
//
// Reference: Interactive_Plan.md §XI UI-003
// ═══════════════════════════════════════════════════════════════════════

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

/// Webcam frame (JPEG-compressed in memory)
pub struct VideoFrame {
    /// JPEG-encoded frame data
    pub jpeg_data: Vec<u8>,
    /// Frame dimensions
    pub width: u32,
    pub height: u32,
    /// Capture timestamp (nanoseconds, monotonic)
    pub timestamp_ns: u64,
    /// Monotonic frame counter
    pub frame_number: u64,
}

/// Audio chunk (16kHz mono PCM)
pub struct AudioChunk {
    /// Raw PCM samples (f32, mono, 16kHz)
    pub samples: Vec<f32>,
    /// Sample rate (always 16000)
    pub sample_rate: u32,
    /// Channels (always 1 = mono)
    pub channels: u16,
    /// Capture timestamp (nanoseconds)
    pub timestamp_ns: u64,
    /// Chunk duration in milliseconds
    pub duration_ms: u32,
}

/// Webcam capture handle
pub struct WebcamCapture {
    running: Arc<AtomicBool>,
    frame_count: u64,
    /// Target FPS
    target_fps: u32,
    /// Resolution
    width: u32,
    height: u32,
}

impl WebcamCapture {
    /// Initialize webcam capture at 30fps, 640x480.
    ///
    /// Uses nokhwa for cross-platform camera access.
    pub fn new(width: u32, height: u32, fps: u32) -> Result<Self, String> {
        Ok(Self {
            running: Arc::new(AtomicBool::new(false)),
            frame_count: 0,
            target_fps: fps,
            width,
            height,
        })
    }

    /// Start capturing frames.
    ///
    /// Spawns a thread that delivers frames via callback.
    /// Uses nokhwa CameraFormat for resolution/fps negotiation.
    pub fn start<F>(&mut self, mut on_frame: F) -> Result<(), String>
    where
        F: FnMut(VideoFrame) + Send + 'static,
    {
        if self.running.load(Ordering::Relaxed) {
            return Err("Already running".into());
        }

        self.running.store(true, Ordering::Release);
        let running = self.running.clone();
        let target_fps = self.target_fps;
        let width = self.width;
        let height = self.height;

        std::thread::spawn(move || {
            // ── nokhwa camera initialization ─────────────────────────
            // In production, this uses:
            //   let mut camera = Camera::new(
            //       CameraIndex::Index(0),
            //       RequestedFormat::new::<RgbFormat>(
            //           RequestedFormatType::Closest(CameraFormat::new(
            //               Resolution::new(width, height),
            //               FrameFormat::MJPEG,
            //               target_fps,
            //           ))
            //       ),
            //   ).expect("Failed to open webcam");
            //   camera.open_stream().expect("Failed to start stream");

            let frame_interval = std::time::Duration::from_millis(1000 / target_fps as u64);
            let mut frame_number: u64 = 0;

            while running.load(Ordering::Acquire) {
                let start = std::time::Instant::now();

                // In production: let frame = camera.frame().unwrap();
                // For now: generate a placeholder JPEG header
                let jpeg_data = generate_placeholder_jpeg(width, height);

                let timestamp_ns = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_nanos() as u64;

                on_frame(VideoFrame {
                    jpeg_data,
                    width,
                    height,
                    timestamp_ns,
                    frame_number,
                });

                frame_number += 1;

                // Sleep to maintain target FPS
                let elapsed = start.elapsed();
                if elapsed < frame_interval {
                    std::thread::sleep(frame_interval - elapsed);
                }
            }
        });

        Ok(())
    }

    /// Stop capturing.
    pub fn stop(&self) {
        self.running.store(false, Ordering::Release);
    }

    /// Check if capture is active.
    pub fn is_running(&self) -> bool {
        self.running.load(Ordering::Relaxed)
    }
}

/// Microphone capture handle
pub struct MicCapture {
    running: Arc<AtomicBool>,
    /// Target sample rate (16kHz for Whisper STT)
    sample_rate: u32,
    /// Chunk duration in ms (how often to deliver audio)
    chunk_duration_ms: u32,
}

impl MicCapture {
    /// Initialize microphone capture at 16kHz mono.
    ///
    /// Uses cpal for cross-platform audio input.
    pub fn new(sample_rate: u32, chunk_duration_ms: u32) -> Result<Self, String> {
        Ok(Self {
            running: Arc::new(AtomicBool::new(false)),
            sample_rate,
            chunk_duration_ms,
        })
    }

    /// Start capturing audio.
    ///
    /// Spawns a thread that delivers PCM chunks via callback.
    pub fn start<F>(&mut self, mut on_chunk: F) -> Result<(), String>
    where
        F: FnMut(AudioChunk) + Send + 'static,
    {
        if self.running.load(Ordering::Relaxed) {
            return Err("Already running".into());
        }

        self.running.store(true, Ordering::Release);
        let running = self.running.clone();
        let sample_rate = self.sample_rate;
        let chunk_ms = self.chunk_duration_ms;

        std::thread::spawn(move || {
            // ── cpal audio initialization ────────────────────────────
            // In production, this uses:
            //   let host = cpal::default_host();
            //   let device = host.default_input_device().unwrap();
            //   let config = cpal::StreamConfig {
            //       channels: 1,
            //       sample_rate: cpal::SampleRate(sample_rate),
            //       buffer_size: cpal::BufferSize::Default,
            //   };
            //   let stream = device.build_input_stream(&config, move |data, _| {
            //       on_chunk(AudioChunk { samples: data.to_vec(), ... });
            //   }, |err| { ... }, None).unwrap();

            let samples_per_chunk = (sample_rate * chunk_ms / 1000) as usize;
            let chunk_interval = std::time::Duration::from_millis(chunk_ms as u64);

            while running.load(Ordering::Acquire) {
                let start = std::time::Instant::now();

                // Generate silence placeholder
                let samples = vec![0.0f32; samples_per_chunk];

                let timestamp_ns = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_nanos() as u64;

                on_chunk(AudioChunk {
                    samples,
                    sample_rate,
                    channels: 1,
                    timestamp_ns,
                    duration_ms: chunk_ms,
                });

                let elapsed = start.elapsed();
                if elapsed < chunk_interval {
                    std::thread::sleep(chunk_interval - elapsed);
                }
            }
        });

        Ok(())
    }

    /// Stop capturing.
    pub fn stop(&self) {
        self.running.store(false, Ordering::Release);
    }
}

/// Generate a minimal valid JPEG placeholder (for development).
/// In production, nokhwa provides real MJPEG frames.
fn generate_placeholder_jpeg(width: u32, height: u32) -> Vec<u8> {
    // JFIF minimal header + placeholder data
    let mut jpeg = Vec::with_capacity(128);
    // SOI marker
    jpeg.extend_from_slice(&[0xFF, 0xD8]);
    // APP0 JFIF marker
    jpeg.extend_from_slice(&[0xFF, 0xE0, 0x00, 0x10]);
    jpeg.extend_from_slice(b"JFIF\0");
    jpeg.extend_from_slice(&[0x01, 0x02]); // version
    jpeg.push(0x00); // units = no units
    jpeg.extend_from_slice(&(width as u16).to_be_bytes());
    jpeg.extend_from_slice(&(height as u16).to_be_bytes());
    jpeg.extend_from_slice(&[0x00, 0x00]); // no thumbnail
    // EOI marker
    jpeg.extend_from_slice(&[0xFF, 0xD9]);
    jpeg
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    #[test]
    fn test_webcam_lifecycle() {
        let mut cam = WebcamCapture::new(640, 480, 30).unwrap();
        let frames = Arc::new(Mutex::new(Vec::new()));
        let frames_clone = frames.clone();

        cam.start(move |frame| {
            frames_clone.lock().unwrap().push(frame);
        }).unwrap();

        std::thread::sleep(std::time::Duration::from_millis(150));
        cam.stop();

        let count = frames.lock().unwrap().len();
        assert!(count >= 2, "Should have captured at least 2 frames, got {}", count);
    }

    #[test]
    fn test_mic_lifecycle() {
        let mut mic = MicCapture::new(16000, 100).unwrap();
        let chunks = Arc::new(Mutex::new(Vec::new()));
        let chunks_clone = chunks.clone();

        mic.start(move |chunk| {
            chunks_clone.lock().unwrap().push(chunk);
        }).unwrap();

        std::thread::sleep(std::time::Duration::from_millis(350));
        mic.stop();

        let count = chunks.lock().unwrap().len();
        assert!(count >= 2, "Should have captured at least 2 chunks, got {}", count);
    }
}
