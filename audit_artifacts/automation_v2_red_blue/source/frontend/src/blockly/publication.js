// Explicit rendering publication contract. Old workspaces never imply one-shot.
export function normalizePublication(value = {}) {
  const mode = value?.mode ?? 'continuous';
  const fade_out_ms = value?.fade_out_ms ?? 0;
  if (!['continuous', 'one_shot'].includes(mode)) throw new Error('Invalid publication mode');
  if (!Number.isInteger(fade_out_ms) || fade_out_ms < 0 || fade_out_ms > 60000) throw new Error('Fade must be 0..60000 ms');
  return { mode, fade_out_ms: mode === 'one_shot' ? fade_out_ms : 0 };
}
