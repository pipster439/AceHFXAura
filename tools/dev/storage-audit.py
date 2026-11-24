"""Read-only logical-byte inventory. Never follows reparse points or reads file contents."""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import stat


def inventory(root):
    rows, errors, links = [], [], []

    def visit(path):
        size = count = 0
        oldest = newest = None
        try:
            entries = list(os.scandir(path))
        except OSError as exc:
            errors.append({"path": str(path), "error": str(exc)})
            return size, count, oldest, newest
        for entry in entries:
            try:
                info = entry.stat(follow_symlinks=False)
                if entry.is_symlink() or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                    links.append(entry.path)
                    continue
                if entry.is_dir(follow_symlinks=False):
                    nbytes, nfiles, first, last = visit(Path(entry.path))
                else:
                    nbytes, nfiles, first, last = info.st_size, 1, info.st_mtime, info.st_mtime
                size += nbytes
                count += nfiles
                if first is not None:
                    oldest = min(oldest, first) if oldest is not None else first
                    newest = max(newest, last) if newest is not None else last
            except OSError as exc:
                errors.append({"path": entry.path, "error": str(exc)})
        rows.append({"path": str(path), "bytes": size, "files": count,
                     "oldest": stamp(oldest), "newest": stamp(newest)})
        return size, count, oldest, newest

    visit(root)
    return {"root": str(root), "directories": rows, "errors": errors, "reparse_skipped": links}


def stamp(value):
    return dt.datetime.fromtimestamp(value, dt.timezone.utc).isoformat() if value is not None else None


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="*")
    parser.add_argument("--roots-file", help="JSON array of explicitly scoped absolute paths")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    roots = args.roots + (json.loads(Path(args.roots_file).read_text(encoding='utf-8-sig')) if args.roots_file else [])
    results = []
    for root in roots:
        path = Path(root).absolute()
        if not path.is_dir():
            continue
        info = path.stat(follow_symlinks=False)
        if path.is_symlink() or getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            results.append({'root': str(path), 'directories': [], 'errors': [], 'reparse_skipped': [str(path)]})
            continue
        results.append(inventory(path))
    Path(args.output).write_text(json.dumps({"time_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                                          "unit": "logical bytes; inclusive directory totals",
                                          "inventories": results}, indent=2), encoding="utf-8")
    for result in results:
        for row in sorted(result["directories"], key=lambda row: row["bytes"], reverse=True):
            relative = Path(row["path"]).relative_to(result["root"])
            if row["bytes"] >= 100 * 1024**2 and len(relative.parts) <= 3:
                print(f"{row['bytes']/1024**3:8.3f} GiB {row['files']:8} {row['path']}")
        print(f"Errors: {len(result['errors'])}; reparse points skipped: {len(result['reparse_skipped'])}")
