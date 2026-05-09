// ═══════════════════════════════════════════════════════════════════════
// moviola_capture.rs — UI-007: Delta-Motion Preprocessor
//
// Converts webcam frames to grayscale, computes Δ(frame[n]-frame[n-1]),
// packs 1-bit change-maps into Di-Bit tokens (10×10 micro-grids per
// Moviola Protocol §4.1-4.3).
//
// Writes Di-Bit tokens to SHM ring, announces MOVIOLA_DELTA via IRC.
// Target: >90fps with >99% sparsity on static scenes.
//
// Reference: Interactive_Plan.md §XI UI-007, Moviola Protocol §4.1-4.3
// ═══════════════════════════════════════════════════════════════════════

use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;

/// Moviola configuration
pub struct MoviolaConfig {
    /// Delta threshold (pixel intensity difference to count as "changed")
    /// Default: 15 (out of 255)
    pub delta_threshold: u8,
    /// Micro-grid dimensions (default: 10×10 cells per frame)
    pub grid_cols: u32,
    pub grid_rows: u32,
    /// Target FPS for Moviola processing
    pub target_fps: u32,
}

impl Default for MoviolaConfig {
    fn default() -> Self {
        Self {
            delta_threshold: 15,
            grid_cols: 10,
            grid_rows: 10,
            target_fps: 90,
        }
    }
}

/// Delta frame result
#[derive(Debug, Clone)]
pub struct DeltaFrame {
    /// 1-bit change map (one bit per pixel; 1=changed, 0=static)
    pub change_map: Vec<u8>,
    /// Width in pixels
    pub width: u32,
    /// Height in pixels
    pub height: u32,
    /// Total pixels that changed
    pub active_pixels: u64,
    /// Total pixels
    pub total_pixels: u64,
    /// Sparsity (fraction of unchanged pixels: 1.0 = fully static)
    pub sparsity: f64,
    /// Frame timestamp
    pub timestamp_ns: u64,
    /// Frame number
    pub frame_number: u64,
}

/// Di-Bit token (10×10 micro-grid, 2-bit encoding per cell)
/// Encoding: 00=static, 01=onset, 10=offset, 11=sustained
#[derive(Debug, Clone)]
pub struct DiBitToken {
    /// Packed Di-Bit data (2 bits per cell, ceil(100/4) = 25 bytes for 10×10)
    pub packed_data: Vec<u8>,
    /// Number of cells (grid_cols × grid_rows)
    pub cell_count: u32,
    /// Number of active cells (non-00)
    pub active_cells: u32,
    /// Sparsity at cell level
    pub cell_sparsity: f64,
    /// Source frame number
    pub frame_number: u64,
    /// Timestamp
    pub timestamp_ns: u64,
}

/// Moviola capture engine
pub struct MoviolaCapture {
    config: MoviolaConfig,
    running: Arc<AtomicBool>,
    /// Previous frame (grayscale) for delta computation
    prev_frame: Option<Vec<u8>>,
    /// Previous Di-Bit cell states (for onset/offset/sustained encoding)
    prev_cell_active: Option<Vec<bool>>,
    /// Frame counter
    frame_count: AtomicU64,
}

impl MoviolaCapture {
    pub fn new(config: MoviolaConfig) -> Self {
        Self {
            config,
            running: Arc::new(AtomicBool::new(false)),
            prev_frame: None,
            prev_cell_active: None,
            frame_count: AtomicU64::new(0),
        }
    }

    /// Convert BGRA frame to grayscale.
    pub fn to_grayscale(bgra: &[u8], width: u32, height: u32) -> Vec<u8> {
        let pixel_count = (width * height) as usize;
        let mut gray = Vec::with_capacity(pixel_count);

        for i in 0..pixel_count {
            let offset = i * 4;
            if offset + 2 < bgra.len() {
                let b = bgra[offset] as u16;
                let g = bgra[offset + 1] as u16;
                let r = bgra[offset + 2] as u16;
                // ITU-R BT.601 grayscale conversion
                gray.push(((r * 77 + g * 150 + b * 29) >> 8) as u8);
            }
        }

        gray
    }

