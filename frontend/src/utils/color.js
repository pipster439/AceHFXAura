export function rgbToHex(rgb) {
  if (!rgb || rgb.length < 3) return '#000000';
  return '#' + rgb.slice(0, 3).map(x => Math.max(0, Math.min(255, Math.floor(x))).toString(16).padStart(2, '0')).join('').toUpperCase();
}

export function hexToRgb(hex) {
  if (!hex) return [0, 0, 0];
  let clean = hex.replace('#', '');
  if (clean.length === 3) clean = clean.split('').map(c => c + c).join('');
  const num = parseInt(clean, 16);
  if (isNaN(num)) return [0, 0, 0];
  return [(num >> 16) & 255, (num >> 8) & 255, num & 255];
}

export function hsvToRgb(h, s, v) {
  let c = v * s;
  let x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  let m = v - c;
  let r1 = 0, g1 = 0, b1 = 0;
  if (h >= 0 && h < 60) { r1 = c; g1 = x; b1 = 0; }
  else if (h >= 60 && h < 120) { r1 = x; g1 = c; b1 = 0; }
  else if (h >= 120 && h < 180) { r1 = 0; g1 = c; b1 = x; }
  else if (h >= 180 && h < 240) { r1 = 0; g1 = x; b1 = c; }
  else if (h >= 240 && h < 300) { r1 = x; g1 = 0; b1 = c; }
  else if (h >= 300 && h < 360) { r1 = c; g1 = 0; b1 = x; }
  return [Math.round((r1 + m) * 255), Math.round((g1 + m) * 255), Math.round((b1 + m) * 255)];
}

export function sampleGradientColor(stops, ratio) {
  if (!stops || stops.length === 0) return '#D9232E';
  if (stops.length === 1) return stops[0].color;
  const sorted = [...stops].sort((a, b) => a.pos - b.pos);
  const norm = Math.max(0, Math.min(1, ratio));
  for (let i = 0; i < sorted.length - 1; i++) {
    const s1 = sorted[i];
    const s2 = sorted[i + 1];
    if (norm >= s1.pos && norm <= s2.pos) {
      const range = s2.pos - s1.pos;
      const factor = range > 0.0001 ? (norm - s1.pos) / range : 0;
      const c1 = hexToRgb(s1.color);
      const c2 = hexToRgb(s2.color);
      return rgbToHex([
        c1[0] + factor * (c2[0] - c1[0]),
        c1[1] + factor * (c2[1] - c1[1]),
        c1[2] + factor * (c2[2] - c1[2])
      ]);
    }
  }
  return sorted[sorted.length - 1].color;
}
