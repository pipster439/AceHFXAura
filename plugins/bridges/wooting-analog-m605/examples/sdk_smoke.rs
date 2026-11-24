use std::time::{Duration, Instant};
use wooting_analog_plugin_aura_m605::AuraM605Plugin;
use wooting_analog_plugin_dev::wooting_analog_common::{DeviceEventType, DeviceInfo};
use wooting_analog_plugin_dev::Plugin;

fn main() {
    let mut plugin = AuraM605Plugin::new();
    let name = plugin.name().0.expect("plugin name");
    println!("[sdk_smoke] Plugin: {name}");

    let cb = Box::new(|evt: DeviceEventType, info: &DeviceInfo| {
        println!(
            "[sdk_smoke] Event {:?}: {} {} ({:04X}:{:04X}, id={})",
            evt, info.manufacturer_name, info.device_name, info.vendor_id, info.product_id, info.device_id
        );
    });

    let count = plugin.initialise(cb).0.expect("initialise");
    println!("[sdk_smoke] Connected devices: {count}");

    let devices = plugin.device_info().0.unwrap_or_default();
    for dev in &devices {
        println!(
            "[sdk_smoke] DeviceInfo: {} | {} | {:04X}:{:04X} | {:?}",
            dev.manufacturer_name, dev.device_name, dev.vendor_id, dev.product_id, dev.device_type
        );
    }

    // Poll briefly so automated smoke runs finish quickly and verify live SHM readback
    let start = Instant::now();
    let mut reads = 0u32;
    let mut saw_a = None;
    while start.elapsed() < Duration::from_millis(800) {
        if let Ok(a_val) = plugin.read_analog(4, 0).0 {
            saw_a = Some(a_val);
            reads += 1;
        }
        if let Ok(buf) = plugin.read_full_buffer(68, 0).0 {
            if !buf.is_empty() {
                println!("[sdk_smoke] Active analog keys: {:?}", buf);
            }
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    println!(
        "[sdk_smoke] Completed reads={reads}, last_A(HID=4)={:?}",
        saw_a
    );
    plugin.unload();
}
