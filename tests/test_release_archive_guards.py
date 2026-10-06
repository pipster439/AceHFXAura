"""Release ZIP rejects malicious containers before extraction or runtime load."""
import importlib.util
from pathlib import Path
import stat
import tempfile
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location('release_guards', Path(__file__).resolve().parents[1] / 'tools/ci/verify_release_package.py')
guards = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(guards)


class ReleaseArchiveGuards(unittest.TestCase):
    def check(self, entries):
        with tempfile.TemporaryDirectory() as root:
            archive = Path(root) / 'fixture.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                for entry in entries:
                    z.writestr(entry, b'fixture')
            # ZipInfo normalizes backslashes on Windows when writing. Encode a
            # genuinely malicious local/central filename instead of that safe ZIP.
            for entry in entries:
                if isinstance(entry, str) and '\\' in entry:
                    archive.write_bytes(archive.read_bytes().replace(entry.replace('\\', '/').encode(), entry.encode()))
            return guards.verify_archive(archive)

    def test_regular_archive(self):
        self.assertEqual(self.check(['Aura.exe', 'runtime-payload/web/index.html'])['crc'], 'PASS')

    def test_traversal_and_absolute_paths(self):
        for path in ('../Aura.exe', '/Aura.exe', 'C:/Aura.exe', 'a\\Aura.exe', 'a/./Aura.exe'):
            with self.subTest(path=path), self.assertRaisesRegex(RuntimeError, 'Unsafe'):
                self.check([path])

    def test_case_insensitive_duplicate(self):
        with self.assertRaisesRegex(RuntimeError, 'Duplicate'):
            self.check(['Aura.exe', 'aura.EXE'])

    def test_unix_symlink_and_windows_reparse(self):
        for attributes in ((stat.S_IFLNK | 0o777) << 16, 0x400):
            item = zipfile.ZipInfo('Aura.exe'); item.external_attr = attributes
            with self.subTest(attributes=attributes), self.assertRaisesRegex(RuntimeError, 'link/reparse'):
                self.check([item])

    def test_private_and_debug_payload(self):
        for path in ('audit_artifacts/report.md', 'Private.PDB', 'config.json', 'capture.pcap', 'project.auraeffect', 'owner-data-abc/config.example.json'):
            with self.subTest(path=path), self.assertRaisesRegex(RuntimeError, 'Forbidden'):
                self.check([path])

    def test_entry_count_and_expanded_size(self):
        from unittest.mock import patch
        with patch.object(guards, 'MAX_ENTRIES', 1), self.assertRaisesRegex(RuntimeError, 'entry count'):
            self.check(['a.txt', 'b.txt'])
        with patch.object(guards, 'MAX_EXPANDED', 4), self.assertRaisesRegex(RuntimeError, 'size'):
            self.check(['a.txt'])

    def test_credential_boundaries_and_native_managed_encodings(self):
        self.assertFalse(guards.contains_credential_payload(b'Neue-Haas-Grotesk-Text-Pro-UltraThin'))
        synthetic = 'sk-syntheticfixture000000000000'
        for encoding in ('ascii', 'utf-16-le'):
            self.assertTrue(guards.contains_credential_payload(('"' + synthetic + '"').encode(encoding)))
            self.assertTrue(guards.contains_credential_payload(('Bearer ' + synthetic).encode(encoding)))
