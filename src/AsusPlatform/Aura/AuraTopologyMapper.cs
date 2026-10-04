using System.Text.Json;

namespace AceHFX.AsusPlatform.Aura;

internal static class AuraTopologyMapper
{
    internal static readonly uint[] Categories=[0,0x10000,0x20000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x120000,0x2f0000];
    internal static AuraValue<T> Value<T>(JsonElement parent,string name) {
        if(!parent.TryGetProperty(name,out var p) || p.ValueKind==JsonValueKind.Null) return new(ExecutionState.NotAttempted,default);
        var state=(ExecutionState)p.GetProperty("execution").GetInt32();
        if(!Enum.IsDefined(state)) throw new InvalidDataException("WorkerExecutionInvalid");
        var value=p.GetProperty("value");
        if(state==ExecutionState.Succeeded && value.ValueKind==JsonValueKind.Null) throw new InvalidDataException("WorkerSucceededWithoutValue");
        if(state!=ExecutionState.Succeeded && value.ValueKind!=JsonValueKind.Null) throw new InvalidDataException("WorkerFailedValuePresent");
        int? hr=p.TryGetProperty("hresult",out var h) && h.ValueKind!=JsonValueKind.Null ? h.GetInt32() : null;
        if(state==ExecutionState.Succeeded && hr is <0) throw new InvalidDataException("WorkerResultContradiction");
        return new(state,value.ValueKind==JsonValueKind.Null ? default : value.Deserialize<T>(WorkerProtocol.Json),hr);
    }
    internal static IReadOnlyList<AuraEnumeration> Map(JsonElement snapshot,Guid instance,LightingTopologyObservation topology) {
        var result=new List<AuraEnumeration>();
        foreach(var category in snapshot.GetProperty("categories").EnumerateArray()) {
            var type=category.GetProperty("category").GetUInt32();
            if(!Categories.Contains(type) || result.Any(x=>x.Category==type)) throw new InvalidDataException("WorkerCategoryInvalid");
            var enumeration=Value<bool?>(category,"enumeration"); var count=Value<int?>(category,"count");
            if(count.Value is <0 or >256 || (enumeration.Execution!=ExecutionState.Succeeded && count.Value!=null)) throw new InvalidDataException("WorkerCountInvalid");
            var devices=new List<AuraDeviceDescriptor>();
            foreach(var d in category.GetProperty("devices").EnumerateArray()) {
                int index=d.GetProperty("runtimeIndex").GetInt32();
                if(index<0 || index>=count.Value || devices.Any(x=>x.RuntimeIndex==index) || d.GetProperty("category").GetUInt32()!=type) throw new InvalidDataException("WorkerDeviceIndexInvalid");
                var item=Value<bool?>(d,"item"); var lights=new List<AuraLightDescriptor>(); var lightCount=Value<int?>(d,"lightCount");
                if(lightCount.Value is <0 or >4096) throw new InvalidDataException("WorkerLightCountInvalid");
                if(d.TryGetProperty("lights",out var array)) foreach(var l in array.EnumerateArray()) {
                    var i=l.GetProperty("index").GetInt32();
                    if(i<0 || i>=lightCount.Value || lights.Any(x=>x.Index==i)) throw new InvalidDataException("WorkerLightIndexInvalid");
                    lights.Add(new(i,Value<string>(l,"name"),Value<byte?>(l,"red"),Value<byte?>(l,"green"),Value<byte?>(l,"blue"),Value<uint?>(l,"color"),Value<uint?>(l,"locationId"),Value<bool?>(l,"item").Execution));
                }
                if(lightCount.Value!=null && lights.Count!=lightCount.Value) throw new InvalidDataException("WorkerLightArrayIncomplete");
                var width=Value<uint?>(d,"width"); var height=Value<uint?>(d,"height");
                var anomalies=new List<string>();
                if(width.Value.HasValue && height.Value.HasValue && lightCount.Value.HasValue && (ulong)width.Value.Value*height.Value.Value!=(ulong)lightCount.Value.Value) anomalies.Add("WidthHeightDoesNotMatchLightsCount");
                if(lights.Any(l=>l.Execution!=ExecutionState.Succeeded || l.Red?.Execution!=ExecutionState.Succeeded || l.Green?.Execution!=ExecutionState.Succeeded || l.Blue?.Execution!=ExecutionState.Succeeded || l.Color?.Execution!=ExecutionState.Succeeded)) anomalies.Add("SomeLightGettersFailed");
                var zones=lights.Where(x=>x.LocationId?.Value!=null).GroupBy(x=>x.LocationId!.Value).Select(g=> {
                    var matching=topology.Zones.Where(z=>z.LocationId==g.Key).ToArray();
                    return new AuraZoneDescriptor(g.First().Name?.Value??"Unknown",g.Key,g.Select(x=>x.Index).ToArray(),
                        matching.Length>0 ? MappingConfidence.Probable : MappingConfidence.Unknown,
                        matching.Length>0 ? "LocationMatchesVendorXmlDeviceIdentityNotProven" : "LocationReadNoXmlIdentityMatch");
                }).ToArray();
                int? identity=d.TryGetProperty("identityIndex",out var id) && id.GetInt32()>=0 ? id.GetInt32() : null;
                devices.Add(new($"{instance:N}:{type:X8}:{index}",index,type,identity,null,Value<uint?>(d,"type"),Value<string>(d,"name"),width,height,lightCount,lights,zones,[],anomalies,item.Execution));
            }
            if((count.Value!=null && devices.Count!=count.Value) || (count.Value==null && devices.Count!=0)) throw new InvalidDataException("WorkerDeviceArrayIncomplete");
            result.Add(new(type,enumeration.Execution,enumeration.HResult,count,devices));
        }
        // Only IUnknown identity held alive by this worker can prove duplicate observations. Names are not IDs.
        return result.Select(c=>c with {Devices=c.Devices.Select(d=>d with {AlsoSeenInCategories=d.ComIdentityIndex==null ? [] :
            result.SelectMany(x=>x.Devices).Where(x=>x.ComIdentityIndex==d.ComIdentityIndex && x.Category!=d.Category).Select(x=>x.Category).Distinct().ToArray()}).ToArray()}).ToArray();
    }
}
