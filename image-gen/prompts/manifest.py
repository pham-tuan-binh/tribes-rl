"""Asset manifest — the L2 subject line for every asset, plus its wiring.

Each Asset says:
  id        stable name (used for --only filtering and master filename)
  category  key into prompts.style.CATEGORY  (unit|building|resource|action|terrain|overlay)
  subject   the L2 subject line fed into compose()
  ref       existing asset used as the reference image (relative to web/assets),
            or None to generate purely from text
  out       where the finished sprite is copied in the game tree
            (relative to web/assets). For units this is the *neutral* master;
            recolor.py fans it out into the per-player numbered files.
  recolor   True for units — one neutral magenta-masked master is generated,
            then tinted to each PLAYER_COLOR and desaturated for Exhausted.

PLAYER_COLOR (from web/js/render.js) — the tribe tint targets:
    0 red #e2413e   1 blue #3f8ee0   2 yellow #e0b23f   3 green #54c96f
"""
from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class Asset:
    id: str
    category: str
    subject: str
    ref: Optional[str] = None
    out: str = ""
    recolor: bool = False


PLAYER_COLORS = {
    0: "#e2413e",  # red
    1: "#3f8ee0",  # blue
    2: "#e0b23f",  # yellow
    3: "#54c96f",  # green
}

# ---------------------------------------------------------------------------
# UNITS — 12 types. Generate ONE neutral master each (magenta faction region),
# recolor.py produces {0..3}.png and {0..3}Exhausted.png.
# ---------------------------------------------------------------------------
UNITS = [
    Asset("warrior", "unit",
          "a lightly-armored bronze-age tribal warrior gripping a short spear "
          "and a small round wooden shield, leather kilt, cloth sash over the "
          "shoulder as the faction-color region",
          ref="unit/warrior/warrior.png", out="unit/warrior/base.png", recolor=True),
    Asset("rider", "unit",
          "a mounted scout on a light horse at a walk, rider in a hooded tunic "
          "holding reins, saddle blanket as the faction-color region",
          ref="unit/rider/rider.png", out="unit/rider/base.png", recolor=True),
    Asset("defender", "unit",
          "a heavy infantry defender behind a tall rectangular tower shield, "
          "iron helmet and scale armor, shield face as the faction-color region",
          ref="unit/defender/0.png", out="unit/defender/base.png", recolor=True),
    Asset("swordsman", "unit",
          "a professional swordsman in a mail hauberk and open helm, drawing a "
          "steel sword, surcoat over the armor as the faction-color region",
          ref="unit/swordsman/0.png", out="unit/swordsman/base.png", recolor=True),
    Asset("archer", "unit",
          "an archer drawing a longbow, leather bracer and light cloth armor, "
          "a quiver on the back, cloth hood as the faction-color region",
          ref="unit/archer/archer.png", out="unit/archer/base.png", recolor=True),
    Asset("catapult", "unit",
          "a wooden torsion catapult siege engine on a timber frame with rope "
          "torsion bundles and iron fittings, a small pennant as the "
          "faction-color region",
          ref="unit/catapult/0.png", out="unit/catapult/base.png", recolor=True),
    Asset("knight", "unit",
          "an armored knight on a caparisoned warhorse, couched lance, full "
          "plate, horse caparison and pennon as the faction-color region",
          ref="unit/knight/knight.png", out="unit/knight/base.png", recolor=True),
    Asset("mind_bender", "unit",
          "a robed shaman priest holding a carved ritual staff topped with bone "
          "and feathers, hooded robe as the faction-color region",
          ref="unit/mind_bender/0.png", out="unit/mind_bender/base.png", recolor=True),
    Asset("boat", "unit",
          "a small single-mast wooden fishing boat with a furled sail and oars, "
          "the sail cloth as the faction-color region",
          ref="unit/boat/Boat.png", out="unit/boat/base.png", recolor=True),
    Asset("ship", "unit",
          "a wooden sailing warship, a caravel with two masts and square sails, "
          "the mainsail as the faction-color region",
          ref="unit/ship/0.png", out="unit/ship/base.png", recolor=True),
    Asset("battleship", "unit",
          "a large three-mast wooden ship of the line bristling with cannon "
          "ports, full rigging, the largest sail as the faction-color region",
          ref="unit/battleship/Battleship.png", out="unit/battleship/base.png", recolor=True),
    Asset("superunit", "unit",
          "a colossal armored war giant, a towering muscular champion in "
          "battle-scarred plate wielding a massive two-handed maul, a great "
          "war-banner cloak as the faction-color region",
          ref="unit/superunit/0.png", out="unit/superunit/base.png", recolor=True),
]

