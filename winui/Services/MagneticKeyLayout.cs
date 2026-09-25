namespace Aura_WinUI.Services;

// Only physical labels, layout geometry and audited Logical/XML IDs live here.
// Hardware Wire IDs remain exclusively in the C++ M605 mapping layer.
public sealed record MagneticVisualKey(ushort LogicalId, string Label, string FullName, int Row, double Units = 1, double Gap = 0);

public static class MagneticKeyLayout
{
    public static IReadOnlyList<MagneticVisualKey> Keys { get; } =
    [
        new(0x0100, "Esc", "Esc", 0, 1, 0),
        new(0x0600, "1", "1", 0), new(0x0700, "2", "2", 0),
        new(0x0101, "3", "3", 0), new(0x0102, "4", "4", 0),
        new(0x0103, "5", "5", 0), new(0x0104, "6", "6", 0),
        new(0x0105, "7", "7", 0), new(0x0106, "8", "8", 0),
        new(0x0107, "9", "9", 0), new(0x0108, "0", "0", 0),
        new(0x0109, "-", "Minus", 0), new(0x010a, "=", "Equal", 0),
        new(0x0706, "Bksp", "Backspace", 0, 2),
        new(0x0608, "Ins", "Insert", 0, 1.3, 0.45),

        new(0x0200, "Tab", "Tab", 1, 1.45),
        new(0x0601, "Q", "Q", 1), new(0x0701, "W", "W", 1),
        new(0x0201, "E", "E", 1), new(0x0202, "R", "R", 1),
        new(0x0203, "T", "T", 1), new(0x0204, "Y", "Y", 1),
        new(0x0205, "U", "U", 1), new(0x0206, "I", "I", 1),
        new(0x0207, "O", "O", 1), new(0x0208, "P", "P", 1),
        new(0x0209, "[", "Left Bracket", 1), new(0x020a, "]", "Right Bracket", 1),
        new(0x0607, "\\", "Backslash", 1, 1.55),
        new(0x0708, "Del", "Delete", 1, 1.3, 0.45),

        new(0x0300, "Caps", "Caps Lock", 2, 1.75),
        new(0x0602, "A", "A", 2), new(0x0702, "S", "S", 2),
        new(0x0301, "D", "D", 2), new(0x0302, "F", "F", 2),
        new(0x0303, "G", "G", 2), new(0x0304, "H", "H", 2),
        new(0x0305, "J", "J", 2), new(0x0306, "K", "K", 2),
        new(0x0307, "L", "L", 2), new(0x0308, ";", "Semicolon", 2),
        new(0x0309, "'", "Quote", 2), new(0x0707, "Enter", "Enter", 2, 2.25),
        new(0x060b, "PgUp", "Page Up", 2, 1.3, 0.45),

        new(0x0400, "Shift", "Left Shift", 3, 2.2),
        new(0x0703, "Z", "Z", 3), new(0x0401, "X", "X", 3),
        new(0x0501, "C", "C", 3), new(0x0402, "V", "V", 3),
        new(0x0403, "B", "B", 3), new(0x0404, "N", "N", 3),
        new(0x0405, "M", "M", 3), new(0x0406, ",", "Comma", 3),
        new(0x0407, ".", "Period", 3), new(0x0408, "/", "Slash", 3),
        new(0x040a, "Shift", "Right Shift", 3, 1.8),
        new(0x070a, "↑", "Up Arrow", 3, 1, 0.45),
        new(0x070b, "PgDn", "Page Down", 3, 1.3),

        new(0x0500, "Ctrl", "Left Ctrl", 4, 1.35),
        new(0x0604, "Win", "Left Win", 4, 1.2),
        new(0x0704, "Alt", "Left Alt", 4, 1.2),
        new(0x0503, "Space", "Space", 4, 6.15),
        new(0x0507, "Alt", "Right Alt", 4, 1.2),
        new(0x0508, "Fn", "Fn", 4, 1.2),
        new(0x050a, "Cop", "Copilot", 4, 1),
        new(0x060a, "←", "Left Arrow", 4, 1, 0.45),
        new(0x050b, "↓", "Down Arrow", 4),
        new(0x040b, "→", "Right Arrow", 4),
    ];

    public static MagneticVisualKey? Find(ushort logicalId) => Keys.FirstOrDefault(key => key.LogicalId == logicalId);
    public static bool IsValidDksActionTarget(ushort logicalId) => logicalId != 0x0508 && Find(logicalId) != null;

    // Fixed navigation columns. Flow accumulation alone drifts across rows
    // because Shift, Enter and Space have different widths.
    public static double RightmostColumn(double unit) => 15.45 * unit + 14 * 4;

    public static double LeftOf(MagneticVisualKey target, double unit)
    {
        var right = RightmostColumn(unit);
        switch (target.LogicalId)
        {
            case 0x0608: // Insert
            case 0x0708: // Delete
            case 0x060b: // Page Up
            case 0x070b: // Page Down
            case 0x040b: // Right Arrow
                return right;
            case 0x070a: // Up Arrow
            case 0x050b: // Down Arrow
                return right - unit - 4;
            case 0x060a: // Left Arrow
                return right - 2 * (unit + 4);
        }
        double cursor = 0;
        foreach (var key in Keys.Where(key => key.Row == target.Row))
        {
            cursor += key.Gap * unit;
            if (key.LogicalId == target.LogicalId) return cursor;
            cursor += key.Units * unit + 4;
        }
        throw new ArgumentOutOfRangeException(nameof(target));
    }
}
