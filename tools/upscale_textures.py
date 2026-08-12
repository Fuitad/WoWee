#!/usr/bin/env python3
"""Offline texture upscale pipeline for wowee.

Selects BLP textures from the extracted Data tree, decodes them with
blp_convert, AI-upscales the PNGs, and writes them back as PNG sidecars
(``foo.png`` next to ``foo.blp``).  The client's AssetManager checks for a
PNG sidecar before decoding a BLP, so overrides take effect on next launch
with no repacking - delete the sidecar to revert.

Upscaler backends (auto-detected, best first):
  1. realesrgan-ncnn-vulkan  - AI upscale (https://github.com/xinntao/Real-ESRGAN/releases,
                               unzip anywhere on PATH; handles RGBA)
  2. ImageMagick convert     - Lanczos resize + light sharpen (fallback)

Every generated sidecar is recorded in ``<data-dir>/upscale_manifest.txt`` so
``--clean`` can remove exactly what this tool created and nothing else.

Examples:
  # Preview which foliage textures would be processed
  python3 tools/upscale_textures.py --dry-run

  # Upscale all tree/foliage textures 4x (capped at 2048px)
  python3 tools/upscale_textures.py

  # Everything under a path fragment, e.g. Durotar doodads
  python3 tools/upscale_textures.py --include durotar --include barrens

  # Remove all sidecars this tool generated
  python3 tools/upscale_textures.py --clean
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FOLIAGE_TOKENS = [
    "tree", "bush", "plant", "leaf", "leaves", "shrub", "palm", "canopy",
    "branch", "foliage", "cactus", "vine", "fern", "flower", "grass",
    "mushroom", "kelp", "moss",
]

REPO_ROOT = Path(__file__).resolve().parent.parent


def find_blp_convert() -> Path:
    for cand in (REPO_ROOT / "build/bin/blp_convert",
                 REPO_ROOT / "build/blp_convert"):
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand
    sys.exit("blp_convert not found - build it first: cmake --build build --target blp_convert")


def detect_upscaler():
    exe = os.environ.get("WOWEE_UPSCALER") or shutil.which("realesrgan-ncnn-vulkan")
    if exe:
        return ("realesrgan", exe)
    for name in ("magick", "convert"):
        exe = shutil.which(name)
        if exe:
            return ("imagemagick", exe)
    sys.exit("No upscaler found. Install realesrgan-ncnn-vulkan (preferred) or ImageMagick.")


FALSE_POSITIVES = ["infernal"]  # substrings that contain a token but aren't foliage

def collect_blps(data_dir: Path, tokens, limit, preset_roots=False):
    """BLP files whose relative path contains any filter token (case-insensitive).

    With preset_roots (the default foliage preset), only world/doodad texture
    roots are searched so tokens can't hit creature skins or UI art.
    """
    out = []
    seen = set()
    for blp in sorted(data_dir.rglob("*.blp")):
        rel = str(blp.relative_to(data_dir)).lower().replace("\\", "/")
        if rel in seen:  # symlinked dirs can yield the same file twice
            continue
        seen.add(rel)
        if preset_roots and not (rel.startswith("world/") or rel.startswith("environment/")
                                 or "/overlay/world/" in rel):
            continue
        if tokens:
            probe = rel
            for fp in FALSE_POSITIVES:
                probe = probe.replace(fp, "")
            if not any(t in probe for t in tokens):
                continue
        out.append(blp)
        if limit and len(out) >= limit:
            break
    return out


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        raise RuntimeError(f"{' '.join(map(str, cmd))}\n{r.stderr.strip()}")
    return r


def upscale_batch(backend, exe, in_dir: Path, out_dir: Path, scale: int):
    if backend == "realesrgan":
        # Directory mode: one model load for the whole batch.
        run([exe, "-i", str(in_dir), "-o", str(out_dir),
             "-s", str(scale), "-n", "realesrgan-x4plus", "-f", "png"])
    else:
        for png in in_dir.glob("*.png"):
            run([exe, str(png), "-filter", "Lanczos", "-resize", f"{scale * 100}%",
                 "-unsharp", "0x1+0.5+0.02", str(out_dir / png.name)])


def cap_dimensions(png: Path, max_dim: int):
    """Downsize a PNG in place if either dimension exceeds max_dim (needs ImageMagick)."""
    im = shutil.which("magick") or shutil.which("convert")
    if im:
        run([im, str(png), "-resize", f"{max_dim}x{max_dim}>", str(png)])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data-dir", type=Path, default=REPO_ROOT / "build/bin/Data",
                    help="extracted Data root (default: build/bin/Data)")
    ap.add_argument("--include", action="append", default=[],
                    help="path substring filter (repeatable); replaces the foliage preset")
    ap.add_argument("--all", action="store_true", help="no filter - process every BLP")
    ap.add_argument("--scale", type=int, default=4, choices=(2, 3, 4))
    ap.add_argument("--max-dim", type=int, default=2048,
                    help="cap output dimensions (default 2048)")
    ap.add_argument("--limit", type=int, default=0, help="process at most N textures")
    ap.add_argument("--batch-size", type=int, default=64,
                    help="textures per upscaler invocation")
    ap.add_argument("--force", action="store_true", help="regenerate existing sidecars")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--clean", action="store_true",
                    help="delete all sidecars listed in the upscale manifest and exit")
    args = ap.parse_args()

    data_dir = args.data_dir.resolve()
    if not data_dir.is_dir():
        sys.exit(f"Data dir not found: {data_dir}")
    manifest = data_dir / "upscale_manifest.txt"

    if args.clean:
        if not manifest.is_file():
            print("Nothing to clean (no upscale manifest).")
            return
        removed = 0
        for line in manifest.read_text().splitlines():
            p = data_dir / line.strip()
            if p.is_file():
                p.unlink()
                removed += 1
        manifest.unlink()
        print(f"Removed {removed} generated sidecar(s).")
        return

    tokens = [] if args.all else [t.lower() for t in (args.include or FOLIAGE_TOKENS)]
    preset_roots = not args.all and not args.include  # foliage preset only
    blps = collect_blps(data_dir, tokens, args.limit, preset_roots)

    already = set()
    if manifest.is_file():
        already = {line.strip() for line in manifest.read_text().splitlines()}

    todo = []
    for blp in blps:
        sidecar = blp.with_suffix(".png")
        rel_sidecar = str(sidecar.relative_to(data_dir))
        if sidecar.exists():
            if rel_sidecar not in already:
                # Hand-made override - never touch it.
                continue
            if not args.force:
                continue
        todo.append(blp)

    print(f"{len(blps)} matching BLP(s), {len(todo)} to process"
          + (f" (filter: {', '.join(tokens)})" if tokens else " (no filter)"))
    if args.dry_run:
        for blp in todo:
            print("  ", blp.relative_to(data_dir))
        return
    if not todo:
        return

    blp_convert = find_blp_convert()
    backend, exe = detect_upscaler()
    print(f"Upscaler: {backend} ({exe}), scale {args.scale}x, max dim {args.max_dim}")
    if backend == "imagemagick":
        print("NOTE: install realesrgan-ncnn-vulkan for far better results.")

    done = failed = 0
    for start in range(0, len(todo), args.batch_size):
        new_entries = []
        batch = todo[start:start + args.batch_size]
        with tempfile.TemporaryDirectory(prefix="wowee_upscale_") as tmp:
            in_dir = Path(tmp) / "in"
            out_dir = Path(tmp) / "out"
            in_dir.mkdir()
            out_dir.mkdir()

            # Stage: copy BLPs to temp, decode to PNG there (blp_convert
            # writes next to its input - decoding in place would activate a
            # low-res override).
            names = {}
            for i, blp in enumerate(batch):
                staged = in_dir / f"{i:04d}.blp"
                shutil.copyfile(blp, staged)
                try:
                    run([blp_convert, "--to-png", str(staged)])
                    staged.unlink()
                    names[f"{i:04d}.png"] = blp
                except RuntimeError as e:
                    print(f"  DECODE FAIL {blp.relative_to(data_dir)}: {e}", file=sys.stderr)
                    failed += 1

            if not names:
                continue
            try:
                upscale_batch(backend, exe, in_dir, out_dir, args.scale)
            except RuntimeError as e:
                print(f"  UPSCALE FAIL (batch of {len(names)}): {e}", file=sys.stderr)
                failed += len(names)
                continue

            for name, blp in names.items():
                result = out_dir / name
                if not result.is_file():
                    print(f"  MISSING OUTPUT {blp.relative_to(data_dir)}", file=sys.stderr)
                    failed += 1
                    continue
                cap_dimensions(result, args.max_dim)
                sidecar = blp.with_suffix(".png")
                shutil.copyfile(result, sidecar)
                new_entries.append(str(sidecar.relative_to(data_dir)))
                done += 1
        # Flush manifest per batch so an interrupted run stays revertible
        if new_entries:
            with manifest.open("a") as f:
                for e in new_entries:
                    if e not in already:
                        f.write(e + "\n")
                        already.add(e)
        print(f"  {min(start + args.batch_size, len(todo))}/{len(todo)} …", flush=True)

    print(f"Done: {done} sidecar(s) written, {failed} failed. "
          f"Restart wowee to see them; revert with --clean.")


if __name__ == "__main__":
    main()
