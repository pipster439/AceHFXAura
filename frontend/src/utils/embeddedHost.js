// The URL selects an initial workspace only. Subsequent workspace changes stay in Studio.
export function readHostSettings(search, storage) {
  const params = new URLSearchParams(search);
  const embedded = params.get('host') === 'winui';
  const requestedTab = params.get('tab');
  const tab = embedded
    ? (requestedTab === 'automation' ? 'automation' : 'studio')
    : (['studio', 'automation'].includes(requestedTab) ? requestedTab : 'lighting');
  const requestedTheme = params.get('theme');
  const theme = embedded
    ? (['dark', 'light'].includes(requestedTheme) ? requestedTheme : 'dark')
    : (storage.getItem('aura-theme') || 'dark');
  return { embedded, tab, theme };
}

// Display-only, one-way host message. Authoring and publication remain HTTP-only.
export function listenForHostTheme(webview, onTheme) {
  if (!webview?.addEventListener) return () => {};
  const receive = event => {
    const message = event.data;
    if (message?.type === 'theme_changed' && ['dark', 'light'].includes(message.theme)) {
      onTheme(message.theme);
    }
  };
  webview.addEventListener('message', receive);
  return () => webview.removeEventListener('message', receive);
}
