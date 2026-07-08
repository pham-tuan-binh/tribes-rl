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
    # Whether to run the background-removal pass. None => derive from category
    # (cut-out sprite categories yes; terrain/overlay no). Override per asset,
    # e.g. walls is an overlay but still needs keying.
    cutout: Optional[bool] = None
    # Terrain family this asset sits on in-game (file stem: plain, water,
    # mountain3, ...). The pipeline attaches that family's *generated* texture
    # as a context reference so the object's palette/lighting blend with the
    # ground. For terrain assets it's the family they should stay coherent
    # with (everything -> plain). None = no terrain context (HUD icons, fog).
    terrain_ctx: Optional[str] = None


# Categories whose output is a cut-out sprite (background removed). Terrain is
# a full-bleed tile; overlays (fog/shine) are soft translucent glows — never key.
CUTOUT_CATEGORIES = {"unit", "building", "resource", "action"}


def wants_cutout(asset: "Asset") -> bool:
    if asset.cutout is not None:
        return asset.cutout
    return asset.category in CUTOUT_CATEGORIES


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
          ref="unit/warrior/warrior.png", out="unit/warrior/base.png", recolor=True, terrain_ctx="plain"),
    Asset("rider", "unit",
          "a mounted scout on a light horse at a walk, rider in a hooded tunic "
          "holding reins, saddle blanket as the faction-color region",
          ref="unit/rider/rider.png", out="unit/rider/base.png", recolor=True, terrain_ctx="plain"),
    Asset("defender", "unit",
          "a heavy infantry defender behind a tall rectangular tower shield, "
          "iron helmet and scale armor, shield face as the faction-color region",
          ref="unit/defender/0.png", out="unit/defender/base.png", recolor=True, terrain_ctx="plain"),
    Asset("swordsman", "unit",
          "a professional swordsman in a mail hauberk and open helm, drawing a "
          "steel sword, surcoat over the armor as the faction-color region",
          ref="unit/swordsman/0.png", out="unit/swordsman/base.png", recolor=True, terrain_ctx="plain"),
    Asset("archer", "unit",
          "an archer drawing a longbow, leather bracer and light cloth armor, "
          "a quiver on the back, cloth hood as the faction-color region",
          ref="unit/archer/archer.png", out="unit/archer/base.png", recolor=True, terrain_ctx="plain"),
    Asset("catapult", "unit",
          "a wooden torsion catapult siege engine on a timber frame with rope "
          "torsion bundles and iron fittings, a small pennant as the "
          "faction-color region",
          ref="unit/catapult/0.png", out="unit/catapult/base.png", recolor=True, terrain_ctx="plain"),
    Asset("knight", "unit",
          "an armored knight on a caparisoned warhorse, couched lance, full "
          "plate, horse caparison and pennon as the faction-color region",
          ref="unit/knight/knight.png", out="unit/knight/base.png", recolor=True, terrain_ctx="plain"),
    Asset("mind_bender", "unit",
          "a robed shaman priest holding a carved ritual staff topped with bone "
          "and feathers, hooded robe as the faction-color region",
          ref="unit/mind_bender/0.png", out="unit/mind_bender/base.png", recolor=True, terrain_ctx="plain"),
    Asset("boat", "unit",
          "a small single-mast wooden fishing boat with a furled sail and oars, "
          "the sail cloth as the faction-color region",
          ref="unit/boat/Boat.png", out="unit/boat/base.png", recolor=True, terrain_ctx="water"),
    Asset("ship", "unit",
          "a wooden sailing warship, a caravel with two masts and square sails, "
          "the mainsail as the faction-color region",
          ref="unit/ship/0.png", out="unit/ship/base.png", recolor=True, terrain_ctx="water"),
    Asset("battleship", "unit",
          "a large three-mast wooden ship of the line bristling with cannon "
          "ports, full rigging, the largest sail as the faction-color region",
          ref="unit/battleship/Battleship.png", out="unit/battleship/base.png", recolor=True, terrain_ctx="water"),
    Asset("superunit", "unit",
          "a colossal armored war giant, a towering muscular champion in "
          "battle-scarred plate wielding a massive two-handed maul, a great "
          "war-banner cloak as the faction-color region",
          ref="unit/superunit/0.png", out="unit/superunit/base.png", recolor=True, terrain_ctx="plain"),
]