    /// Compute delta frame: Δ(current - previous).
    ///
    /// Returns a DeltaFrame with 1-bit change map and sparsity.
    pub fn compute_delta(
        &mut self,
        current_gray: &[u8],
        width: u32,
        height: u32,
    ) -> DeltaFrame {
        let pixel_count = (width * height) as usize;
        let threshold = self.config.delta_threshold;
        let frame_number = self.frame_count.fetch_add(1, Ordering::Relaxed);

        // Bit-packed change map (1 bit per pixel)
        let map_bytes = (pixel_count + 7) / 8;
        let mut change_map = vec![0u8; map_bytes];
        let mut active_pixels: u64 = 0;

        if let Some(ref prev) = self.prev_frame {
            for i in 0..pixel_count {
                if i < prev.len() && i < current_gray.len() {
                    let diff = (current_gray[i] as i16 - prev[i] as i16).unsigned_abs() as u8;
                    if diff > threshold {
                        change_map[i / 8] |= 1 << (i % 8);
                        active_pixels += 1;
                    }
                }
            }
        }

        // Store current frame for next delta
        self.prev_frame = Some(current_gray.to_vec());

        let total_pixels = pixel_count as u64;
        let sparsity = if total_pixels > 0 {
            1.0 - (active_pixels as f64 / total_pixels as f64)
        } else {
            1.0
        };

        let timestamp_ns = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos() as u64;

        DeltaFrame {
            change_map,
            width,
            height,
            active_pixels,
            total_pixels,
            sparsity,
            timestamp_ns,
            frame_number,
        }
    }

    /// Pack delta frame into Di-Bit token (10×10 micro-grid).
    ///
    /// Each cell covers a rectangular region of the frame.
    /// 2-bit encoding per cell:
    ///   00 = static (no change this frame or last)
    ///   01 = onset  (just became active)
    ///   10 = offset (just became inactive)
    ///   11 = sustained (active this frame and last)
    pub fn pack_dibit(&mut self, delta: &DeltaFrame) -> DiBitToken {
        let cols = self.config.grid_cols;
        let rows = self.config.grid_rows;
        let cell_count = cols * rows;

        let cell_w = delta.width / cols;
        let cell_h = delta.height / rows;

        // Compute per-cell activity
        let mut cell_active = vec![false; cell_count as usize];

        for row in 0..rows {
            for col in 0..cols {
                let cell_idx = (row * cols + col) as usize;
                let mut cell_has_activity = false;

                // Check if any pixel in this cell's region changed
                for py in (row * cell_h)..((row + 1) * cell_h).min(delta.height) {
                    for px in (col * cell_w)..((col + 1) * cell_w).min(delta.width) {
                        let pixel_idx = (py * delta.width + px) as usize;
                        let byte_idx = pixel_idx / 8;
                        let bit_idx = pixel_idx % 8;
                        if byte_idx < delta.change_map.len() {
                            if delta.change_map[byte_idx] & (1 << bit_idx) != 0 {
                                cell_has_activity = true;
                                break;
                            }
                        }
                    }
                    if cell_has_activity { break; }
                }

                cell_active[cell_idx] = cell_has_activity;
            }
        }

        // Compute 2-bit Di-Bit encoding using previous cell states
        let packed_bytes = ((cell_count * 2) as usize + 7) / 8;
        let mut packed_data = vec![0u8; packed_bytes];
        let mut active_cells = 0u32;

        for i in 0..cell_count as usize {
            let current = cell_active[i];
            let previous = self.prev_cell_active
                .as_ref()
                .map(|p| p.get(i).copied().unwrap_or(false))
                .unwrap_or(false);

            // 2-bit encoding
            let bits: u8 = match (previous, current) {
                (false, false) => 0b00, // static
                (false, true)  => 0b01, // onset
                (true,  false) => 0b10, // offset
                (true,  true)  => 0b11, // sustained
            };

            if bits != 0 {
                active_cells += 1;
            }

            // Pack 2 bits at position i*2
            let bit_offset = i * 2;
            let byte_idx = bit_offset / 8;
            let shift = bit_offset % 8;
            packed_data[byte_idx] |= bits << shift;
        }

        // Store cell states for next frame
        self.prev_cell_active = Some(cell_active);

        let cell_sparsity = if cell_count > 0 {
            1.0 - (active_cells as f64 / cell_count as f64)
        } else {
            1.0
        };

        DiBitToken {
            packed_data,
            cell_count,
            active_cells,
            cell_sparsity,
            frame_number: delta.frame_number,
            timestamp_ns: delta.timestamp_ns,
        }
    }

