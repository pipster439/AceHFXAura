pub mod shm;

use std::collections::HashMap;
use std::os::raw::{c_float, c_ushort};
use shm::{SharedSnapshot, ShmReader};
use wooting_analog_plugin_dev::wooting_analog_common::*;
use wooting_analog_plugin_dev::*;

pub const AURA_M605_VID: u16 = 0x0B05;
pub const AURA_M605_PID: u16 = 0x1B7E;

pub struct AuraM605Plugin {
    initialised: bool,
    connected: bool,
    reader: ShmReader,
    device: DeviceInfo,
    callback: Option<Box<dyn Fn(DeviceEventType, &DeviceInfo) + Send>>,
}

impl AuraM605Plugin {
    pub fn new() -> Self {
        let device_id = generate_device_id("AURA-M605-HFX-SHM", AURA_M605_VID, AURA_M605_PID);
        let device = DeviceInfo::new_with_id(
            AURA_M605_VID,
            AURA_M605_PID,
            "ASUS ROG (via Aura)".to_string(),
            "Falchion Ace HFX".to_string(),
            device_id,
            DeviceType::Keyboard,
        );
        Self {
            initialised: false,
            connected: false,
            reader: ShmReader::new(),
            device,
            callback: None,
        }
    }

    fn poll_snapshot(&mut self) -> Option<SharedSnapshot> {
        let snap = self.reader.read_snapshot();
        let now_connected = snap.as_ref().map_or(false, |s| s.active);
        if now_connected != self.connected {
            self.connected = now_connected;
            if let Some(cb) = &self.callback {
                let evt = if now_connected {
                    DeviceEventType::Connected
                } else {
                    DeviceEventType::Disconnected
                };
                cb(evt, &self.device);
            }
        }
        snap
    }
}

impl Default for AuraM605Plugin {
    fn default() -> Self {
        Self::new()
    }
}

impl Plugin for AuraM605Plugin {
    fn name(&mut self) -> SDKResult<&'static str> {
        Ok("Aura ROG Falchion Ace HFX (M605)").into()
    }

    fn initialise(
        &mut self,
        callback: Box<dyn Fn(DeviceEventType, &DeviceInfo) + Send>,
    ) -> SDKResult<u32> {
        self.callback = Some(callback);
        self.initialised = true;
        self.connected = false;
        let _ = self.poll_snapshot();
        Ok(if self.connected { 1 } else { 0 }).into()
    }

    fn is_initialised(&mut self) -> bool {
        self.initialised
    }

    fn device_info(&mut self) -> SDKResult<Vec<DeviceInfo>> {
        if !self.initialised {
            return Err(WootingAnalogResult::UnInitialized).into();
        }
        let _ = self.poll_snapshot();
        if self.connected {
            Ok(vec![self.device.clone()]).into()
        } else {
            Ok(Vec::new()).into()
        }
    }

    fn unload(&mut self) {
        self.initialised = false;
        self.connected = false;
        self.callback = None;
        self.reader.close();
    }

    fn read_analog(&mut self, code: u16, device: DeviceID) -> SDKResult<f32> {
        if !self.initialised {
            return Err(WootingAnalogResult::UnInitialized).into();
        }
        if device != 0 && device != self.device.device_id {
            return Err(WootingAnalogResult::NoDevices).into();
        }
        let Some(snap) = self.poll_snapshot() else {
            return Err(WootingAnalogResult::NoDevices).into();
        };
        if !snap.active {
            return Err(WootingAnalogResult::NoDevices).into();
        }
        for key in &snap.keys {
            if key.hid_usage == code {
                let v = key.normalized.clamp(0.0, 1.0);
                return Ok(if v < 0.015 { 0.0 } else { v }).into();
            }
        }
        Ok(0.0).into()
    }

    fn read_full_buffer(
        &mut self,
        max_length: usize,
        device: DeviceID,
    ) -> SDKResult<HashMap<c_ushort, c_float>> {
        if !self.initialised {
            return Err(WootingAnalogResult::UnInitialized).into();
        }
        if device != 0 && device != self.device.device_id {
            return Err(WootingAnalogResult::NoDevices).into();
        }
        let Some(snap) = self.poll_snapshot() else {
            return Err(WootingAnalogResult::NoDevices).into();
        };
        if !snap.active {
            return Err(WootingAnalogResult::NoDevices).into();
        }

        let mut map = HashMap::new();
        for key in &snap.keys {
            if map.len() >= max_length {
                break;
            }
            // Standard USB HID Page 0x07 usages (skip synthetic FN=232), with 0.02 (~0.08mm) noise floor
            if key.hid_usage >= 4 && key.hid_usage <= 231 && key.normalized > 0.02 {
                map.insert(key.hid_usage as c_ushort, key.normalized.clamp(0.0, 1.0));
            }
        }
        Ok(map).into()
    }
}

declare_plugin!(AuraM605Plugin, AuraM605Plugin::new);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plugin_identity_matches_m605() {
        let mut p = AuraM605Plugin::new();
        assert_eq!(p.name().0.unwrap(), "Aura ROG Falchion Ace HFX (M605)");
        assert_eq!(p.device.vendor_id, 0x0B05);
        assert_eq!(p.device.product_id, 0x1B7E);
        assert_eq!(p.device.device_type, DeviceType::Keyboard);
    }
}
