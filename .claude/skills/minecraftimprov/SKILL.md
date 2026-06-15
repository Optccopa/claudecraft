---
name: minecraftimprov
description: >
  Use BEFORE starting any new feature that affects gameplay mechanics. Pull
  the real values and rules from minecraft.wiki first instead of guessing or
  approximating. Trigger whenever a vanilla game feature is being added or
  changed, even if the wiki is not mentioned. Not for engine, rendering, or
  build-system architecture.
allowed-tools: WebFetch
---

# Minecraft Wiki Lookup

Before writing a new vanilla feature, look up how the game actually does it.
The wiki is the source of truth for exact numbers and rules; memory and
existing code comments are not.

This project targets Newer **Java Edition** Releases. When the wiki shows
per-edition or per-version columns, read the Java Edition column.

## When to use

Any time a feature that exists in vanilla is being implemented or changed —
blocks, mobs, items, world generation, game mechanics, player stats.

Skip it for pure engine work (meshing, shaders, threading, file formats) that
has no vanilla equivalent.

## Workflow

1. Fetch the page directly:

       WebFetch https://minecraft.wiki/w/Water

   This returns the rendered page with its tables intact. If you don't know
   the exact title, resolve it first — see "Finding the right page" below.

2. Read the infobox and data tables, not just the prose. Exact values live in
   the tables. Use the feature-type checklist below to know what to extract.

3. Follow the related pages listed in the checklist for the feature type you're
   implementing. Features are coupled — don't implement in isolation.

4. Add a source comment only when the number would look arbitrary without
   context — i.e., a reader couldn't infer it from the identifier alone:

       static constexpr float kBlastResistance = 6.0f; // minecraft.wiki/w/Oak_Log

   Skip the comment when the value is self-evident or named clearly enough.

## Data to extract by feature type

### Block
- Hardness, blast resistance, tool tier required, preferred tool
- Luminance (emitted light level 0–15)
- Opacity (does it block light fully?)
- Solid / replaceable / waterloggable flags
- Bounding box (full cube, or custom shape with exact dimensions)
- Drops (including tool requirements and fortune/silk-touch variants)
- Flammability (ignite chance, burn chance)
- Related pages: `/w/Mining`, `/w/Block_states`, `/w/Light`

### Mob
- Max health (hearts → HP = hearts × 2)
- Attack damage and knockback
- Armor points
- Movement speed (blocks/second — check "Behavior" section)
- Follow range, sight range
- Spawn conditions (biome, light level, block requirements)
- Drops and XP
- Related pages: `/w/Mob`, `/w/Spawn`, `/w/Damage`

### Item
- Stack size
- Durability (uses before break)
- Enchantability
- Any cooldown
- Related pages: `/w/Item`, `/w/Enchanting_mechanics`

### Game mechanic / player stat
- Exact formula when one exists (hunger exhaustion, fall damage, etc.)
- Tick rate or update frequency
- All edge cases listed in the "Behavior" or "Technical" sections
- Related pages: look for the mechanic's own article, not just the block
  that uses it (e.g., `/w/Light`, `/w/Fluid`, `/w/Hunger`, `/w/Sprinting`)

## Common cross-references

| Feature being added | Also check |
|---|---|
| Water / lava | `/w/Fluid`, `/w/Waterlogging`, `/w/Light` |
| Any transparent block | `/w/Opacity`, `/w/Light` |
| Redstone component | `/w/Redstone_circuit`, `/w/Tick` |
| Spawnable surface | `/w/Spawn`, `/w/Light` |
| Player movement change | `/w/Sprinting`, `/w/Hunger`, `/w/Attribute` |
| Any block with drops | `/w/Mining`, `/w/Fortune`, `/w/Silk_Touch` |

## Finding the right page

Page URLs follow the in-game name: capitalize it and replace spaces with
underscores.

    https://minecraft.wiki/w/Redstone_Dust
    https://minecraft.wiki/w/Blast_Furnace
    https://minecraft.wiki/w/Light

Redirects resolve automatically. When a direct guess 404s or you're unsure of
the title, resolve it before fetching — three options, fastest first:

**Autocomplete the title.** `opensearch` returns matching page titles — best
when you roughly know the name:

    https://minecraft.wiki/api.php?action=opensearch&search=blast+furnace&limit=5&format=json

**Full-text search.** Use when the topic is a mechanic or behavior, not a page
name:

    https://minecraft.wiki/api.php?action=query&list=search&srsearch=mob+spawning+light+level&format=json

**Human search page.**

    https://minecraft.wiki/?search=waterlogging

Pick the right title from the results, then WebFetch `/w/<Title>`.

### Search tips

- Mechanics often have their own page separate from the block — light levels on
  `/w/Light`, spawn rules on `/w/Spawn`, state lists on `/w/Block_states`.
- URL-encode spaces as `+` or `%20`.
- Add `&format=json` to API calls; without it you get an HTML help page.
- Ambiguous names (e.g. "Note Block" block vs. note pitch mechanic) —
  `opensearch` surfaces variants so you can pick the right one.
