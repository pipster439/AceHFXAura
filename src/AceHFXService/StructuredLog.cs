using System.Diagnostics;
using System.Text.Json;

namespace AceHFX.Service;

public sealed class StructuredLog(bool console)
{
    public void Write(string eventName, object? details = null, bool failure = false)
    {
        var line = JsonSerializer.Serialize(new { timestamp = DateTimeOffset.UtcNow, eventName, details });
        if (console) Console.WriteLine(line);
        else
        {
            // The installer creates this source; logging failure must not kill the broker.
            try { EventLog.WriteEntry("AceHFXService", line, failure ? EventLogEntryType.Warning : EventLogEntryType.Information); }
            catch (Exception e) when (e is System.ComponentModel.Win32Exception or InvalidOperationException or System.Security.SecurityException) { }
        }
    }
}
