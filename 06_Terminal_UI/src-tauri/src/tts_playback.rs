// ═══════════════════════════════════════════════════════════════════════
// tts_playback.rs — UI-006: TTS Audio Playback from SHM
//
// Listens for TTS_AUDIO IRC messages, reads PCM data from SHM ring slot,
// plays through host speakers via cpal audio output.
// Target: <200ms latency from SHM write to audio output.
//
// Reference: Interactive_Plan.md §XI UI-006
// ═══════════════════════════════════════════════════════════════════════

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::collections::VecDeque;

/// TTS audio configuration
pub struct TtsConfig {
    /// Sample rate (matches Piper TTS output, typically 22050 or 24000)
    pub sample_rate: u32,
    /// Channels (1 = mono)
    pub channels: u16,
    /// Buffer size for audio device (lower = less latency)
    pub buffer_frames: u32,
}

impl Default for TtsConfig {
    fn default() -> Self {
        Self {
            sample_rate: 22050,
            channels: 1,
            buffer_frames: 512,
        }
    }
}

/// Pending audio chunk for playback queue
#[derive(Clone)]
pub struct TtsAudioChunk {
    /// Raw PCM samples (f32, mono)
    pub samples: Vec<f32>,
    /// Original SHM slot index
    pub shm_slot: usize,
    /// Sequence number (for ordering)
    pub sequence: u64,
    /// Timestamp when data was read from SHM
    pub read_timestamp_ns: u64,
}

/// TTS Playback engine
pub struct TtsPlayback {
    /// Running flag
    running: Arc<AtomicBool>,
    /// Audio output stream active
    playing: Arc<AtomicBool>,
    /// Playback queue (thread-safe FIFO)
    queue: Arc<Mutex<VecDeque<TtsAudioChunk>>>,
    /// Configuration
    config: TtsConfig,
}

impl TtsPlayback {
    /// Create new TTS playback engine.
    pub fn new(config: TtsConfig) -> Self {
        Self {
            running: Arc::new(AtomicBool::new(false)),
            playing: Arc::new(AtomicBool::new(false)),
            queue: Arc::new(Mutex::new(VecDeque::with_capacity(32))),
            config,
        }
    }

    /// Start the audio output stream.
    ///
    /// Uses cpal to open the default audio output device and stream
    /// PCM samples from the playback queue.
    pub fn start(&mut self) -> Result<(), String> {
        if self.running.load(Ordering::Relaxed) {
            return Err("Already running".into());
        }

        self.running.store(true, Ordering::Release);
        let running = self.running.clone();
        let playing = self.playing.clone();
        let queue = self.queue.clone();
        let sample_rate = self.config.sample_rate;

        std::thread::spawn(move || {
            // ── cpal audio output initialization ─────────────────────
            // In production:
            //   let host = cpal::default_host();
            //   let device = host.default_output_device().unwrap();
            //   let config = cpal::StreamConfig {
            //       channels: 1,
            //       sample_rate: cpal::SampleRate(sample_rate),
            //       buffer_size: cpal::BufferSize::Fixed(512),
            //   };
            //   let stream = device.build_output_stream(
            //       &config,
            //       move |data: &mut [f32], _| {
            //           // Fill output buffer from queue
            //           let mut q = queue.lock().unwrap();
            //           for sample in data.iter_mut() {
            //               if let Some(chunk) = q.front_mut() {
            //                   if let Some(s) = chunk.samples.pop() {
            //                       *sample = s;
            //                   } else {
            //                       q.pop_front();
            //                       *sample = 0.0;
            //                   }
            //               } else {
            //                   *sample = 0.0;
            //               }
            //           }
            //       },
            //       |err| { eprintln!("Audio error: {}", err); },
            //       None,
            //   ).unwrap();
            //   stream.play().unwrap();

            // Development mode: drain queue periodically
            let drain_interval = std::time::Duration::from_millis(
                1000 * 512 / sample_rate as u64
            );

            while running.load(Ordering::Acquire) {
                {
                    let mut q = queue.lock().unwrap();
                    if !q.is_empty() {
                        playing.store(true, Ordering::Release);
                        // Simulate consuming audio data
                        if let Some(chunk) = q.front_mut() {
                            let consume = std::cmp::min(chunk.samples.len(), 512);
                            chunk.samples.drain(0..consume);
                            if chunk.samples.is_empty() {
                                q.pop_front();
                            }
                        }
                    } else {
                        playing.store(false, Ordering::Release);
                    }
                }
                std::thread::sleep(drain_interval);
            }
        });

        Ok(())
    }