# ---------------------------------------------------------------------------
# BUILDINGS — 10 unique files, isometric, transparent, no recolor.
# NOTE: ref=None on purpose. The original building sprites are authored TILTED
# (pre-rotated for the game's diamond frame), so using them as reference images
# drags that tilt into the generation. We generate buildings from the subject
# prompt + the upright --style-ref instead, keeping them level.
# ---------------------------------------------------------------------------
BUILDINGS = [
    Asset("dock",        "building", "a wooden fishing dock and pier with a small moored boat and net racks",
          out="building/dock2.png", terrain_ctx="water"),
    Asset("mine",        "building", "a timber-framed mountain mine entrance with support beams, an ore cart and rails",
          out="building/mine2.png", terrain_ctx="mountain3"),
    Asset("forge",       "building", "a blacksmith forge with a stone chimney, anvil, and glowing furnace",
          out="building/forge2.png", terrain_ctx="plain"),
    Asset("farm",        "building", "a plowed farm field with neat crop furrows and a small wooden fence and shed",
          out="building/farm2.png", terrain_ctx="plain"),
    Asset("windmill",    "building", "a stone-and-timber windmill with four cloth sails and a thatched cap",
          out="building/windmill2.png", terrain_ctx="plain"),
    Asset("custom_house","building", "a timber trade custom house with market stalls, crates and barrels of goods",
          out="building/custom_house2.png", terrain_ctx="plain"),
    Asset("lumber_hut",  "building", "a small woodcutter's log hut with an axe in a stump and stacked firewood",
          out="building/lumber_hut2.png", terrain_ctx="forest2"),
    Asset("sawmill",     "building", "a water-powered sawmill with a large saw blade, log pile and plank stacks",
          out="building/sawmill2.png", terrain_ctx="forest2"),
    Asset("temple",      "building", "a modest stone temple with columns and a stepped base, a small altar",
          out="building/temple2.png", terrain_ctx="plain"),
    Asset("monument",    "building", "a grand carved stone monument, an obelisk on a stepped plinth with relief carvings",
          out="building/monument2.png", terrain_ctx="plain"),
    # village/city live under terrain/ paths but are generated as building-style
    # sprites drawn upright over a plain ground tile (see render.js).
    Asset("village",     "building", "a small cluster of three thatched-roof huts around a dirt yard, a tiny hamlet",
          out="terrain/village2.png", terrain_ctx="plain"),
    Asset("city",        "building", "a walled medieval town center: stone keep, tiled-roof houses and a small plaza",
          out="terrain/city3.png", terrain_ctx="plain"),
]

