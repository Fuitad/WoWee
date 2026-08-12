# Wowee Open Format Specification v1.2

Novel file formats for custom WoW zone content. No Blizzard IP.

## WOT - Wowee Open Terrain (JSON metadata)
- Extension: `.wot`
- Contains: tile coords, texture list, per-chunk layers/holes, water data,
  doodad placements (M2 objects), WMO placements (buildings)
- Key: `"format": "wot-1.0"`
- Placement fields: `doodadNames[]`, `doodads[]` (nameId, uniqueId, pos, rot, scale, flags)
- WMO fields: `wmoNames[]`, `wmos[]` (nameId, uniqueId, pos, rot, flags, doodadSet)

## WHM - Wowee HeightMap (binary)
- Extension: `.whm`
- Magic: `WHM1` (0x314D4857)
- Layout: magic(4) + chunkCount(4=256) + vertsPerChunk(4=145) + per-chunk data × 256
- Per-chunk: baseHeight(4 float) + heights[145](580 = 145 floats) + alphaSize(4) + alphaData(alphaSize bytes)
- Alpha data: raw alpha blend maps for texture layers (same format as ADT MCAL)
- Backward compatible: older WHM files without alpha data still load (alphaSize=0)

## WOM - Wowee Open Model (binary)
- Extension: `.wom`
- Magic: `WOM1` (0x314D4F57) static, `WOM2` (0x324D4F57) animated, `WOM3` (0x334D4F57) multi-batch
- Layout: magic(4) + vertCount(4) + indexCount(4) + texCount(4) + boundRadius(4) + boundMin(12) + boundMax(12) + nameLen(2) + name + vertices + indices(uint32 each) + [pathLen(2) + path] × texCount
- WOM1 Vertex: position(vec3=12) + normal(vec3=12) + texCoord(vec2=8) = 32 bytes
- WOM2/WOM3 Vertex: + boneWeights(4) + boneIndices(4) = 40 bytes
- WOM2/WOM3 trailing block: boneCount(4) + [keyBoneId(4) + parentBone(2) + pivot(12) + flags(4)] × bones
  + animCount(4) + [id(4) + duration(4) + speed(4) + per-bone-keyframe-block] × anims
- Keyframe: timeMs(4) + translation(12) + rotation(16) + scale(12) = 44 bytes
- WOM3 Batches (after WOM2 trailing block): batchCount(4) + [indexStart(4) + indexCount(4) + textureIndex(4) + blendMode(2) + flags(2)] × N
- WOM3 blendMode: 0=opaque, 1=alpha-test, 2=alpha, 3=add, 4=mod, 5=mod2x, 6=blendAdd, 7=screen
- WOM3 flags: bit 0 = unlit, bit 1 = two-sided, bit 2 = no z-write
- Backward compatible: WOM1 files load without bone/animation data; WOM3 falls back to single-batch when batches block missing

## WOB - Wowee Open Building (binary)
- Extension: `.wob`
- Magic: `WOB1` (0x31424F57)
- Layout: magic(4) + groupCount(4) + portalCount(4) + doodadCount(4) + boundRadius(4) + nameLen(2) + name + groups + portals + doodads
- Group: nameLen(2) + name + vertexCount(4) + indexCount(4) + texCount(4) + outdoor(1) + boundMin(12) + boundMax(12)
  + vertices(pos+normal+uv+color = 48 bytes) + indices(uint32) + texPaths(prefixed) + materialCount(4) + materials
