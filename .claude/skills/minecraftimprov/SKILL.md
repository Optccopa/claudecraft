---
name: minecraftimprov
description: >
  Use BEFORE implementing any new system that affects gameplay mechanics. Pull
  the real values and rules from minecraft.wiki first instead of guessing or
  approximating. Trigger whenever a vanilla game feature is being added or
  changed, even if the wiki is not mentioned. Not for engine, rendering, or
  build-system architecture.
allowed-tools: WebFetch
---

# Minecraft Wiki Lookup

Before writing a new vanilla feature, look up how the game actually does it.
The wiki is the source of truth for exact numbers and rules; memory and
existing comments are not.

## When to use

Any time a new update is being implemented that affects in-game logic.

Skip it for pure engine work (meshing, shaders, threading, file formats) that
has no vanilla equivalent.

## Workflow

1. Fetch the page directly:

       WebFetch https://minecraft.wiki/w/Water

   This returns the rendered page with its tables intact. If you don't know
   the exact title, resolve it first — see "Finding the right page" below.

2. Read the infobox and data tables, not just the prose — the exact values
   (hardness, blast resistance, luminance, stack size, tick rate) live there,
   not in the paragraph text.

3. Follow related entries before committing to a design. Features are coupled:
   water also means fluid behavior, waterlogging, and light; a block also means
   its tool tier, drops, and any redstone interaction. Look those up too rather
   than implementing the feature in isolation.

4. Record each value's source where it lands in code:

       // luminance 15 — minecraft.wiki/Glowstone

   so the number can be re-verified later.

## Finding the right page

Page URLs follow the in-game name: capitalize it and replace spaces with
underscores.

    https://minecraft.wiki/w/Redstone_Dust
    https://minecraft.wiki/w/Blast_Furnace
    https://minecraft.wiki/w/Light

Redirects resolve automatically, so `/w/Cobblestone` works even when it points
somewhere else. When a direct guess 404s or you're unsure of the title, resolve
it before fetching — three options, fastest first:

**Autocomplete the title.** `opensearch` returns a clean list of matching page
titles — best when you roughly know the name:

    https://minecraft.wiki/api.php?action=opensearch&search=blast+furnace&limit=5&format=json

**Full-text search.** Use this when the topic isn't itself a page name (a
mechanic, a value, a behavior). It returns the pages that mention the query:

    https://minecraft.wiki/api.php?action=query&list=search&srsearch=mob+spawning+light+level&format=json

**Human search page.** WebFetch the site's own results page and read it:

    https://minecraft.wiki/?search=waterlogging

Pick the right title from the results, then WebFetch `/w/<Title>` and read its
infobox and tables.

### Search tips

- A mechanic often has its own page separate from the block that uses it —
  light levels live on `/w/Light`, spawn rules on `/w/Spawn`, the full state
  list on `/w/Block_states`. If a value isn't on the obvious page, search the
  *mechanic* name, not the block.
- URL-encode spaces in queries as `+` or `%20`.
- Some names are ambiguous (a "Note Block" the block vs. note/pitch mechanics);
  `opensearch` surfaces the variants so you can pick the right one.
- Add `&format=json` to any API call; without it you get an HTML help page.