using AceHFX.AsusPlatform;

namespace Aura_WinUI.Platform;

// Presentation depends on platform evidence, never vendor identifiers or interfaces.
internal static class AsusPlatformDiagnostics
{
    public static string Describe(AceHFX.AsusPlatform.Cooling.CoolingRuntimeSnapshot state)
    {
        var fans = state.Channels.Select(fan =>
            $"{fan.Name.Value ?? "未知风扇"}：RPM {fan.CurrentRpm.Value?.ToString() ?? "未知"}；" +
            $"温度源 ID {fan.SourceIndex.Value?.ToString() ?? "未知"}；RPM 模式 {fan.RpmMode.Value?.ToString() ?? "未知"}；" +
            $"曲线点数 {fan.CurrentCurve?.PointCount.Value?.ToString() ?? "未知"}");
        return "散热只读诊断\n" +
            $"FanWorker：{state.Capabilities.WorkerReachable}；COM：{state.Activation}；枚举：{state.Capabilities.Enumeration}\n" +
            $"控制通道：{state.Capabilities.ControlChannelCount?.ToString() ?? "未知"}；RPM 传感器：{state.Capabilities.RpmSensorCount?.ToString() ?? "未知"}；" +
            $"温度源：{state.Capabilities.TemperatureSourceCount?.ToString() ?? "未知"}\n" +
            string.Join("\n", fans) + "\n只读模式；写入与恢复安全合同尚未验证。";
    }
    public static string Describe(AceHFX.AsusPlatform.Aura.AuraRuntimeState state)
    {
        string Execution(ExecutionState value) => value switch { ExecutionState.Succeeded=>"成功", ExecutionState.Failed=>"失败",
            ExecutionState.PermissionDenied=>"访问被拒绝", ExecutionState.TimedOut=>"超时", ExecutionState.Unavailable=>"不可用", _=>"未尝试" };
        var devices=state.Categories.SelectMany(x=>x.Devices).GroupBy(x=>x.ComIdentityIndex?.ToString()??x.RuntimeId).Select(x=>x.First());
        var outcome=state.Error switch {PlatformError.None=>"成功", PlatformError.VendorTimeout=>"组件查询超时",PlatformError.WorkerCrashed=>"工作进程已退出",PlatformError.MalformedRequest=>"工作进程响应无效",_=>"查询未完成"};
        var dynamicSetting=state.DynamicLighting.Enabled.Value switch {true=>"开启（用户设置）",false=>"关闭（用户设置）",_=>"未知"};
        return $"Aura 只读查询：{outcome}\nAura SDK：{Describe(state.Capabilities.Installed)}；Lighting Service：{Describe(state.Capabilities.ServiceRunning)}\n"+
            $"Aura 工作进程：{Describe(state.Capabilities.WorkerReachable)}；COM：{Execution(state.Activation)}\n"+
            $"设备枚举：{Execution(state.Enumeration)}；分类返回数：{state.DeviceCount?.ToString()??"未知"}；去重数：{state.UniqueComIdentityCount?.ToString()??"未知"}\n"+
            string.Join("\n",devices.Select(x=>$"设备：{x.Name?.Value??"未知"}；灯数量：{x.LightCount?.Value?.ToString()??"未知"}"))+"\n"+
            $"Windows Dynamic Lighting：{dynamicSetting}；LampArray 接口：{(state.DynamicLighting.Execution==ExecutionState.Succeeded?state.DynamicLighting.LampArrays.Count.ToString():"未知")}\n"+
            "当前光效控制者：未知；Aura 控制权：尚未验证，需单独批准\n实时输出：尚未验证；本次仅执行只读查询。";
    }
    public static string Describe(AsusPlatformStatus status)
    {
        var capabilities = status.Capabilities;
        return $"AceHFXService：安装 {Describe(status.Broker.Installed)}，运行 {Describe(status.Broker.Running)}\n" +
            $"IPC：{(status.IpcReachable ? "已认证连接" : "未连接")}；散热特权代理：{(status.IpcReachable ? "可连接" : "不可用")}\n" +
            $"Aura 组件：{Describe(capabilities?.Aura.Backend.StackDetected)}；散热组件：{Describe(capabilities?.Cooling.Backend.StackDetected)}\n" +
            "当前仅检测组件状态；硬件控制能力尚未验证。";
    }
    private static string Describe(Evidence? evidence) => evidence?.State switch
    {
        CapabilityState.Available => "已检测",
        CapabilityState.Unavailable => "未检测到",
        CapabilityState.InstalledButStopped => "未运行",
        CapabilityState.PermissionDenied => "访问被拒绝",
        CapabilityState.VersionUnsupported => "版本不兼容",
        _ => "未知"
    };
}