# ---------------------------------------------------------------------------
# BUILDINGS — 10 unique files, isometric, transparent, no recolor.
# ---------------------------------------------------------------------------
BUILDINGS = [
    Asset("dock",        "building", "a wooden fishing dock and pier with a small moored boat and net racks",
          ref="building/dock2.png", out="building/dock2.png"),
    Asset("mine",        "building", "a timber-framed mountain mine entrance with support beams, an ore cart and rails",
          ref="building/mine2.png", out="building/mine2.png"),
    Asset("forge",       "building", "a blacksmith forge with a stone chimney, anvil, and glowing furnace",
          ref="building/forge2.png", out="building/forge2.png"),
    Asset("farm",        "building", "a plowed farm field with neat crop furrows and a small wooden fence and shed",
          ref="building/farm2.png", out="building/farm2.png"),
    Asset("windmill",    "building", "a stone-and-timber windmill with four cloth sails and a thatched cap",
          ref="building/windmill2.png", out="building/windmill2.png"),
    Asset("custom_house","building", "a timber trade custom house with market stalls, crates and barrels of goods",
          ref="building/custom_house2.png", out="building/custom_house2.png"),
    Asset("lumber_hut",  "building", "a small woodcutter's log hut with an axe in a stump and stacked firewood",
          ref="building/lumber_hut2.png", out="building/lumber_hut2.png"),
    Asset("sawmill",     "building", "a water-powered sawmill with a large saw blade, log pile and plank stacks",
          ref="building/sawmill2.png", out="building/sawmill2.png"),
    Asset("temple",      "building", "a modest stone temple with columns and a stepped base, a small altar",
          ref="building/temple2.png", out="building/temple2.png"),
    Asset("monument",    "building", "a grand carved stone monument, an obelisk on a stepped plinth with relief carvings",
          ref="building/monument2.png", out="building/monument2.png"),
]

# ---------------------------------------------------------------------------
# RESOURCES — 7 gatherables, small world objects.
# ---------------------------------------------------------------------------
RESOURCES = [
    Asset("fish",   "resource", "two silvery fish breaking the water surface with a light ripple",
          ref="resource/fish2.png", out="resource/fish2.png"),
    Asset("fruit",  "resource", "a lush wild berry bush heavy with ripe red and blue berries",
          ref="resource/fruit2.png", out="resource/fruit2.png"),
    Asset("animal", "resource", "a wild deer standing alert on grass, a huntable game animal",
          ref="resource/animal2.png", out="resource/animal2.png"),
    Asset("whale",  "resource", "a whale surfacing and spouting in deep blue open water",
          ref="resource/whale2.png", out="resource/whale2.png"),
    Asset("ore",    "resource", "a rocky outcrop with glinting metal ore veins and a few loose ore chunks",
          ref="resource/ore2.png", out="resource/ore2.png"),
    Asset("crops",  "resource", "a cluster of ripe golden wheat stalks ready to harvest",
          ref="resource/crops2.png", out="resource/crops2.png"),
    Asset("ruins",  "resource", "a small crumbling ancient stone ruin with a broken column and mossy blocks",
          ref="resource/ruins2.png", out="resource/ruins2.png"),
]

