using System.ComponentModel;
using System.IO.Pipes;
using System.Runtime.InteropServices;

namespace AceHFX.AsusPlatform.Ipc;

public static class SecurePipe
{
    public static string SecurityDescriptor(InteractiveUser user) =>
        "O:" + (NativeSecurity.Current().IsSystem ? "SY" : NativeSecurity.Current().Sid) + "G:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x12019b;;;" +
        new System.Security.Principal.SecurityIdentifier(user.Sid).Value + ")";

    public static NamedPipeServerStream CreateServer(string name, InteractiveUser user)
    {
        if (!NativeSecurity.ConvertStringSecurityDescriptorToSecurityDescriptor(SecurityDescriptor(user), 1, out var descriptor, out _)) throw NativeSecurity.LastError();
        try
        {
            var attributes = new NativeSecurity.SecurityAttributes { Length = Marshal.SizeOf<NativeSecurity.SecurityAttributes>(), Descriptor = descriptor };
            // DUPLEX | OVERLAPPED | FIRST_PIPE_INSTANCE, byte mode | REJECT_REMOTE_CLIENTS.
            var handle = NativeSecurity.CreateNamedPipe(@"\\.\pipe\" + name, 3 | 0x40000000 | 0x80000, 8, 1, 4096, 4096, 0, ref attributes);
            if (handle.IsInvalid) { handle.Dispose(); throw NativeSecurity.LastError(); }
            try { return new NamedPipeServerStream(PipeDirection.InOut, true, false, handle); }
            catch { handle.Dispose(); throw; }
        }
        finally { NativeSecurity.LocalFree(descriptor); }
    }
    internal static async Task<NamedPipeClientStream> ConnectAsync(string name, CancellationToken token)
    {
        while (true)
        {
            token.ThrowIfCancellationRequested();
            // Explicit rights avoid GENERIC_WRITE's CreatePipeInstance bit. Identification
            // SQOS prevents a spoofed pipe server from impersonating a privileged client.
            var handle = NativeSecurity.CreateFile(@"\\.\pipe\" + name, NativeSecurity.ClientRights, 0, IntPtr.Zero, 3,
                0x40000000 | 0x100000 | 0x10000, IntPtr.Zero);
            if (!handle.IsInvalid)
            {
                try { return new NamedPipeClientStream(PipeDirection.InOut, true, true, handle); }
                catch { handle.Dispose(); throw; }
            }
            var error = Marshal.GetLastWin32Error();
            handle.Dispose();
            if (error is not (2 or 231)) throw new Win32Exception(error);
            await Task.Delay(50, token);
        }
    }
}
