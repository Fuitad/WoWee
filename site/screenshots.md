---
title: Screenshots
---

# Screenshots

Everything below is WoWee's own renderer: a native C++ client drawing the
world through Vulkan, with no Blizzard code in it. The game data is the
player's own.

<p align="center">
  <img src="assets/orgrimmar-entrance.png" alt="The gates of Orgrimmar, rendered by WoWee" width="100%" />
</p>

**The gates of Orgrimmar.** Terrain, world models and their doodads, drawn with
distance fog blended toward the sky's own horizon colour.

<p align="center">
  <img src="assets/krayonsignin.png" alt="The WoWee login screen" width="100%" />
</p>

**The login screen.** The interface is the game's own FrameXML, parsed and laid
out by this client rather than reimplemented.

<p align="center">
  <img src="assets/krayonload.png" alt="The WoWee loading screen" width="100%" />
</p>

**Loading into the world.**

---

## Adding to this page

Drop a `.png` into the repository's `assets/` directory and add a block here:

```markdown
<p align="center">
  <img src="assets/your-screenshot.png" alt="What it shows" width="100%" />
</p>

**A short caption.** A sentence about what is worth noticing in it.
```

The site's build copies `assets/*.png` across, so the path above is all that is
needed - nothing else to register. Keep them under a couple of megabytes each
where you can; the page loads every image on it.

[Back to the front page](index.md)
