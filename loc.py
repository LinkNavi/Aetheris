#!/usr/bin/env python3
import os
import sys
from collections import defaultdict

CODE_EXTENSIONS = {
    '.py', '.js', '.ts', '.jsx', '.tsx', '.c', '.cpp', '.cc', '.h', '.hpp',
    '.java', '.cs', '.go', '.rs', '.rb', '.php', '.swift', '.kt', '.scala',
    '.sh', '.bash', '.zsh', '.fish', '.ps1', '.lua', '.r', '.m', '.f90',
    '.html', '.css', '.scss', '.sass', '.less', '.xml', '.json', '.yaml',
    '.yml', '.toml', '.sql', '.asm', '.s', '.dart', '.ex', '.exs', '.erl',
    '.hs', '.ml', '.clj', '.lisp', '.vim','.frag','.vert', '.tf', '.vue', '.svelte'
}

def count_lines(path):
    try:
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            return sum(1 for _ in f)
    except (OSError, PermissionError):
        return 0

def scan_folder(folder):
    ext_counts = defaultdict(int)
    total = 0
    for root, _, files in os.walk(folder):
        for fname in files:
            ext = os.path.splitext(fname)[1].lower()
            if ext in CODE_EXTENSIONS:
                lines = count_lines(os.path.join(root, fname))
                ext_counts[ext] += lines
                total += lines
    return ext_counts, total

def main():
    folders = sys.argv[1:]
    if not folders:
        print("Usage: loc.py <folder1> [folder2] ...")
        sys.exit(1)

    grand_total = 0
    all_ext = defaultdict(int)

    for folder in folders:
        if not os.path.isdir(folder):
            print(f"Skipping (not a folder): {folder}")
            continue
        ext_counts, total = scan_folder(folder)
        print(f"\n{folder}  ({total:,} lines)")
        for ext, count in sorted(ext_counts.items(), key=lambda x: -x[1]):
            print(f"  {ext:<12} {count:>10,}")
        grand_total += total
        for k, v in ext_counts.items():
            all_ext[k] += v

    if len(folders) > 1:
        print(f"\nTOTAL: {grand_total:,} lines")
        for ext, count in sorted(all_ext.items(), key=lambda x: -x[1]):
            print(f"  {ext:<12} {count:>10,}")

if __name__ == '__main__':
    main()
