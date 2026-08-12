# HD Texture Assets

**Source**: TurtleHD Texture Pack (Turtle WoW)
**Imported**: 2026-01-27
**Total Files**: 298 BLP textures
**Total Size**: 11MB

## Directory Structure

```
textures/
├── character/
│   └── human/          # 274 human male textures
├── creature/           # 15 creature textures
├── item/               # (reserved for future)
└── world/
    ├── generic/        # 1 generic world texture
    └── stormwind/      # 8 Stormwind building textures
```

## Usage

These HD BLP textures are ready for integration with:
- **WMO Renderer**: Building texture mapping
- **Character Renderer**: M2 model skin/face textures
- **Creature Renderer**: NPC texture application

## Integration Status

Textures are loaded via the BLP pipeline and applied to WMO/M2 renderers.
HD texture overrides (e.g. TurtleHD packs) can be placed as PNG files
alongside the original BLP paths - the asset manager checks for `.png`
overrides before loading the `.blp` version.
