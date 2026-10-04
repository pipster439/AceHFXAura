using Microsoft.Win32;

namespace AceHFX.AsusPlatform.Runtime;

public sealed class AsusCapabilityDetector(IRuntimeInventory inventory) : IAsusCapabilityProvider
{
    // Canonical coclass IDs copied from Phase 2.1 raw TypeLib-derived generated bindings.
    // No COM interfaces are needed or activated in M1.
    internal const string AuraSdkClass = "05921124-5057-483E-A037-E9497B523590";
    internal const string AuraDevelopmentClass = "34B707DC-1133-4EBC-B380-21387A50A89D";
    internal const string MediatorClass = "95775DC4-77AA-4E94-8CF6-68267EEF1856";
    internal const string FanClass = "14083C53-B8E7-48E4-9320-811F3478C4A4";
    public AsusPlatformCapabilities Discover()
    {
        var services = new[] { "LightingService", "AsusFanControlService", "asComSvc", "Asusgio3", "AsIO3",
            "AsusCertService", "ArmouryCrateService", "ROG Live Service", "ArmourySocketServer", "AceHFXService" }
            .Select(inventory.Service).ToArray();
        var definitions = new[] { ("AuraSdk", AuraSdkClass), ("AuraDevelopment", AuraDevelopmentClass),
            ("ServiceMediator", MediatorClass), ("FanControlManager", FanClass) };
        var com = definitions.SelectMany(d => new[] { RegistryView.Registry64, RegistryView.Registry32 }
            .Select(view => inventory.ComClass(d.Item1 + ":" + view, d.Item2, view))).ToArray();
        ComponentStatus Service(string name) => services.Single(s => s.Name == name);
        ComponentStatus Com(string name)
        {
            var native = com.Single(s => s.Name == name + ":Registry64");
            // In-proc Aura must match the product's x64 architecture. Verified vendor
            // out-of-proc servers may be registered only in the 32-bit registry view.
            return name == "AuraSdk" || native.Registered.Value == true ? native :
                com.Single(s => s.Name == name + ":Registry32");
        }
        var aura = Stack(Service("LightingService"), Com("AuraSdk").Registered);
        var cooling = Stack(Service("AsusFanControlService"), Com("FanControlManager").Registered);
        var primaryDriver = Service("Asusgio3");
        var alternateDriver = Service("AsIO3");
        var driver = primaryDriver.Installed.Value == true ||
            (alternateDriver.Installed.Value != true && primaryDriver.Installed.Value != false) ? primaryDriver : alternateDriver;
        return new(DateTimeOffset.UtcNow,
            new(Com("AuraSdk"), Service("LightingService"), Com("ServiceMediator"),
                Evidence.NotAttempted("SharedMemoryNotOpenedM1"), inventory.DynamicLighting(),
                new(aura, Evidence.NotAttempted("AuraControlNotValidatedM1"))),
            new(Service("AsusFanControlService"), Com("FanControlManager"), driver, Service("asComSvc"),
                new(cooling, Evidence.NotAttempted("CoolingControlNotValidatedM1"), true)),
            services, services.Concat(com).ToArray(), inventory.SecurityContext());
    }
    private static Evidence Stack(ComponentStatus service, Evidence registered)
    {
        var running = service.Running;
        if (service.Installed.State == CapabilityState.PermissionDenied || running.State == CapabilityState.PermissionDenied || registered.State == CapabilityState.PermissionDenied)
            return Evidence.Denied("StackMetadataAccessDenied");
        if (running.Value == false) return running;
        if (registered.Value == false) return registered;
        return running.Value == true && registered.Value == true ? Evidence.Detected(true, "RunningAndRegisteredOnly") :
            Evidence.NotAttempted("StackEvidenceIncomplete");
    }
}
