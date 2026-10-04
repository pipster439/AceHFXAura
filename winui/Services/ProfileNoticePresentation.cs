namespace Aura_WinUI.Services;

// A dismissed/expired notification stays dismissed across polling renders.
// State refresh and activation events deliberately have separate identities.
public sealed class ProfileNoticePresentation
{
    private long? _presentedSequence;
    public bool Accept(long sequence)
    {
        if (_presentedSequence == sequence) return false;
        _presentedSequence = sequence;
        return true;
    }
    // A queued success timeout cannot dismiss a newer blocking notification.
    public static bool CanExpire(long scheduledSequence, long currentSequence, ProfileNoticeKind kind) =>
        scheduledSequence == currentSequence && kind == ProfileNoticeKind.Success;
}
