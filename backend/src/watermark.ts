import { PNG } from "pngjs";

const MAX_MP = 8_000_000;
const MAX_BYTES = 8 * 1024 * 1024;

export function readPngSize(bytes: ArrayBuffer): { width: number; height: number } {
  const view = new DataView(bytes);
  if (bytes.byteLength < 24) {
    throw new Error("CLOUD_IMAGE_REJECTED");
  }
  const width = view.getUint32(16);
  const height = view.getUint32(20);
  console.log(`watermark: IHDR ${width}x${height} bytes=${bytes.byteLength}`);
  if (width * height > MAX_MP || bytes.byteLength > MAX_BYTES) {
    throw new Error("CLOUD_IMAGE_REJECTED");
  }
  return { width, height };
}

function drawGlyph(png: PNG, x: number, y: number, color: [number, number, number, number]) {
  if (x < 0 || y < 0 || x >= png.width || y >= png.height) {
    return;
  }
  const idx = (png.width * y + x) << 2;
  const a = color[3] / 255;
  png.data[idx] = Math.round(png.data[idx] * (1 - a) + color[0] * a);
  png.data[idx + 1] = Math.round(png.data[idx + 1] * (1 - a) + color[1] * a);
  png.data[idx + 2] = Math.round(png.data[idx + 2] * (1 - a) + color[2] * a);
}

// ─── Ariadne's Thread [AT-0031] ─────────────────────
// What: Bake SeenShot watermark into a already-capped PNG
// Why:  Watermark only on server at publish, one file in quota
// Date: 2026-08-25
// Related: backend/src/quota.ts
// ─────────────────────────────────────────────────────
export function applyWatermark(bytes: ArrayBuffer): Uint8Array {
  readPngSize(bytes);
  const png = PNG.sync.read(Buffer.from(bytes));
  const label = "SeenShot";
  const scale = Math.max(2, Math.floor(png.width / 400));
  const startX = Math.max(8, png.width - label.length * 8 * scale - 16);
  const startY = Math.max(8, png.height - 14 * scale - 12);
  const ink: [number, number, number, number] = [255, 255, 255, 180];
  for (let i = 0; i < label.length; i += 1) {
    const code = label.charCodeAt(i);
    for (let row = 0; row < 7; row += 1) {
      for (let col = 0; col < 5; col += 1) {
        const on = ((code + row * 3 + col) % 4) !== 0;
        if (!on) {
          continue;
        }
        for (let dy = 0; dy < scale; dy += 1) {
          for (let dx = 0; dx < scale; dx += 1) {
            drawGlyph(png, startX + i * 8 * scale + col * scale + dx, startY + row * scale + dy, ink);
          }
        }
      }
    }
  }
  const out = PNG.sync.write(png);
  console.log(`watermark: output bytes=${out.length}`);
  return out;
}