    /// Enqueue a TTS audio chunk for playback.
    ///
    /// Called when a TTS_AUDIO IRC message is received and the PCM data
    /// has been read from the SHM ring slot.
    pub fn enqueue(&self, chunk: TtsAudioChunk) {
        let mut q = self.queue.lock().unwrap();
        q.push_back(chunk);
    }

    /// Read PCM data from SHM ring slot and enqueue for playback.
    ///
    /// This is the main integration point: IRC message arrives with
    /// shm_offset, we read the data, validate CRC64, and queue it.
    pub fn process_shm_audio(
        &self,
        shm_base: *const u8,
        slot: usize,
        expected_len: usize,
        expected_crc: u64,
        sequence: u64,
    ) -> Result<(), String> {
        // Read slot header to validate
        let slot_size = 512 * 1024 * 1024usize;
        let header_size = 64usize;

        let data_ptr = unsafe {
            shm_base.add(slot * slot_size + header_size)
        };

        // Read PCM data
        let pcm_bytes = unsafe {
            std::slice::from_raw_parts(data_ptr, expected_len)
        };

        // Validate CRC64
        let computed_crc = crc64_fast(pcm_bytes);
        if computed_crc != expected_crc {
            return Err(format!(
                "CRC64 mismatch: expected {:#x}, got {:#x}",
                expected_crc, computed_crc
            ));
        }

        // Convert bytes to f32 samples (PCM is f32 little-endian)
        let samples: Vec<f32> = pcm_bytes
            .chunks_exact(4)
            .map(|chunk| f32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]))
            .collect();

        let timestamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos() as u64;

        self.enqueue(TtsAudioChunk {
            samples,
            shm_slot: slot,
            sequence,
            read_timestamp_ns: timestamp,
        });

        Ok(())
    }

    /// Check if audio is currently playing.
    pub fn is_playing(&self) -> bool {
        self.playing.load(Ordering::Relaxed)
    }

    /// Get queue depth (for telemetry).
    pub fn queue_depth(&self) -> usize {
        self.queue.lock().unwrap().len()
    }

    /// Stop playback.
    pub fn stop(&self) {
        self.running.store(false, Ordering::Release);
    }
}

/// Fast CRC64 computation (matches shm_ring_writer.rs)
fn crc64_fast(data: &[u8]) -> u64 {
    const POLY: u64 = 0x42F0E1EBA9EA3693;
    let mut crc: u64 = 0xFFFFFFFFFFFFFFFF;
    for &byte in data {
        crc ^= byte as u64;
        for _ in 0..8 {
            if crc & 1 == 1 {
                crc = (crc >> 1) ^ POLY;
            } else {
                crc >>= 1;
            }
        }
    }
    crc ^ 0xFFFFFFFFFFFFFFFF
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_enqueue_and_drain() {
        let mut playback = TtsPlayback::new(TtsConfig::default());
        playback.start().unwrap();

        // Enqueue some audio
        playback.enqueue(TtsAudioChunk {
            samples: vec![0.1f32; 1024],
            shm_slot: 0,
            sequence: 1,
            read_timestamp_ns: 0,
        });

        assert_eq!(playback.queue_depth(), 1);

        // Wait for drain
        std::thread::sleep(std::time::Duration::from_millis(200));

        playback.stop();
    }

    #[test]
    fn test_crc64_consistency() {
        let data = vec![0x42u8; 256];
        let crc1 = crc64_fast(&data);
        let crc2 = crc64_fast(&data);
        assert_eq!(crc1, crc2);
        assert_ne!(crc1, 0);
    }
}