    /// Full pipeline: BGRA → grayscale → delta → Di-Bit token.
    pub fn process_frame(
        &mut self,
        bgra: &[u8],
        width: u32,
        height: u32,
    ) -> (DeltaFrame, DiBitToken) {
        let gray = Self::to_grayscale(bgra, width, height);
        let delta = self.compute_delta(&gray, width, height);
        let dibit = self.pack_dibit(&delta);
        (delta, dibit)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_static_scene_sparsity() {
        let mut moviola = MoviolaCapture::new(MoviolaConfig::default());

        // Two identical frames → >99% sparsity
        let frame = vec![128u8; 640 * 480 * 4]; // BGRA gray

        let (delta1, _) = moviola.process_frame(&frame, 640, 480);
        // First frame has no previous → all zeros
        assert_eq!(delta1.active_pixels, 0);

        let (delta2, dibit) = moviola.process_frame(&frame, 640, 480);
        assert_eq!(delta2.active_pixels, 0);
        assert!(delta2.sparsity > 0.99, "Sparsity should be >99% for static scene");
        assert_eq!(dibit.active_cells, 0, "No active cells in static scene");
    }

    #[test]
    fn test_motion_detection() {
        let mut moviola = MoviolaCapture::new(MoviolaConfig {
            delta_threshold: 10,
            ..Default::default()
        });

        // Frame 1: all zeros
        let frame1 = vec![0u8; 640 * 480 * 4];
        moviola.process_frame(&frame1, 640, 480);

        // Frame 2: top-left corner is bright
        let mut frame2 = vec![0u8; 640 * 480 * 4];
        for y in 0..48 {
            for x in 0..64 {
                let idx = (y * 640 + x) * 4;
                frame2[idx] = 200;     // B
                frame2[idx + 1] = 200; // G
                frame2[idx + 2] = 200; // R
                frame2[idx + 3] = 255; // A
            }
        }

        let (delta, dibit) = moviola.process_frame(&frame2, 640, 480);
        assert!(delta.active_pixels > 0, "Should detect motion");
        assert!(dibit.active_cells > 0, "Should have active Di-Bit cells");
    }

    #[test]
    fn test_dibit_encoding() {
        let mut moviola = MoviolaCapture::new(MoviolaConfig {
            grid_cols: 2,
            grid_rows: 2,
            delta_threshold: 10,
            ..Default::default()
        });

        // 4-cell grid (2×2) with known patterns
        let frame1 = vec![0u8; 20 * 20 * 4]; // 20×20 BGRA
        moviola.process_frame(&frame1, 20, 20);

        // Frame 2 with activity in top-left only
        let mut frame2 = vec![0u8; 20 * 20 * 4];
        for y in 0..10 {
            for x in 0..10 {
                let idx = (y * 20 + x) * 4;
                frame2[idx] = 200;
                frame2[idx + 1] = 200;
                frame2[idx + 2] = 200;
            }
        }

        let (_, dibit) = moviola.process_frame(&frame2, 20, 20);
        assert_eq!(dibit.cell_count, 4);
        // Top-left cell should be onset (01)
        assert!(dibit.active_cells >= 1);
    }
}
