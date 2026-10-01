# Profile Rapid Trigger validation presentation

## RT production cutover update

The current explicit per-key planner, prior-state restoration, `51 54` typed
selector contract and `51 53` rejection are documented in
[PROFILE_RT_PRODUCTION_MODEL.md](PROFILE_RT_PRODUCTION_MODEL.md).
Earlier production proposals below are historical evidence/context, not the
current apply contract. No global/common RT inheritance is implemented.


The daemon remains the authoritative document writer, validator and apply owner.
WinUI computes local draft messages only and sends no magnetic setter from an
editor control. Profile schema and daemon safety semantics are unchanged.

See [corrected product model](PROFILE_RT_PRODUCT_MODEL.md). Normal editing is
selected-key RT. **管理所选按键** controls draft object presence, and **启用所选
按键的快速触发** controls per-key enable flags. There is no overall master.
Old global data is isolated under a compatibility expander whose switch is
**启用快速触发设置（旧版参数）**, not a hardware switch readback or per-key master.

| Draft condition | Local issue code | Presentation |
|---|---|---|
| Legacy RT configured, enable false | RtMasterDisabled | Specifically identifies the old parameters and the enable switch; per-key disabled states remain legal |
| Sensitivity outside 0.1–2.5 / not 0.1 step | RtSensitivityRange | Specific sensitivity range error |
| RT top/bottom outside 0–0.5 / invalid step | RtDeadzoneRange | Specific RT limit range error |
| Unified mode with different press/release | RtSeparateMode | Explain matching values or separate mode |
| Enabled per-key RT plus nonstandard DKS | RtDksConflict | Specific key-labelled conflict |

Summary and inline RT InfoBars derive from the current draft every render.
Multiple issues remain visible. Other Actuation/Deadzone/DKS issues retain their
own messages. A failed Save does not leave a copied validation message in the
persistent Notice slot, so enabling RT or changing Profile cannot retain a stale
error. Revision conflict keeps the user's complete draft for explicit review.

Legacy subordinate numeric controls require the legacy enable flag; release
also requires separate mode. Per-key controls require managed and enabled keys.
Counts derive only from explicit draft keys. Multi-selection says it shows
first-key values and updates all selected keys. Disabled legacy RT remains
invalid under the old schema; numbers are not silently discarded. Removing a
per-key object restores trusted manual configuration under current runtime
policy, not firmware common inheritance. Both Apply entry points respect draft
validation. Per-key inline errors are separate from legacy compatibility errors.

The opt-in `--validate-profile-rt-layout` fixture renders the actual native
ProfilesPage against an offline client. Its mutation/activation methods throw,
and the existing validation startup guard skips daemon startup. Normal production
launch and ownership are unchanged. Captures cover unmanaged, managed with zero
enabled keys, one/many/all enabled keys, separate sensitivity values, DKS conflict,
legacy blocker, 800-DIP narrow and dark-theme states. A structural assertion is not pixel testing;
actual render results are reported separately.
