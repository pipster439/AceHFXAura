using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text.Json;
using AceHFX.FanWorker;

namespace FanTypeLibValidator.Tests;

[TestClass]
public sealed class ValidatorTests
{
    [TestMethod]
    [DataRow(0x0000000000000007UL, 0x00000007u)]
    [DataRow(0x1234567800000007UL, 0x00000007u)]
    [DataRow(0x0000000080000007UL, 0x80000007u)]
    [DataRow(0x1234567880000007UL, 0x80000007u)]
    [DataRow(0x00000000FFFFFFFFUL, 0xFFFFFFFFu)]
    [DataRow(0xFFFFFFFFFFFFFFFFUL, 0xFFFFFFFFu)]
    public void HRefTypePreservesLow32(ulong storage, uint expected)
    {
        Assert.AreEqual(expected, unchecked((uint)FanLibraryContract.HRefTypeCarrier(storage)));
        Assert.AreEqual(FanLibraryContract.HRefTypeCarrier(storage), FanLibraryContract.HRefTypeCarrier(unchecked((uint)storage)));
        Assert.AreEqual(FanLibraryContract.HRefTypeCarrier(storage), FanLibraryContract.HRefTypeCarrier(unchecked((ulong)(long)(int)(uint)storage)));
        var union = IntPtr.Size == 8 ? new IntPtr(unchecked((long)storage)) : new IntPtr(unchecked((int)(uint)storage));
        Assert.AreEqual(expected, unchecked((uint)FanLibraryContract.ExtractHRefType(new TYPEDESC { vt = 29, lpValue = union })));
        Console.WriteLine("HREF_RESULT:" + JsonSerializer.Serialize(new { storage = $"0x{storage:X16}", expected = $"0x{expected:X8}",
            actual = $"0x{unchecked((uint)FanLibraryContract.HRefTypeCarrier(storage)):X8}", carrier = FanLibraryContract.HRefTypeCarrier(storage), nativeWidth = IntPtr.Size * 8,
            x86Equivalent = FanLibraryContract.HRefTypeCarrier(unchecked((uint)storage)) }));
        if ((expected & 0x80000000) != 0) Assert.IsLessThan(0, FanLibraryContract.HRefTypeCarrier(storage));
    }
    [TestMethod]
    public void HRefTypeRejectsInactiveUnion() => Assert.ThrowsExactly<InvalidDataException>(() =>
        FanLibraryContract.ExtractHRefType(new TYPEDESC { vt = 26, lpValue = new IntPtr(1) }));

    [TestMethod]
    [DataRow(0UL)]
    [DataRow(0x1234567800000000UL)]
    public void CapturedFixtureMatchesAllSixReducedInterfaces(ulong upper)
    {
        using var fixture = new TypeLibFixture { UpperBits = upper };
        FanLibraryContract.VerifyLibrary(fixture.Create());
    }
    [TestMethod]
    [DataRow("Library", "LibraryIdentity")]
    [DataRow("MissingInterface", "InterfaceLookup")]
    [DataRow("InterfaceIdentity", "InterfaceLookup")]
    [DataRow("Dispid", "PropertyLookup")]
    [DataRow("Setter", "PropertyLookup")]
    [DataRow("ParameterCount", "PropertyLookup")]
    [DataRow("ReturnType", "ReturnType")]
    [DataRow("ReferenceIid", "UserDefinedReference")]
    [DataRow("NestedType", "UserDefinedReference")]
    [DataRow("NullNested", "ReturnType")]
    [DataRow("ParameterType", "ParameterType")]
    [DataRow("ParameterFlags", "ParameterType")]
    [DataRow("NullParameters", "ParameterType")]
    public void AbiMismatchFailsClosedWithStage(string fault, string stage)
    {
        using var fixture = new TypeLibFixture { Fault = fault };
        var error = Assert.ThrowsExactly<FanValidationException>(() => FanLibraryContract.VerifyLibrary(fixture.Create()));
        var diagnostic = error.Diagnostic;
        Assert.AreEqual(stage, diagnostic.Stage);
        Assert.IsNotNull(diagnostic.ExceptionType); Assert.IsNotNull(diagnostic.ExceptionHresult);
        var json = FanLibraryContract.DescribeFailure(error);
        Console.WriteLine("DIAGNOSTIC_RESULT:" + json);
        Assert.DoesNotContain("fixture", json); Assert.DoesNotContain("lpValue", json);
        if (stage == "UserDefinedReference" && fault == "ReferenceIid") Assert.AreEqual("0x80000007", diagnostic.HRefTypeBits);
        if (stage is "ReturnType" or "UserDefinedReference" or "ParameterType")
        { Assert.IsNotNull(diagnostic.ExpectedVartype); if (fault == "NullParameters") Assert.IsNull(diagnostic.ObservedVartype); else Assert.IsNotNull(diagnostic.ObservedVartype); Assert.IsNotNull(diagnostic.Property); Assert.IsNotNull(diagnostic.InterfaceIid); }
    }
    [TestMethod]
    public void DiagnosticsDoNotExposeExceptionMessage()
    {
        var error = new COMException("C:\\secret\\file 0x1234567812345678", unchecked((int)0x80070005));
        var json = FanLibraryContract.DescribeFailure(error);
        Assert.DoesNotContain("secret", json); Assert.DoesNotContain("12345678", json);
        Assert.Contains("COMException", json); Assert.Contains("LibraryIdentity", json);
    }
    [TestMethod]
    [TestCategory("OfflineMetadata")]
    public void InstalledTypeLibMetadataOnly()
    {
        if (Environment.GetEnvironmentVariable("FAN_TYPELIB_OFFLINE") != "1") Assert.Inconclusive("Offline metadata test not requested.");
        var path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "ASUS", "AsusFanControlService", "2.05.06", "AsusFanControlService.exe");
        if (!File.Exists(path)) Assert.Inconclusive("Installed metadata file unavailable; no elevation attempted.");
        FanLibraryContract.VerifyFile(path);
        Console.WriteLine(JsonSerializer.Serialize(new { operation = "LoadTypeLibEx(REGKIND_NONE)", activated = false, status = "Succeeded", pointerWidth = IntPtr.Size * 8 }));
    }
}
