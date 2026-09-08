//! UTF-8 launcher labels, using the same rasterizer as game text.
use ab_glyph::{point, Font, FontRef, ScaleFont};

/// Buffers are borrowed for this call only. Output is straight-alpha white RGBA.
/// The host supplies valid, non-overlapping buffers of the stated lengths.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn art3m1s_launcher_label(
    font: *const u8, font_len: usize, text: *const u8, text_len: usize,
    pixels: *mut u8, width: usize, height: usize,
) -> i32 {
    if font.is_null() || text.is_null() || pixels.is_null()
        || font_len > 32 * 1024 * 1024 || text_len > 512
        || width == 0 || width > 1024 || height < 8 || height > 128 {
        return -1;
    }
    let Ok(font) = FontRef::try_from_slice(unsafe { std::slice::from_raw_parts(font, font_len) }) else { return -1; };
    let Ok(text) = std::str::from_utf8(unsafe { std::slice::from_raw_parts(text, text_len) }) else { return -1; };
    let pixels = unsafe { std::slice::from_raw_parts_mut(pixels, width * height * 4) };
    pixels.fill(0);
    let scaled = font.as_scaled(ab_glyph::PxScale::from(height as f32 - 4.0));
    let mut advance = 0.0;
    let mut previous = None;
    for ch in text.trim_start_matches('\u{feff}').chars() {
        let id = scaled.glyph_id(ch);
        if let Some(prev) = previous { advance += scaled.kern(prev, id); }
        if let Some(outline) = scaled.outline_glyph(id.with_scale_and_position(
            scaled.scale(), point(advance, 2.0 + scaled.ascent()))) {
            let bounds = outline.px_bounds();
            outline.draw(|x, y, coverage| {
                let x = x as i32 + bounds.min.x as i32;
                let y = y as i32 + bounds.min.y as i32;
                if x >= 0 && y >= 0 && (x as usize) < width && (y as usize) < height {
                    let offset = (y as usize * width + x as usize) * 4;
                    let alpha = (coverage * 255.0).round() as u8;
                    pixels[offset..offset + 3].fill(255);
                    let old = pixels[offset + 3] as u16;
                    pixels[offset + 3] = (old + (255 - old) * alpha as u16 / 255) as u8;
                }
            });
        }
        advance += scaled.h_advance(id);
        if advance >= width as f32 { break; }
        previous = Some(id);
    }
    0
}
