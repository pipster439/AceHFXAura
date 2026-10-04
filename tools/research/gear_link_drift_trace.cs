// Read-only realtime metadata trace. No HID handle opens, payloads or raw ETL.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Session;

public static class GearDriftTrace
{
    static TraceEventSession session;
    static Thread reader;
    static string root;
    static long seen, matched, failures;
    static readonly Dictionary<string, string> names = new Dictionary<string, string>();
    static Dictionary<string, string> aliases = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    static readonly object outputLock = new object();
    public static string Redact(string path)
    {
        var match = Regex.Match(path, @"VID_0B05&PID_1B7E(?:&MI_[0-9A-F]{2})?", RegexOptions.IgnoreCase);
        return match.Success ? "HID#" + match.Value.ToUpperInvariant() + "#[instance-redacted]" : null;
    }
    static void Save(string file, object value, bool append = false)
    {
        var text = JsonSerializer.Serialize(value) + "\n";
        lock (outputLock) {
            if (append) File.AppendAllText(Path.Combine(root, file), text);
            else File.WriteAllText(Path.Combine(root, file), text);
        }
    }
    static object Field(TraceEvent e, string name)
    {
        foreach (var n in e.PayloadNames) if (n.Equals(name, StringComparison.OrdinalIgnoreCase)) return e.PayloadByName(n);
        return null;
    }
    static string ProcessName(int pid)
    {
        try { using (var p = Process.GetProcessById(pid)) return p.ProcessName + ".exe"; }
        catch { return "unknown"; }
    }
    static void Observe(TraceEvent e)
    {
        Interlocked.Increment(ref seen);
        try {
            string path = Convert.ToString(Field(e, "FileName"));
            string key = Convert.ToString(Field(e, "FileObject"));
            string identity = Redact(path ?? "");
            if (identity == null && path != null) aliases.TryGetValue(path, out identity);
            if (identity != null && key != "") names[key] = identity;
            if (identity == null && key != "") names.TryGetValue(key, out identity);
            if (identity == null) return;
            if (names.Count > 200000) throw new InvalidOperationException("Metadata map bound exceeded");
            Interlocked.Increment(ref matched);
            Save("process-events.jsonl", new {
                timestamp_utc = e.TimeStamp.ToUniversalTime().ToString("o"),
                process_name = ProcessName(e.ProcessID), operation = e.EventName,
                device_path = identity, provider = e.ProviderName,
                provenance = "ETW event header process; no HID payload correlation or thread-owner inference"
            }, true);
        } catch { Interlocked.Increment(ref failures); }
    }
    public static void Start(string directory)
    {
        root = directory;
        var aliasesPath = Path.Combine(root, "device-aliases.json");
        if (File.Exists(aliasesPath)) aliases = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(aliasesPath));
        session = new TraceEventSession("AuraGearDrift-" + Guid.NewGuid().ToString("N"));
        session.StopOnDispose = true;
        session.Source.Dynamic.All += Observe;
        try { session.EnableProvider("Microsoft-Windows-Kernel-File", TraceEventLevel.Informational, 0x3B0); }
        catch { session.Dispose(); throw; }
        reader = new Thread(() => {
            try { session.Source.Process(); }
            catch { Interlocked.Increment(ref failures); }
        });
        reader.IsBackground = true;
        reader.Start();
        Save("trace-status.json", new { state = "running", mode = "realtime_filtered_metadata", raw_etl_saved = false,
            caveat = "Kernel-File coverage of HID writes is not guaranteed; zero matches is inconclusive" });
    }
    public static void Stop()
    {
        if (session != null) { session.Dispose(); reader.Join(5000); }
        Save("trace-status.json", new { state = "stopped", seen_events = seen, matched_events = matched,
            decode_failures = failures, raw_etl_saved = false,
            caveat = "Handle/name or matching metadata alone is not proof of a specific 51 00 sender" });
    }
}
