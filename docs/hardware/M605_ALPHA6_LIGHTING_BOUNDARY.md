# M605 alpha.6 firmware lighting boundary

AceHFXAura alpha.6 keeps firmware-side Hardware Analog as an experimental capability with a prerequisite. The normal Magnetic page cannot establish the prerequisite and disables its Apply control while Aura renders Direct RGB. This is a product safety decision, not a claim that the firmware feature is broken.

## Verified

- The existing typed `M605Runtime::SetAnalogEffect(0, enabled)` path sends the physically verified `51 2D` effect-0 modifier.
- The owner observed continuous Hall travel/range-reactive lighting when the keyboard was already in official firmware Static effect 0. Turning the modifier off restored constant Static lighting.
- Official Static main-key lighting set to red remained red after all keyboard USB cables were disconnected for about 15 seconds and reconnected to the PC in UEFI/BIOS, before Windows or ASUS services started.

## Unknown

- The exact FW 1.00.59 internal implementation. Only an official 1.00.58 image was available for static analysis.
- Which official command or profile operation persists the Static main-key setting. The `51 2C` handler's immediate RAM staging does not close downstream `50 55` or separate save behavior.
- A verified safe transition from Aura Direct RGB (`C0 81`) into firmware Static. Merely pausing Direct RGB and enabling `51 2D` did not produce visible Hall-reactive lighting in the owner's controlled test.
- Light Bar persistence and its relationship to main-key firmware lighting. In the BIOS observation the Light Bar did not match the red main keys; after Windows boot, the owner reported that toggling the physical RT switch brought it into sync.

## Deferred

- Automatic Lighting Ownership and Direct RGB to firmware Static switching.
- Custom `51 2C` replay, a production builder, and a Native HID allowlist entry.
- Firmware Static transition and reconnect/persistence automation.

No custom `51 2C` replay is authorized by the alpha.6 evidence. The normal Aura software lighting and key-event-based Static **按键高亮** remain available. **按键高亮** does not read Hall travel.
