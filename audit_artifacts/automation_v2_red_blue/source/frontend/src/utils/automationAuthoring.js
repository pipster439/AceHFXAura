// Transport and edit helpers only. Daemon remains the schema authority.
export async function automationRequest(path, method = 'GET', body, fetcher = fetch) {
  const response = await fetcher(`/api/automation/${path}`, {
    method, cache: 'no-store', ...(body === undefined ? {} : {headers: {'Content-Type':'application/json'}, body: JSON.stringify(body)})
  });
  const result = await response.json();
  if (!response.ok) { const error = new Error(result.message || result.errors?.[0]?.message || result.error || `HTTP ${response.status}`); error.result = result; throw error; }
  return result;
}
export function sparsePatch(before, after) {
  if (!before || !after || Array.isArray(before) || Array.isArray(after) || typeof before !== 'object' || typeof after !== 'object') return after;
  const patch = {};
  for (const key of Object.keys(before)) if (!(key in after)) patch[key] = null;
  for (const key of Object.keys(after)) if (JSON.stringify(before[key]) !== JSON.stringify(after[key])) patch[key] = sparsePatch(before[key], after[key]);
  return patch;
}
export function ruleForPairing(pairing, profile = '') {
  return {id: crypto.randomUUID(), model:'automation_v2', when:{mode:pairing.mode,
    condition:pairing.mode === 'event' ? {event:'event.kill'} : {field:'process',op:'==',value:'cs2.exe'}},
    action:pairing.action === 'activate_profile' ? {type:pairing.action,profile} :
      {type:pairing.action,lifetime:pairing.lifetime,effect:{kind:'profile_effect',name:profile}}};
}