# ---------------------------------------------------------------------------
# RESOURCES — 7 gatherables, small world objects.
# ---------------------------------------------------------------------------
RESOURCES = [
    Asset("fish",   "resource", "two silvery fish breaking the water surface with a light ripple",
          ref="resource/fish2.png", out="resource/fish2.png", terrain_ctx="water"),
    Asset("fruit",  "resource", "a lush wild berry bush heavy with ripe red and blue berries",
          ref="resource/fruit2.png", out="resource/fruit2.png", terrain_ctx="plain"),
    Asset("animal", "resource", "a wild deer standing alert on grass, a huntable game animal",
          ref="resource/animal2.png", out="resource/animal2.png", terrain_ctx="forest2"),
    Asset("whale",  "resource", "a whale surfacing and spouting in deep blue open water",
          ref="resource/whale2.png", out="resource/whale2.png", terrain_ctx="deepwater"),
    Asset("ore",    "resource", "a rocky outcrop with glinting metal ore veins and a few loose ore chunks",
          ref="resource/ore2.png", out="resource/ore2.png", terrain_ctx="mountain3"),
    Asset("crops",  "resource", "a cluster of ripe golden wheat stalks ready to harvest",
          ref="resource/crops2.png", out="resource/crops2.png", terrain_ctx="plain"),
    Asset("ruins",  "resource", "a small crumbling ancient stone ruin with a broken column and mossy blocks",
          ref="resource/ruins2.png", out="resource/ruins2.png", terrain_ctx="plain"),
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
# OVERLAYS — map layers.
#
# fog: an OPAQUE full-tile shroud (category 'tile'). The renderer draws it INSTEAD
#      of terrain on unseen tiles, so it just needs to hide what's under it — no
#      alpha. Generated as a full-bleed tile.
# walls: a solid cut-out object (category 'overlay', cutout).
# shine (selection glow) is a translucent VFX and is intentionally left as the
#      original — a hard cut-out would destroy its soft alpha.
# ---------------------------------------------------------------------------
OVERLAYS = [
    Asset("fog",   "tile", "a dense dark grey fog-of-war shroud, murky rolling clouds fully obscuring the ground below",
          out="fog.png"),
    Asset("walls", "overlay", "a ring of grey stone defensive city walls with crenellations enclosing a tile footprint",
          ref="terrain/walls.png", out="terrain/walls.png", cutout=True, terrain_ctx="plain"),
]

# ---------------------------------------------------------------------------
# TERRAIN — PHASE 2. Only the 7 base tiles have subjects here; the ~24 edge
# variants per base are reconstructed by re-skinning existing variant masks
# (see PLAN.md "Terrain re-skinning"). Kept out of the default set on purpose.
# ---------------------------------------------------------------------------
# ref=None on purpose (same as buildings): the seamless texture comes from the
# subject prompt + --style-ref, not the old cartoony tile. Each family's new
# texture is then re-skinned onto every base+variant tile (pipeline/terrain_reskin.py).
# out= encodes the family prefix used for matching (stem: plain, forest2, ...).
#
# ORDER MATTERS: plain is generated FIRST; every other family takes the freshly
# generated plain texture as a palette/lighting reference (terrain_ctx="plain")
# so all ground tiles blend together. Objects then reference their family.
# village2 / city3 are not ground: they're generated as sprites (see BUILDINGS).
TERRAIN = [
    Asset("plain",     "terrain", "seamless top-down short green grassland with scattered dirt patches and small stones",
          out="terrain/plain.png"),
    Asset("forest",    "terrain", "seamless top-down dense deciduous forest canopy, rounded green treetops",
          out="terrain/forest2.png", terrain_ctx="plain"),
    Asset("mountain",  "terrain", "seamless top-down rugged grey rocky mountain terrain with scree and stone",
          out="terrain/mountain3.png", terrain_ctx="plain"),
    Asset("water",     "terrain", "seamless top-down shallow turquoise coastal water with gentle ripples",
          out="terrain/water.png", terrain_ctx="plain"),
    Asset("deepwater", "terrain", "seamless top-down deep dark blue ocean water with subtle swell",
          out="terrain/deepwater.png", terrain_ctx="plain"),
]

# ---------------------------------------------------------------------------
# Registry + lookup helpers.
# ---------------------------------------------------------------------------
# Ordered: terrain FIRST — objects reference the freshly generated ground
# textures (terrain_ctx), so the ground must exist before what stands on it.
GROUPS = {
    "terrain": TERRAIN,
    "unit": UNITS,
    "building": BUILDINGS,
    "resource": RESOURCES,
    "action": ACTIONS,
    "overlay": OVERLAYS,
}

# Default: the full ordered pipeline (terrain -> objects on it).
DEFAULT_GROUPS = ["terrain", "unit", "building", "resource", "action", "overlay"]

ALL = [a for g in GROUPS.values() for a in g]


def select(groups=None, ids=None):
    """Return assets filtered by category group and/or explicit id list."""
    pool = ALL if groups is None else [a for g in groups for a in GROUPS[g]]
    if ids:
        want = set(ids)
        pool = [a for a in pool if a.id in want]
    return pool
