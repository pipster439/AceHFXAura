using System.Security.Cryptography;
using System.Xml;
using System.Xml.Linq;
using Microsoft.Win32;
using Windows.Devices.Enumeration;
using Windows.Devices.Lights;

namespace AceHFX.AsusPlatform.Aura;

public static class AuraObservations
{
    public static async Task<DynamicLightingObservation> DynamicLightingAsync(CancellationToken token)
    {
        var settings = new Dictionary<string,string>(StringComparer.Ordinal);
        Evidence enabled=Evidence.NotAttempted("UserLightingSettingMissing");
        try {
            using var key=Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Lighting",false);
            if(key!=null) foreach(var name in key.GetValueNames().Take(64)) {
                if(key.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames) is int value) settings[name]=value.ToString(System.Globalization.CultureInfo.InvariantCulture);
            }
            if(settings.TryGetValue("IsLampArrayEnabled",out var raw)) enabled=Evidence.Detected(raw!="0","UserSettingOnlyNotOwnerProof");
            else if(settings.TryGetValue("AmbientLightingEnabled",out var ambient)) enabled=Evidence.Detected(ambient!="0","AmbientLightingRegistryFlagOnlyUiNotValidated");
        } catch(Exception e) when(e is UnauthorizedAccessException or System.Security.SecurityException) { enabled=Evidence.Denied("UserLightingSettingsDenied"); }
        catch(IOException) { enabled=Evidence.Unknown("UserLightingSettingsUnavailable"); }
        try {
            // Enumeration only. FromIdAsync/IsEnabled/color effects would open a LampArray control session.
            var devices=await DeviceInformation.FindAllAsync(LampArray.GetDeviceSelector()).AsTask(token);
            var lamps=devices.Select(d=>new LampArrayDescriptor(d.Id,d.Name,d.IsEnabled,
                d.Name.Contains("FALCHION",StringComparison.OrdinalIgnoreCase) && d.Name.Contains("HFX",StringComparison.OrdinalIgnoreCase) ? true : null,
                "DeviceInformationNameOnlyNoPhysicalIdentityProof")).ToArray();
            return new(ExecutionState.Succeeded,settings,enabled,lamps,
                Evidence.NotAttempted("AmbientOwnerNotProvenByRegistryOrPnP"),
                Evidence.NotAttempted("AuraLampArrayIdentityCrossReferenceUnknown"),"DeviceInterfacesEnumeratedNoControlSessionOpened");
        } catch(OperationCanceledException) { throw; }
        catch(Exception e) when(e is UnauthorizedAccessException or System.Runtime.InteropServices.COMException or InvalidOperationException or TypeLoadException or NotSupportedException or MissingMethodException) {
            return new(e is UnauthorizedAccessException ? ExecutionState.PermissionDenied : e is TypeLoadException or NotSupportedException or MissingMethodException ? ExecutionState.Unavailable : ExecutionState.Failed,settings,enabled,[],
                Evidence.NotAttempted("AmbientOwnerUnknown"),Evidence.NotAttempted("AuraParticipationUnknown"),"LampArrayEnumerationFailed");
        }
    }
    public static LightingTopologyObservation LightingTopology() => ReadLightingTopology(
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),@"ASUS\RogAura30\GetDeviceCap.xml"),
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),@"LightingService\DevLastStatConfig.xml"));
    internal static LightingTopologyObservation ReadLightingTopology(string capabilityPath,string lastStatePath)
    {
        var sync=new Dictionary<string,string>(); var unsync=new Dictionary<string,string>();
        var zones=new List<LightingTopologyZone>(); string? digest=null;
        try {
            var data=ReadBounded(capabilityPath); digest=Convert.ToHexString(SHA256.HashData(data));
            using var stream=new MemoryStream(data); var xml=LoadXml(stream);
            foreach(var device in xml.Descendants("device")) foreach(var led in device.Descendants("led")) {
                if(zones.Count>=8192) throw new InvalidDataException("LightingTopologyLimit");
                zones.Add(new((string?)device.Attribute("key")??"Unknown",(string?)led.Attribute("key")??"Unknown",
                    uint.TryParse((string?)led.Element("location"),out var location)?location:null,
                    (string?)led.Element("locationname"),(string?)led.Element("type")));
            }
            using var last=new MemoryStream(ReadBounded(lastStatePath)); var status=LoadXml(last);
            foreach(var device in status.Descendants("lastsynclist").Elements("device")) sync[(string?)device.Attribute("key")??"Unknown"]=device.Value;
            foreach(var device in status.Descendants("lastunsynclist").Elements("device")) unsync[(string?)device.Attribute("key")??"Unknown"]=device.Value;
            return new(ExecutionState.Succeeded,capabilityPath,digest,zones,sync,unsync,"VendorFileEvidenceNotLiveOwnerOrPhysicalIdentity");
        } catch(UnauthorizedAccessException) { return new(ExecutionState.PermissionDenied,capabilityPath,digest,zones,sync,unsync,"LightingTopologyReadDenied"); }
        catch(Exception e) when(e is IOException or XmlException) { return new(ExecutionState.Unavailable,capabilityPath,digest,zones,sync,unsync,"LightingTopologyIncomplete"); }
    }
    private static byte[] ReadBounded(string path) {
        using var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);
        if(file.Length>2*1024*1024) throw new InvalidDataException("LightingXmlTooLarge");
        var bytes=new byte[(int)file.Length]; file.ReadExactly(bytes); return bytes;
    }
    private static XDocument LoadXml(Stream stream) {
        using var reader=XmlReader.Create(stream,new XmlReaderSettings {DtdProcessing=DtdProcessing.Prohibit,XmlResolver=null,MaxCharactersInDocument=2*1024*1024});
        return XDocument.Load(reader);
    }
}
