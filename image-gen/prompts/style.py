"""Prompt hierarchy for asset generation.

Every final prompt is composed from four layers so the whole asset set reads as
one coherent art pack:

    L0  STYLE_CORE   — global art direction (realistic, Age-of-Empires vibe).
    L1  CATEGORY     — per-category framing (view angle, framing, scale intent).
    L2  <subject>    — the one-line subject, defined per asset in manifest.py.
    Lx  TECH         — shared technical constraints (transparent bg, no text...).

    compose(category, subject) -> full prompt string

Change STYLE_CORE once and the entire pack shifts together. Change a CATEGORY
template to re-tune a whole class of assets. Only manifest.py knows subjects.
"""

# ---------------------------------------------------------------------------
# L0 — global art direction. This is the single knob that defines "the look".
# ---------------------------------------------------------------------------
STYLE_CORE = (
    "Realistic hand-painted real-time-strategy game art in the visual tradition "
    "of Age of Empires II: Definitive Edition and Age of Empires IV. "
    "Grounded, believable proportions and materials — worn metal, rough timber, "
    "dyed wool, stone and dirt — with rich painterly texture and soft physically "
    "plausible lighting from the upper-left. Muted, earthy, historically-grounded "
    "color palette; high detail but readable at small size. NOT cartoon, NOT cel "
    "shaded, NOT flat vector, NOT pixel art, NOT anime, NOT toy-like."
)

# ---------------------------------------------------------------------------
# L1 — category framing. One entry per asset category.
# ---------------------------------------------------------------------------
CATEGORY = {
    # Standing figures, seen from a raised 3/4 game-camera angle, small on screen.
    "unit": (
        "A single game unit sprite: one character rendered from a raised 3/4 "
        "top-down angle (camera ~45 degrees above), full body, standing upright "
        "and centered, facing the lower-right of the frame. It has a clearly "
        "defined faction cloth/banner region painted in flat pure magenta "
        "(#FF00FF) — this is a recolor mask, keep it a solid even magenta with no "
        "shading — so the engine can tint it to each player's team color. "
        "The rest of the unit uses natural material colors."
    ),
    # Structures, isometric, sitting on the ground plane.
    "building": (
        "A single game building sprite rendered in isometric 3/4 view (camera "
        "~45 degrees above and rotated 45 degrees), the whole structure centered "
        "and complete within the frame, sitting on a small implied ground "
        "footprint with no surrounding terrain."
    ),
    # Small world objects / gatherable resources sitting on a tile.
    "resource": (
        "A single small resource object for a strategy-game map tile, seen from a "
        "raised 3/4 top-down angle, centered, self-contained, reading clearly as "
        "an icon-scale object even though it is fully rendered and realistic."
    ),
    # Seamless top-down ground texture tile (phase 2 — hardest to tile).
    "terrain": (
        "A seamless top-down ground texture for a strategy-game map tile, filling "
        "the entire square frame edge to edge with an even, tileable surface and "
        "no single dominant focal object, lit flatly and evenly so tiles blend "
        "when placed edge to edge."
    ),
    # Flat-ish UI action glyph, but rendered with realistic material.
    "action": (
        "A single clean user-interface action icon for a strategy game, one "
        "centered symbolic object filling most of the frame, rendered with "
        "realistic material and lighting but composed as a crisp readable HUD "
        "glyph with a subtle rim so it stays legible on any background."
    ),
    # Non-tiling overlays (fog, selection shine, walls, roads).
    "overlay": (
        "A single game map overlay element rendered from a raised 3/4 top-down "
        "angle, centered and self-contained, meant to be layered on top of the "
        "map terrain."
    ),
}

# ---------------------------------------------------------------------------
# Lx — shared technical constraints. Appended to every prompt. Two flavors:
# transparent cut-out sprites vs. edge-to-edge terrain tiles.
# ---------------------------------------------------------------------------
TECH_SPRITE = (
    "The subject is fully isolated on a 100% transparent background with clean "
    "anti-aliased edges and no ground shadow, no drop shadow, no vignette, no "
    "backdrop. Exactly one subject, centered, with a small even margin. "
    "Absolutely no text, no letters, no numbers, no logos, no watermark, no "
    "UI frames, no borders, no color swatches, no grid."
)
TECH_TILE = (
    "Fill the whole square frame — no transparency, no border, no vignette, no "
    "central subject. The four edges must tile seamlessly against copies of "
    "themselves. Absolutely no text, letters, numbers, logos, watermark, "
    "characters or man-made objects unless they are the requested surface."
)

# categories that want the tile tech block instead of the sprite one
_TILE_CATEGORIES = {"terrain"}


def compose(category: str, subject: str) -> str:
    """Assemble the full layered prompt for one asset."""
    if category not in CATEGORY:
        raise KeyError(f"unknown category {category!r}; expected one of {sorted(CATEGORY)}")
    tech = TECH_TILE if category in _TILE_CATEGORIES else TECH_SPRITE
    return (
        f"{STYLE_CORE}\n\n"
        f"{CATEGORY[category]}\n\n"
        f"Subject: {subject.strip()}\n\n"
        f"{tech}"
    )