- Material: pathLen(2) + texturePath + flags(4) + shader(4) + blendMode(4)
- Doodad: pathLen(2) + modelPath + position(12) + rotation(12 euler degrees) + scale(4)
- Portal: groupA(4) + groupB(4) + vertexCount(4) + vertices(12 bytes each)
- toWMOModel restoration:
  - Materials are deduplicated across all groups by (texture, blend, flags)
  - Texture paths .png → .blp (renderer's PNG override is keyed on .blp)
  - Outdoor flag → WMO group flag bit 0x08
  - Portal vertices reconstruct MOPR refs for groupA / groupB
  - Doodad euler degrees → glm::quat using glm::eulerAngles convention
  - Default `Set_$DefaultGlobal` doodadSet emitted when doodads present

## WCP - Wowee Content Pack (archive)
- Extension: `.wcp`
- Magic: `WCP1` (0x31504357)
- Layout: magic(4) + fileCount(4) + infoJsonSize(4) + infoJSON + [pathLen(2) + path + dataSize(4) + data] × N
- Info JSON includes categorized file list (terrain/model/building/texture/data)

## zone.json - Map Definition
- Replaces WDT
- Fields: `mapName`, `displayName`, `mapId`, `biome`, `baseHeight`
- `hasCreatures`, `description`, `tiles` array, `files` map
- `doodadNames[]`, `doodads[]`, `wmoNames[]`, `wmos[]` for placed objects
- `editorVersion` for compatibility tracking

## JSON DBC - Data Table Replacement
- Replaces binary DBC files
- Format: `{"format": "wowee-dbc-json-1.0", "records": [...], "fieldCount": N}`
- Records are arrays of mixed types: integers, floats, strings
- Client loads via DBCFile::loadJSON() when found in custom_zones/ or output/

## PNG Textures - Texture Replacement
- Replaces BLP texture files
- Standard PNG format, loaded by client's texture override system
- Editor auto-converts BLP→PNG on export via stb_image_write

## WOC - Wowee Open Collision (binary)
- Extension: `.woc`
- Magic: `WOC1` (0x31434F57)
- Layout: magic(4) + triCount(4) + tileX(4) + tileY(4) + boundsMin(12) + boundsMax(12) + triangles
- Triangle: v0(12) + v1(12) + v2(12) + flags(1) = 37 bytes
- Flags: 0x01=walkable, 0x02=water, 0x04=steep, 0x08=indoor
- Generated from terrain heightmap with slope classification (50 deg threshold)
- Respects terrain holes (skips triangles in hole regions)
- WoweeCollisionBuilder::addMesh appends placed WMO group geometry
  (transformed into world space, slope-classified) so collision covers
  buildings as well as terrain.

## Terrain Stamps (.json)
- Portable terrain feature snapshots (mountains, craters, etc.)
- Format: `{"format": "wowee-stamp-1.0", "vertices": [[dx, dy, height], ...]}`
- Can be saved/loaded across zones and sessions

## Open Format Scoring (0-7)
1. WOT terrain metadata present
2. WHM heightmap with valid magic
3. zone.json map definition
4. PNG textures present
5. WOM models with valid magic
6. WOB buildings with valid magic
7. WOC collision mesh with valid magic

## SQL Server Export
- AzerothCore-flavored INSERT statements for: `creature_template`, `creature`,
  `creature_addon`, `waypoint_data`, `quest_template`, `creature_queststarter`,
  `creature_questender`.
- Coordinates: editor render coords are converted to WoW canonical via
  `core::coords::renderToCanonical` (X/Y swap) before write.
- Orientation: editor degrees from +renderX (west) → WoW radians from +X (north).
  Conversion: `wowYaw = π/2 - editorYaw` then normalised to [0, 2π).
- Quest objectives: KillCreature targets fill RequiredNpcOrGo[1..4]; CollectItem
  targets fill RequiredItemId[1..6]. Target ID parsed from objective.targetName.
- Quest givers/turn-in NPCs: written as `creature_queststarter` /
  `creature_questender` rows linking npc.id ↔ quest.id.

## Format Quick Reference

| Format    | Replaces | Magic              | Extension     | Purpose                             |
|-----------|----------|--------------------|---------------|-------------------------------------|
| WOT       | (part of ADT) | `wot-1.0` JSON  | `.wot`        | Terrain metadata + placements       |
| WHM       | ADT MCNK heights/alpha | `WHM1` (0x314D4857) | `.whm` | Heightmap + alpha layers       |
| WOM1/2/3  | M2/skin  | `WOM1/2/3` (0x31/32/33 4D4F57) | `.wom` | Static / animated / multi-batch model |
| WOB       | WMO + group files | `WOB1` (0x31424F57) | `.wob` | Building (groups + portals + doodads) |
| WOC       | (none, novel) | `WOC1` (0x31434F57) | `.woc` | Walkability collision mesh        |
| WCP       | MPQ archive | `WCP1` (0x31504357) | `.wcp` | Whole-zone bundle                |
| zone.json | WDT      | `"format":"wowee-zone-1.0"` | `zone.json` | Map definition + audio/flags |
| JSON DBC  | DBC      | `"format":"wowee-dbc-json-1.0"` | `*.json` | Data tables                  |
| PNG       | BLP      | PNG signature      | `.png`        | Textures (loaded via override sys)  |

## All formats are novel, portable, and open for redistribution.
## No Blizzard intellectual property is used in any format definition.

## Headless CLI Surface

The `wowee_editor` binary provides a complete non-GUI workflow over these formats:

### Inspection
- `--info <wom-base>` - WOM metadata (vertices/bones/animations/batches)
- `--info-wob <wob-base>` - WOB groups/portals/doodads
- `--info-woc <woc-path>` - WOC triangles/walkability counts
- `--info-wot <wot-base>` - WOT/WHM tile coord, chunks, height range
- `--info-wcp <wcp-path>` - WCP archive metadata (per-category counts)
- `--list-wcp <wcp-path>` - every file in a WCP, sorted by path
- `--info-creatures <p>` - creatures.json summary
- `--info-objects <p>` - objects.json summary
- `--info-quests <p>` - quests.json summary + chain validation

### Validation
- `--validate <zoneDir>` - open-format completeness score (0/7) with per-format counts
- `--zone-summary <zoneDir>` - one-shot validate + creature/object/quest counts
- `--diff-wcp <a> <b>` - file-by-file WCP comparison (added/removed/changed)

### Authoring
- `--scaffold-zone <name> [tx ty]` - create a blank zone in custom_zones/<slug>/
- `--build-woc <wot-base>` - generate WOC collision mesh from WHM/WOT
- `--regen-collision <zoneDir>` - rebuild every WOC under a zone dir
- `--export-png <wot-base>` - render heightmap/normal/zone-map PNG previews
- `--convert-m2 <path>` - M2 → WOM
- `--convert-wmo <path>` - WMO → WOB

### Packaging
- `--pack-wcp <zone> [dst]` - pack a zone into a WCP archive
- `--unpack-wcp <wcp> [dst]` - extract a WCP archive

### Discovery
- `--list-zones` - list custom_zones/ + output/ contents
- `--version` - version info
