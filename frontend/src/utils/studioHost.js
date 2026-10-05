// Native UI is a command surface; the retained editor remains the document owner.
export const STUDIO_COMMANDS = Object.freeze(['new', 'open', 'select', 'save', 'preview', 'validate', 'build', 'publish', 'details', 'bench']);
export function listenStudioCommands(webview, handlers) {
  if (!webview?.addEventListener) return () => {};
  const receive = event => {
    const m = event.data;
    if (m?.type !== 'studio_command' || !STUDIO_COMMANDS.includes(m.command)) return;
    if (Object.keys(m).some(k => !['type', 'command', 'name'].includes(k))) return;
    if (m.command === 'select' && (typeof m.name !== 'string' || m.name.length > 128)) return;
    handlers[m.command]?.(m);
  };
  webview.addEventListener('message', receive);
  return () => webview.removeEventListener('message', receive);
}
export function postStudioState(state) {
  window.chrome?.webview?.postMessage({ type: 'studio_state', ...state });
}
export function initialStudioProject(effects, draft) {
  if (draft?.json && typeof draft.name === 'string' && /^[a-zA-Z_][a-zA-Z0-9_]{0,47}$/.test(draft.name)) return draft.name;
  return Object.keys(effects || {})[0] || 'custom_rainbow';
}
