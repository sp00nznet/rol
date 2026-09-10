"""Resolve MSI cabinet entries to their installed paths and place them.

The cabs name every file by its MSI File key (a GUID); the real name and target
directory only exist in the File/Component/Directory tables. This walks the
directory tree, finds the game root, and copies each staged entry into place.

Usage:  python tools/place.py <tables-dir> <cab-staging-dir> <game-dir>

where <tables-dir> holds the TSVs written by tools/dump_msi.ps1, and the
staging dir holds every cabinet extracted into one place (spanned entries
need their sibling cabs present, so extract all four together).
"""
import os, shutil, sys

S, STAGE, GAME = sys.argv[1:4] if len(sys.argv) >= 4 else sys.exit(__doc__)


def rows(name):
    with open(os.path.join(S, name), encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line:
                yield line.split('\t')


def longname(s):
    """MSI 'SHORT|Long' -> Long."""
    return s.split('|', 1)[1] if '|' in s else s


dirs = {d: (parent, default) for d, parent, default in rows('directory.tsv')}
comp = {c: d for c, d in rows('component.tsv')}


def path_of(d, seen=None):
    seen = seen or set()
    if d in seen or d not in dirs:
        return []
    seen.add(d)
    parent, default = dirs[d]
    name = longname(default)
    head = path_of(parent, seen) if parent else []
    return head + ([] if name in ('.', 'SourceDir') else [name])


# The install root is the directory whose resolved path ends at the game folder.
roots = {d: path_of(d) for d in dirs}
root_id = None
for d, p in roots.items():
    if p and p[-1].lower() == 'rise of legends':
        root_id = d
        break
if not root_id:
    sys.exit('could not find the game root directory in the MSI')
root_path = roots[root_id]
print('install root:', '\\'.join(root_path), f'(id {root_id})')


def relpath(d):
    """Path of directory d relative to the game root, or None if outside it."""
    p = roots.get(d) or []
    if len(p) < len(root_path) or p[:len(root_path)] != root_path:
        return None
    return os.path.join(*p[len(root_path):]) if len(p) > len(root_path) else ''


placed = outside = missing = 0
for key, filename, component, seq in rows('file.tsv'):
    src = os.path.join(STAGE, key)
    if not os.path.exists(src):
        missing += 1
        continue
    rel = relpath(comp.get(component, ''))
    if rel is None:
        outside += 1
        continue
    dest_dir = os.path.join(GAME, rel)
    os.makedirs(dest_dir, exist_ok=True)
    shutil.copy2(src, os.path.join(dest_dir, longname(filename)))
    placed += 1

print(f'placed {placed}, outside the game tree {outside}, not in cabs {missing}')
