"""Verify a staged or freshly extracted WinUI ZIP without loading its code."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package_winui import verify_package, product_version

MAX_ENTRIES = 2048
MAX_EXPANDED = 1024 * 1024 * 1024
FORBIDDEN_PARTS = {'audit_artifacts', 'audit_repros', 'node_modules', '.git', 'obj', 'snapshots', 'journals', 'owner-data', 'diagnostics'}
FORBIDDEN_NAMES = {'config.json', 'studio-llm-settings.json', 'client-settings.json', 'recent.json', 'portable.marker', 'aackbhal_x64.dll'}
FORBIDDEN_EXTENSIONS = {'.pdb', '.ilk', '.obj', '.lib', '.exp', '.ipdb', '.iobj', '.dmp', '.pcap', '.pcapng', '.auraeffect', '.trx', '.log', '.patch', '.zip', '.cs', '.cpp'}


def safe_name(name):
    if not name or '\\' in name or ':' in name or name.startswith('/') or any(ord(c) < 32 for c in name) or any(p in ('', '.', '..') or p.endswith((' ', '.')) for p in name.split('/')):
        raise RuntimeError('Unsafe archive path')
    parts = PurePosixPath(name).parts
    if any(p.casefold() in FORBIDDEN_PARTS or p.casefold().startswith(('owner-data-', 'winui-stage-')) for p in parts):
        raise RuntimeError('Forbidden package directory')
    if parts[-1].casefold() in FORBIDDEN_NAMES or PurePosixPath(name).suffix.casefold() in FORBIDDEN_EXTENSIONS:
        raise RuntimeError('Forbidden package file')
    return name.casefold()


def verify_archive(path):
    with zipfile.ZipFile(path) as archive:
        if not 1 <= len(archive.infolist()) <= MAX_ENTRIES:
            raise RuntimeError('Archive entry count exceeds bound')
        seen = set(); expanded = 0
        for entry in archive.infolist():
            # ZipInfo normalizes Windows backslashes/NUL when reading; validate
            # the original central-directory name before using normalized paths.
            key = safe_name(entry.orig_filename)
            if key in seen:
                raise RuntimeError('Duplicate case-insensitive archive path')
            seen.add(key)
            mode = (entry.external_attr >> 16) & 0xf000
            if mode not in (0, stat.S_IFREG) or entry.external_attr & 0x410:
                raise RuntimeError('Archive link/reparse/directory entry rejected')
            if entry.flag_bits & 1:
                raise RuntimeError('Encrypted archive rejected')
            expanded += entry.file_size
            if expanded > MAX_EXPANDED or entry.file_size > MAX_EXPANDED // 2:
                raise RuntimeError('Archive decompressed size exceeds bound')
        if archive.testzip() is not None:
            raise RuntimeError('Archive CRC failure')
        return {'entries': len(seen), 'expanded_bytes': expanded, 'crc': 'PASS'}


def verify_directory(directory, expected_version, private_paths=()):
    directory = Path(directory).resolve()
    manifest = verify_package(directory)
    if manifest['version'] != expected_version:
        raise RuntimeError('Wrong runtime product version')
    actual = {}
    needles = []
    for private in private_paths:
        for spelling in {str(private), str(private).replace('\\', '/'), str(private).replace('/', '\\')}:
            needles.extend([spelling.encode('utf-8').lower(), spelling.encode('utf-16-le').lower()])
    for file in directory.rglob('*'):
        info = file.lstat()
        if file.is_symlink() or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise RuntimeError('Package link/reparse rejected')
        if not file.is_file():
            continue
        relative = file.relative_to(directory).as_posix(); safe_name(relative)
        data = file.read_bytes(); lower = data.lower()
        if any(needle and needle in lower for needle in needles):
            raise RuntimeError('Local developer path leaked: ' + relative)
        if re.search(rb'(?i)sk-[a-z0-9_-]{20,}|Bearer[ ]+[a-z0-9_-]{20,}', data):
            raise RuntimeError('Credential-looking payload rejected: ' + relative)
        if relative != 'checksums.json':
            key = relative.casefold()
            if key in actual:
                raise RuntimeError('Duplicate package path')
            actual[key] = (relative, hashlib.sha256(data).hexdigest())
    expected = json.loads((directory / 'checksums.json').read_text(encoding='utf-8'))
    if len(expected) != len({k.casefold() for k in expected}) or {k.casefold() for k in expected} != set(actual):
        raise RuntimeError('Checksums inventory mismatch')
    for name, digest in expected.items():
        safe_name(name)
        if actual[name.casefold()][1] != digest:
            raise RuntimeError('Package checksum mismatch: ' + name)
    versions = {name: product_version(directory / name, exact=True) for name in (
        'Aura.exe', 'Aura.dll', 'AsusPlatform.dll', 'runtime-payload/aura_daemon.exe', 'runtime-payload/aura_web_ui.exe')}
    return {'result': 'PASS', 'version': manifest['version'], 'build_id': manifest['build_id'],
            'checksums': len(expected), 'ProductVersion': versions,
            'links_reparse_forbidden_files_private_paths_credentials': 'PASS'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--zip', type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--private-path', action='append', default=[])
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = verify_directory(args.directory, args.version, args.private_path)
    if args.zip:
        result['archive'] = verify_archive(args.zip)
        result['zip_bytes'] = args.zip.stat().st_size
        with args.zip.open('rb') as stream:
            result['zip_sha256'] = hashlib.file_digest(stream, 'sha256').hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