# ---------------------------------------------------------------------------
# ACTIONS — 9 HUD glyphs. Realistic material, icon composition.
# ---------------------------------------------------------------------------
ACTIONS = [
    Asset("attack",  "action", "a pair of crossed steel swords, aggressive",
          ref="actions/attack.png", out="actions/attack.png"),
    Asset("capture", "action", "a planted victory flag on a pole claiming ground",
          ref="actions/capture.png", out="actions/capture.png"),
    Asset("convert", "action", "two curved arrows chasing in a circle around a glowing rune, transformation",
          ref="actions/convert.png", out="actions/convert.png"),
    Asset("disband", "action", "a broken snapped sword, retire or dismiss",
          ref="actions/disband.png", out="actions/disband.png"),
    Asset("examine", "action", "a brass magnifying glass over a scrap of old map, inspect",
          ref="actions/examine.png", out="actions/examine.png"),
    Asset("heal",    "action", "a green medicinal cross made of leaves and bandage, restore health",
          ref="actions/heal.png", out="actions/heal.png"),
    Asset("heal2",   "action", "a radiant green healing cross with a soft glow, area restore",
          ref="actions/heal2.png", out="actions/heal2.png"),
    Asset("move",    "action", "a set of leather boots with motion arrows on the ground, movement",
          ref="actions/move.png", out="actions/move.png"),
    Asset("upgrade", "action", "an upward golden chevron arrow over an anvil, promote or upgrade",
          ref="actions/upgrade.png", out="actions/upgrade.png"),
]

# ---------------------------------------------------------------------------
# OVERLAYS — non-tiling map layers.
# ---------------------------------------------------------------------------
OVERLAYS = [
    Asset("fog",   "overlay", "a soft dark grey cloud of fog of war covering a square map tile, semi-opaque smoky edges",
          ref="fog.png", out="fog.png"),
    Asset("shine", "overlay", "a soft glowing golden highlight ring marking a selectable tile, faint and translucent",
          ref="shine3.png", out="shine3.png"),
    Asset("walls", "overlay", "a ring of grey stone defensive city walls with crenellations enclosing a tile footprint",
          ref="terrain/walls.png", out="terrain/walls.png"),
]

# ---------------------------------------------------------------------------
# TERRAIN — PHASE 2. Only the 7 base tiles have subjects here; the ~24 edge
# variants per base are reconstructed by re-skinning existing variant masks
# (see PLAN.md "Terrain re-skinning"). Kept out of the default set on purpose.
# ---------------------------------------------------------------------------
TERRAIN = [
    Asset("plain",     "terrain", "short dry grassland with scattered dirt patches and small stones, top-down",
          ref="terrain/plain.png", out="terrain/plain.png"),
    Asset("forest",    "terrain", "dense deciduous forest canopy seen from above, rounded treetops, top-down",
          ref="terrain/forest2.png", out="terrain/forest2.png"),
    Asset("mountain",  "terrain", "rugged grey rocky mountain slope with snow caps and scree, top-down",
          ref="terrain/mountain3.png", out="terrain/mountain3.png"),
    Asset("water",     "terrain", "shallow turquoise coastal water with gentle ripples, top-down",
          ref="terrain/water.png", out="terrain/water.png"),
    Asset("deepwater", "terrain", "deep dark blue ocean water with subtle swell, top-down",
          ref="terrain/deepwater.png", out="terrain/deepwater.png"),
    Asset("village",   "terrain", "a cluster of small thatched-roof huts among grass, a hamlet seen from above, top-down",
          ref="terrain/village2.png", out="terrain/village2.png"),
    Asset("city",      "terrain", "a stone town center plaza with tiled roofs and packed-earth streets, top-down",
          ref="terrain/city3.png", out="terrain/city3.png"),
]

# ---------------------------------------------------------------------------
# Registry + lookup helpers.
# ---------------------------------------------------------------------------
GROUPS = {
    "unit": UNITS,
    "building": BUILDINGS,
    "resource": RESOURCES,
    "action": ACTIONS,
    "overlay": OVERLAYS,
    "terrain": TERRAIN,
}

# Phase-1 default set: everything self-contained and transparent. Terrain is
# opt-in (--group terrain) because its seamless-tiling is the hard problem.
DEFAULT_GROUPS = ["unit", "building", "resource", "action", "overlay"]

ALL = [a for g in GROUPS.values() for a in g]


def select(groups=None, ids=None):
    """Return assets filtered by category group and/or explicit id list."""
    pool = ALL if groups is None else [a for g in groups for a in GROUPS[g]]
    if ids:
        want = set(ids)
        pool = [a for a in pool if a.id in want]
    return pool
