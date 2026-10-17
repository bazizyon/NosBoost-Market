# Market

A NosBoost mod for the NosBazaar and what your things are worth. One window in the mods menu (F9) has a tab for each part.

## Bazaar Filter

A filter panel sits next to the bazaar window. You say which stats you want and the mod marks the listings that have them, so you don't have to open every item.

### What it does

- Weapons and armour: pick up to 4 groups of stats. A group can need all of its stats, a number of them, or a total value. Each stat can have a minimum.
- Specialist cards: filter by upgrade, level, attack, defence, element, HP/MP, PvE, PvP and resistance points, and show each card's perfection right in the list.
- Partner cards: filter by the rank of each skill (F to S) and by upgrade, and show the ranks right in the list.
- Matching listings can be tinted, the others dimmed, and their buy button hidden.
- The window title shows how many listings on the page match.

The Filter button under "Search criteria" turns everything on and off.

## Character Value

Records what you search in the NosBazaar and shows what the items in your inventory are worth.

### What it does

- Every bazaar search you do is saved, so prices build up over time without any extra work.
- Your inventory shows a value on each item.
- Hover an item to see its value.
- Ctrl+Shift+F searches the item under your mouse in the bazaar.
- An editor (F9) lets you set how many of an item people usually buy at once, switch items off, give fixed prices, and say what an item can be turned into.

### How a value is decided

The value is the average price you would pay when buying the usual amount, starting from the cheapest listing. One cheap listing moves the value a little, and a silly price at the end of the page is never reached.

- If an item can be had cheaper another way (a skin from its chest), the cheaper price counts.
- If an item can be turned into something worth more (raid tokens in a shop), it is worth at least that.
- Items with a fixed price, like NPC shop items, are worth exactly that.

### Files

All in `mods\Market\CharacterValue`. They are read again as soon as they change.

| File | What it holds |
|---|---|
| `market\YYYY-MM.log` | Every bazaar search and answer. Everything else can be rebuilt from it. |
| `amounts.txt` | `<vnum> <amount>`: how many people buy at once |
| `unvalued.txt` | Items that are never valued |
| `fixed.txt` | `<vnum> <price>`: fixed prices |
| `sources.txt` | `<item> <source> [count]`: cheaper ways to get an item |
| `conversions.txt` | `<item> <output> [count]` or `random <item>`: what an item turns into |
| `rules.txt` | `default amount N`, `vnum X manual V`, `vnum X none`, `packages on` |
| `values.json` | Values from an outside tool, used before everything except `unvalued.txt` |

## Install

Download the DLL from this repo's Releases and put it in the game's `mods` folder. NosBoost loads it by itself. Open it from the NosBoost mods menu (F9).
