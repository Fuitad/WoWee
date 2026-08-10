#!/usr/bin/env python3
"""Audit an asset-pack overlay and disable the parts of it that break the game.

A third-party model pack is not a patch this client can take whole. It replaces
files that already worked, and it does so with art built for a later client:
models whose textures it forgot to ship, and character models drawn from an art
set this client has no compositor for. Taking such a pack whole is how you get a
naked player with segmented limbs and white statues in Stormwind.

The rule this applies is: a pack may ADD freely, and may REPLACE only where the
replacement is complete. Everything it fails on is disabled by removing it from
the overlay manifest, which is the whole of what makes a file reachable — the
files stay on disk so a decision can be reversed by re-running with a different
rule rather than by extracting again.

Three things get disabled, each for a reason measured rather than assumed:

  1. Character models, and everything tied to them. The HD player models carry
     a different geoset set (they have no 1, 701 or 1501 — no default hair, no
     ears variant 1, no "wearing nothing" cloak) and their skins carry three
     times the vertices. The client picks equipment geosets by number and
     composites a 512x512 body texture; neither survives the swap. The .anim
     files go with them: keyframes for an HD skeleton against the base model is
     the "weird arms and legs" half of the same fault.

  2. Models missing a real texture. A missing reflection or environment map only
     costs shine and is kept. A missing base texture renders the model white,
     which is what the Stormwind lion statue and the funerary banner became.

  3. CharSections.dbc, which is what points the compositor at _HD art. The
     client says so itself: "is 512x256 but its region on this 512x512 body is
     256x128 — mismatched art sets".

CreatureDisplayInfo is not dropped but rewritten: the pack re-points 13953
display ids, and 13648 of them land on HD humanoid models — the same ones from
(1). Those are reverted to what the base game had, one integer field per row, so
the string block stays exactly as it was. The 305 that land on creature models
are kept, and they are the part of this pack that works.

    python3 tools/asset_pack_curate.py Data/expansions/wotlk [--apply]

Without --apply it reports and changes nothing.
"""

import json
import os
import re
import struct
import sys

# Textures a model lays over itself rather than draws itself with. Missing one
# costs an effect, not the surface. "glow" earns its place the hard way: an HD
# character model carries deathKnightEyeGlow.blp in slot 0 — the first slot is
# the body's on most models and an eye effect on these — so treating slot 0 as
# decisive without this disabled both gnome female models and left a display row
# pointing at nothing.
REFLECTION_WORDS = ("reflect", "envmap", "fresnel", "glass", "caustic",
                    "spec", "smooth", "orbreflect", "glow", "eyeglow")


def load_dbc(path):
    """id -> (fields, string-resolver). Enough to read and to patch in place."""
    data = open(path, "rb").read()
    rec, fld, rsize, _ssize = struct.unpack("<IIII", data[4:20])
    body = data[20:20 + rec * rsize]
    strings = data[20 + rec * rsize:]

    def resolve(off):
        if off == 0 or off >= len(strings):
            return ""
        return strings[off:strings.find(b"\0", off)].decode("utf-8", "replace")

    rows = {}
    for i in range(rec):
        vals = struct.unpack("<%dI" % fld, body[i * rsize:i * rsize + fld * 4])
        rows[vals[0]] = (vals, resolve, i)
    return rows, rec, fld, rsize


def m2_textures(path):
    """(type, filename) per texture, whatever the type.

    Only type 0 is *supposed* to carry a name — every other type is art the
    client supplies at runtime. Reading the name for type 0 alone was therefore
    reasonable and wrong: a model can carry a name in a runtime slot anyway, and
    one that does is telling you it was built for that art rather than for
    whatever it is handed.
    """
    try:
        data = open(path, "rb").read()
    except OSError:
        return []
    if data[:4] != b"MD20":
        return []
    n_tex, o_tex = struct.unpack("<II", data[0x50:0x58])
    out = []
    for i in range(n_tex):
        rec = data[o_tex + i * 16:o_tex + i * 16 + 16]
        if len(rec) < 16:
            break
        typ, _flags, length, offset = struct.unpack("<IIII", rec)
        name = ""
        if length > 1:
            name = data[offset:offset + length].split(b"\0")[0].decode("ascii", "ignore")
        out.append((typ, name))
    return out


def is_reflection(name):
    base = name.lower().rsplit("\\", 1)[-1]
    return any(word in base for word in REFLECTION_WORDS)


def find_overlays(data_root="Data"):
    """Every expansion under Data/ that carries its own asset manifest."""
    found = []
    expansions = os.path.join(data_root, "expansions")
    if not os.path.isdir(expansions):
        return found
    for name in sorted(os.listdir(expansions)):
        path = os.path.join(expansions, name)
        if os.path.exists(os.path.join(path, "manifest.json")):
            found.append(path)
    return found


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    apply_changes = "--apply" in sys.argv
    keep_hd = "--keep-hd-characters" in sys.argv

    # No argument audits whatever overlays this install has, and reports
    # nothing to do when it has none. A tool that cannot be run without being
    # told where to look is a tool nobody runs.
    if not args:
        overlays = find_overlays()
        if not overlays:
            print("no asset overlay in Data/expansions — nothing to audit")
            return 0
        rc = 0
        for path in overlays:
            rc |= curate(path, apply_changes, keep_hd)
        return rc
    return curate(args[0].rstrip("/"), apply_changes, keep_hd)


def curate(overlay_root, apply_changes, keep_hd=False):

    manifest_path = os.path.join(overlay_root, "manifest.json")
    manifest = json.load(open(manifest_path))
    entries = manifest["entries"]
    files_root = os.path.join(overlay_root, manifest["basePath"])

    # The base tree this overlay sits on top of: <data>/manifest.json, two
    # directories up from an expansion overlay.
    base_root = os.path.dirname(os.path.dirname(overlay_root))
    base = json.load(open(os.path.join(base_root, "manifest.json")))["entries"]

    def resolves(tex):
        key = tex.replace("/", "\\").lower()
        return key in entries or key in base

    drop = set()
    reasons = {}

    def mark(key, why):
        if key in entries:
            drop.add(key)
            reasons.setdefault(why, []).append(key)

    # (1) every character file that replaces one the base game already had.
    # Additions under character/ — the pack's separate NPC models — are not
    # touched here; they are judged on their textures like anything else.
    if not keep_hd:
        for key in list(entries):
            if key.startswith("character\\") and key in base:
                mark(key, "character replacement (HD geosets and art set)")

    # (3) the DBCs that are not about models at all, or that carry the HD art set
    unwanted_dbcs = ["dbfilesclient\\emotestextsound.dbc"]
    if not keep_hd:
        # CharSections is what aims the compositor at _HD art; it belongs with
        # the HD models and goes with them either way.
        unwanted_dbcs.append("dbfilesclient\\charsections.dbc")
    for key in unwanted_dbcs:
        mark(key, "DBC unrelated to models, or pointing at HD art")

    # (2) models missing a texture that is not merely a reflection map
    #
    # Only a type-0 slot counts. That is the one that names its own art and gets
    # nothing supplied at runtime, so a name there that resolves to nothing is a
    # surface with no texture at all.
    for key, val in list(entries.items()):
        if not key.endswith(".m2") or key in drop:
            continue
        textures = m2_textures(os.path.join(files_root, val["p"]))
        # Only the FIRST texture slot decides. That is the one that covers the
        # mesh; the slots after it are what a model adds on top — a reflection,
        # a glow, waterfall spray. The Stormwind lion statue lost slot 0 and
        # rendered white, which is the case this rule is for. The gryphon roost
        # lost slots 1 and 2 — spray and a reflection — and was disabled beside
        # it for no reason, which is why the roosting gryphons outside a flight
        # master were still the stock ones.
        first = [name for typ, name in textures[:1] if typ == 0 and name]
        hard = [name for name in first
                if not resolves(name) and not is_reflection(name)]
        # A name in a runtime skin slot is NOT a reason to disable the model.
        # Types 11, 12 and 13 are filled from CreatureDisplayInfo at spawn, over
        # whatever the file says — so a leftover name that resolves to nothing
        # costs nothing, and the display's own skin is what gets drawn. Disabling
        # on that signature threw away 47 working HD meshes, the gryphon among
        # them, and put the stock bird back.
        why = "model missing a base texture (renders white)" if hard else None
        if why:
            mark(key, why)
            stem = key[:-3]
            for i in range(6):
                mark("%s%02d.skin" % (stem, i), "skin of a disabled model")

    # (3a) face art authored for a different atlas than the models use.
    #
    # Four races ship a face at twice the size of the region it goes in — and it
    # is not a larger version of the same picture. A human female's faceLower is
    # a strip of nose, lips and chin; a draenei female's is a whole head, ear,
    # horn, mouth and eyeball on one sheet. Meanwhile both models' head UVs run
    # the same range, u 0.005..0.496 and v 0.61..0.99, which is the stock face
    # layout. So the art does not belong in that slot at any canvas size, and
    # growing the atlas for it only makes everything around it softer.
    #
    # The models are fine and the stock face art fits them, so the oversized
    # replacements go and the game's own faces are used underneath the new mesh.
    def blp_size(path):
        try:
            with open(path, "rb") as f:
                head = f.read(20)
            if head[:4] != b"BLP2":
                return None
            return struct.unpack("<II", head[12:20])
        except OSError:
            return None

    for key in list(entries):
        if key in drop or not key.endswith(".blp") or key not in base:
            continue
        name = key.rsplit("\\", 1)[-1]
        if "facelower" not in name and "faceupper" not in name:
            continue
        if not key.startswith("character\\"):
            continue
        new_size = blp_size(os.path.join(files_root, entries[key]["p"]))
        old_size = blp_size(os.path.join(base_root, base[key]["p"]))
        if new_size and old_size and new_size[0] > old_size[0]:
            mark(key, "face art authored for a different atlas than the model uses")

    # (3b) art the pack shipped for a model these rules disabled.
    #
    # A replacement texture is painted for the UVs of the mesh it came with. Put
    # the stock mesh back and leave that texture over the stock one, and it is
    # the wrong picture stretched over different geometry — the pack's gryphon
    # skin is 2184 vertices' worth of art on an 846-vertex bird. Only
    # replacements go: a texture the pack ADDS is a new variant nothing was
    # using, and the models still present may well want it.
    disabled_model_dirs = {key.rsplit("\\", 1)[0] for key in drop if key.endswith(".m2")}
    live_model_dirs = {key.rsplit("\\", 1)[0] for key in entries
                       if key.endswith(".m2") and key not in drop}
    for key in list(entries):
        if key in drop or not key.endswith(".blp") or key not in base:
            continue
        directory = key.rsplit("\\", 1)[0]
        if directory in disabled_model_dirs and directory not in live_model_dirs:
            mark(key, "art for a mesh these rules disabled")

    # (4) an .anim or .skin is keyframes and submeshes FOR a particular model.
    # Left overriding a model that is not itself overridden, it is the base
    # game's skeleton being driven by another model's animation data — which is
    # the segmented-limbs half of the character fault, and disabling a model in
    # (2) creates a fresh one of these every time. Run to a fixed point, since
    # dropping a skin can never drop a model but the order is not worth
    # reasoning about.
    while True:
        again = 0
        for key in list(entries):
            if key in drop or key not in base:
                continue
            if key.endswith(".anim"):
                model = re.sub(r"\d{4}-\d{2}\.anim$", ".m2", key)
            elif key.endswith(".skin"):
                model = re.sub(r"\d{2}\.skin$", ".m2", key)
            else:
                continue
            if model not in entries or model in drop:
                mark(key, "animation or skin whose model is not replaced")
                again += 1
        if not again:
            break

    kept = {k: v for k, v in entries.items() if k not in drop}

    print("overlay: %s" % overlay_root)
    print("  entries before : %d" % len(entries))
    print("  disabled       : %d" % len(drop))
    for why in sorted(reasons):
        print("      %-52s %d" % (why, len(reasons[why])))
    print("  entries after  : %d" % len(kept))

    # CreatureDisplayInfo: keep the pack's re-points onto creature models, put
    # the ones onto HD humanoids back to what the base game had.
    cdi_key = "dbfilesclient\\creaturedisplayinfo.dbc"
    cmd_key = "dbfilesclient\\creaturemodeldata.dbc"
    reverted = kept_repoints = 0
    if cdi_key in kept and cmd_key in kept:
        cdi_path = os.path.join(files_root, kept[cdi_key]["p"])
        cmd_path = os.path.join(files_root, kept[cmd_key]["p"])
        base_cdi = None
        for name in os.listdir(os.path.join(base_root, "db")):
            if name.lower() == "creaturedisplayinfo.dbc":
                base_cdi = os.path.join(base_root, "db", name)
        pack_rows, rec, fld, rsize = load_dbc(cdi_path)
        base_rows, _, _, _ = load_dbc(base_cdi)
        model_rows, _, _, _ = load_dbc(cmd_path)

        def model_path(model_id):
            row = model_rows.get(model_id)
            return row[1](row[0][2]) if row else ""

        def model_file_resolves(mdx):
            if not mdx:
                return False
            key = mdx.replace("/", "\\").lower()
            if key.endswith(".mdx"):
                key = key[:-4] + ".m2"
            return key in kept or key in base

        patch = []
        for disp_id, (vals, _r, index) in pack_rows.items():
            if disp_id not in base_rows:
                continue
            was = base_rows[disp_id][0][1]
            now = vals[1]
            if was == now:
                continue
            target = model_path(now)
            # A re-point is only worth keeping if the model it names is one this
            # client can draw AND is still reachable. Disabling a model above
            # leaves any re-point onto it aiming at nothing, and a display id
            # whose model file is missing does not fall back — the creature
            # simply does not appear, which is worse than the old model.
            usable = ((keep_hd or not target.upper().startswith("CHARACTER\\"))
                      and model_file_resolves(target))
            if usable:
                kept_repoints += 1
            else:
                patch.append((index, was))
                reverted += 1
        print("  display re-points reverted to the base game : %d" % reverted)
        print("  display re-points kept (creature models)    : %d" % kept_repoints)

        # The pack ships humanoids at two levels of detail: the full model, and a
        # reduced one under an NPC\ folder for crowds. Its display table points
        # every humanoid NPC at the reduced one, and for some races that is no
        # better than what the game already had — a human female NPC is 6558
        # vertices against the stock 6305, which is why they still looked old
        # standing next to a player at 17878.
        #
        # With --keep-hd-characters the full models are wanted, so the NPC rows
        # are pointed at them. It costs what it sounds like it costs: a crowded
        # city draws NPCs at two to three times the vertices. Without the flag
        # this is not done, and the reduced models stay.
        npc_upgrade = []
        if keep_hd:
            for model_id, (mvals, mresolve, mindex) in model_rows.items():
                path = mresolve(mvals[2])
                lower = path.replace("/", "\\").lower()
                if "\\npc\\" not in lower:
                    continue
                # Character\Human\Female\NPC\HumanFemaleNPC.mdx
                #   -> Character\Human\Female\HumanFemale.mdx
                parts = path.replace("/", "\\").split("\\")
                if len(parts) < 2:
                    continue
                stem = parts[-1]
                if stem.lower().endswith(".mdx") and stem[:-4].lower().endswith("npc"):
                    full = "\\".join(parts[:-2] + [stem[:-7] + ".mdx"])
                    key = full.lower()[:-4] + ".m2"
                    if key in kept:
                        npc_upgrade.append((mindex, full))
            print("  humanoid NPC rows pointed at the full model  : %d" % len(npc_upgrade))

        # A display row's skin names belong to the model it draws. The pack
        # changes both together; reverting the model alone leaves art authored
        # for a mesh that is no longer there, wrapped onto one it does not fit.
        # So the skins go back wherever the model did — and wherever the model
        # this row ends up using is not one the pack supplies, since a
        # replacement the rules above disabled leaves exactly the same
        # disagreement.
        skin_fields = (6, 7, 8)   # Skin1, Skin2, Skin3
        skin_patch = []
        for disp_id, (vals, resolve, index) in pack_rows.items():
            base_row = base_rows.get(disp_id)
            if not base_row:
                continue
            final_model = vals[1]
            for idx, was in patch:
                if idx == index:
                    final_model = was
                    break
            if model_file_resolves(model_path(final_model)) and \
               model_path(final_model).replace("/", "\\").lower()[:-4] + ".m2" in kept:
                continue  # the pack supplies this model; its own art belongs on it
            for f in skin_fields:
                want = base_row[1](base_row[0][f])
                if resolve(vals[f]) != want:
                    skin_patch.append((index, f, want))
        print("  skin names put back with their model            : %d" %
              len({i for i, _f, _w in skin_patch}))

        if apply_changes and npc_upgrade:
            mdata = bytearray(open(cmd_path, "rb").read())
            mrec, mfld, mrsize = struct.unpack("<III", mdata[4:16])
            mstart = 20 + mrec * mrsize
            mblock = bytearray(mdata[mstart:])
            madded = {}
            for _mi, full in npc_upgrade:
                if full not in madded:
                    madded[full] = len(mblock)
                    mblock += full.encode("utf-8") + b"\0"
            for mi, full in npc_upgrade:
                off = 20 + mi * mrsize + 2 * 4     # field 2 is the model path
                mdata[off:off + 4] = struct.pack("<I", madded[full])
            # The header's fifth word is how long the string block is, and a
            # reader that trusts it — this client's does — treats an offset past
            # the end as no string at all. Growing the block without saying so
            # left every patched row with an empty path, and a display whose
            # model has no path does not render: the NPCs went invisible and the
            # log said "No model path for modelId 6001".
            mdata[16:20] = struct.pack("<I", len(mblock))
            open(cmd_path, "wb").write(bytes(mdata[:mstart] + mblock))
            print("  rewrote %s" % cmd_path)

        if apply_changes and (patch or skin_patch):
            data = bytearray(open(cdi_path, "rb").read())
            for index, was in patch:
                # field 1 is ModelID; the header is 20 bytes
                off = 20 + index * rsize + 4
                data[off:off + 4] = struct.pack("<I", was)
            if skin_patch:
                # Reverted names have to live somewhere the record can point at,
                # and the base table's offsets mean nothing in this file's string
                # block. Append them here and point at the copy; records keep
                # their size, so only the block grows.
                strings_start = 20 + rec * rsize
                block = bytearray(data[strings_start:])
                added = {}
                for _index, _f, want in skin_patch:
                    if want in added:
                        continue
                    if want == "":
                        added[want] = 0
                        continue
                    added[want] = len(block)
                    block += want.encode("utf-8") + b"\0"
                for index, f, want in skin_patch:
                    off = 20 + index * rsize + f * 4
                    data[off:off + 4] = struct.pack("<I", added[want])
                data[16:20] = struct.pack("<I", len(block))
                data = data[:strings_start] + block
            open(cdi_path, "wb").write(bytes(data))
            print("  rewrote %s" % cdi_path)

    if not apply_changes:
        print("\n(dry run — pass --apply to write)")
        return 0

    manifest["entries"] = kept
    manifest["fileCount"] = len(kept)
    json.dump(manifest, open(manifest_path, "w"), separators=(",", ":"))
    print("  wrote %s" % manifest_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
