package core.game;

// Golden-trace dumper for the polytopia-rl C port.
// Plays seeded random games driving GameState directly (mirroring
// Game.processTurn) and dumps one JSON line per action with:
//   - the canonical chosen action
//   - the sorted canonical legal-action set (before the action)
//   - a canonical dump of the full state (after the action)
// Stochastic actions (EXAMINE, LEVELUP/EXPLORER) are excluded from selection
// so the stream replays deterministically; diplomacy actions are excluded to
// match the C port's v1 scope. Post-capture unit->city assignments are dumped
// so the C replayer can inject Java's seeded shuffle results.
//
// Usage: java core.game.TraceDumper <levelSeed> <actionSeed> <maxSteps> <csvOut> <traceOut>
// Positions are printed as (x,y) = Java's (row, col) indices.

import core.Constants;
import core.Types;
import core.actions.Action;
import core.actions.cityactions.*;
import core.actions.tribeactions.*;
import core.actions.unitactions.*;
import core.actors.City;
import core.actors.Temple;
import core.actors.Building;
import core.actors.Tribe;
import core.actors.units.Unit;
import core.levelgen.LevelGenerator;

import java.io.FileWriter;
import java.util.*;

public class TraceDumper {

    public static void main(String[] args) throws Exception {
        long levelSeed = Long.parseLong(args[0]);
        long actionSeed = Long.parseLong(args[1]);
        int maxSteps = Integer.parseInt(args[2]);
        String csvOut = args[3];
        String traceOut = args[4];

        // Generate and save the level so the C side can load the identical map.
        LevelGenerator gen = new LevelGenerator(levelSeed);
        gen.init(11, 3, 4, 0.5, new Types.TRIBE[]{Types.TRIBE.XIN_XI, Types.TRIBE.OUMAJI});
        gen.generate();
        gen.toCSV(csvOut);

        GameState gs = new GameState(new Random(actionSeed), Types.GAME_MODE.CAPITALS);
        gs.init(csvOut);

        Random pick = new Random(actionSeed + 1);
        FileWriter out = new FileWriter(traceOut);

        out.write("{\"init\":" + stateJson(gs) + "}\n");

        int steps = 0;
        boolean over = false;
        while (!over && steps < maxSteps) {
            Tribe[] tribes = gs.getTribes();
            for (Tribe tribe : tribes) {
                if (tribe.getWinner() != Types.RESULT.INCOMPLETE) continue;
                gs.initTurn(tribe);
                out.write("{\"initTurn\":" + tribe.getTribeId() + ",\"state\":" + stateJson(gs) + "}\n");

                while (true) {
                    gs.computePlayerActions(tribe);
                    ArrayList<Action> all = gs.getAllAvailableActions();
                    ArrayList<Action> usable = new ArrayList<>();
                    ArrayList<String> legal = new ArrayList<>();
                    for (Action a : all) {
                        String c = canon(a, gs);
                        if (c == null) continue;           // excluded action kinds
                        legal.add(c);
                        if (!c.startsWith("EXAMINE") && !c.equals("LEVELUP EXPLORER " + cityPos(a, gs)))
                            if (!(a instanceof LevelUp) || ((LevelUp) a).getBonus() != Types.CITY_LEVEL_UP.EXPLORER)
                                usable.add(a);
                    }
                    Collections.sort(legal);

                    // pick: 1/8 bias to END_TURN when present, else uniform over usable
                    Action chosen = null;
                    if (pick.nextInt(8) == 0) {
                        for (Action a : usable) if (a instanceof EndTurn) { chosen = a; break; }
                    }
                    if (chosen == null) {
                        // avoid EXAMINE-only stalls: usable always has END_TURN or level-ups
                        chosen = usable.get(pick.nextInt(usable.size()));
                    }
                    String chosenCanon = canon(chosen, gs);
                    boolean isEnd = chosen instanceof EndTurn;
                    boolean isCapture = chosen instanceof Capture;

                    if (!isEnd) gs.next(chosen);
                    steps++;

                    StringBuilder line = new StringBuilder();
                    line.append("{\"step\":").append(steps)
                        .append(",\"player\":").append(tribe.getTribeId())
                        .append(",\"legal\":").append(strArr(legal))
                        .append(",\"chosen\":\"").append(chosenCanon).append("\"");
                    if (isCapture) line.append(",\"assign\":").append(assignJson(gs));
                    if (!isEnd) line.append(",\"state\":").append(stateJson(gs));
                    line.append("}\n");
                    out.write(line.toString());

                    if (isEnd || steps >= maxSteps) break;
                }
                gs.endTurn(tribe);
                out.write("{\"endTurn\":" + tribe.getTribeId() + ",\"state\":" + stateJson(gs) + "}\n");

                if (gs.gameOver()) { over = true; break; }
                if (steps >= maxSteps) { over = true; break; }
            }
            gs.incTick();
        }
        StringBuilder fin = new StringBuilder("{\"final\":[");
        for (int i = 0; i < gs.getTribes().length; i++) {
            if (i > 0) fin.append(",");
            fin.append("\"").append(gs.getTribes()[i].getWinner()).append("\"");
        }
        fin.append("]}\n");
        out.write(fin.toString());
        out.close();
        System.out.println("steps=" + steps);
    }

    static String cityPos(Action a, GameState gs) {
        if (a instanceof CityAction) {
            City c = (City) gs.getActor(((CityAction) a).getCityId());
            return pos(c.getPosition().x, c.getPosition().y);
        }
        return "";
    }

    static String pos(int x, int y) { return "(" + x + "," + y + ")"; }

    static String upos(int unitId, GameState gs) {
        Unit u = (Unit) gs.getActor(unitId);
        return pos(u.getPosition().x, u.getPosition().y);
    }

    // Canonical action string; null = excluded from the trace (diplomacy).
    static String canon(Action a, GameState gs) {
        if (a instanceof DeclareWar || a instanceof SendStars) return null;
        if (a instanceof EndTurn) return "END_TURN";
        if (a instanceof ResearchTech) return "RESEARCH " + ((ResearchTech) a).getTech();
        if (a instanceof BuildRoad) {
            var p = ((BuildRoad) a).getPosition();
            return "BUILD_ROAD " + pos(p.x, p.y);
        }
        if (a instanceof Move) {
            Move m = (Move) a;
            return "MOVE " + upos(m.getUnitId(), gs) + "->" + pos(m.getDestination().x, m.getDestination().y);
        }
        if (a instanceof Attack) {
            Attack at = (Attack) a;
            return "ATTACK " + upos(at.getUnitId(), gs) + "->" + upos(at.getTargetId(), gs);
        }
        if (a instanceof Convert) {
            Convert cv = (Convert) a;
            return "CONVERT " + upos(cv.getUnitId(), gs) + "->" + upos(cv.getTargetId(), gs);
        }
        if (a instanceof Capture) return "CAPTURE " + upos(((Capture) a).getUnitId(), gs);
        if (a instanceof Recover) return "RECOVER " + upos(((Recover) a).getUnitId(), gs);
        if (a instanceof HealOthers) return "HEAL_OTHERS " + upos(((HealOthers) a).getUnitId(), gs);
        if (a instanceof MakeVeteran) return "MAKE_VETERAN " + upos(((MakeVeteran) a).getUnitId(), gs);
        if (a instanceof Upgrade) {
            Upgrade up = (Upgrade) a;
            Unit u = (Unit) gs.getActor(up.getUnitId());
            return "UPGRADE " + upos(up.getUnitId(), gs) + " " + u.getType();
        }
        if (a instanceof Disband) return "DISBAND " + upos(((Disband) a).getUnitId(), gs);
        if (a instanceof Examine) return "EXAMINE " + upos(((Examine) a).getUnitId(), gs);
        if (a instanceof Build) {
            Build b = (Build) a;
            return "BUILD " + cityPos(a, gs) + " " + b.getBuildingType() + " " + pos(b.getTargetPos().x, b.getTargetPos().y);
        }
        if (a instanceof Spawn) return "SPAWN " + cityPos(a, gs) + " " + ((Spawn) a).getUnitType();
        if (a instanceof LevelUp) return "LEVELUP " + ((LevelUp) a).getBonus() + " " + cityPos(a, gs);
        if (a instanceof ResourceGathering) {
            ResourceGathering g = (ResourceGathering) a;
            return "GATHER " + cityPos(a, gs) + " " + pos(g.getTargetPos().x, g.getTargetPos().y);
        }
        if (a instanceof ClearForest) {
            var p = ((ClearForest) a).getTargetPos();
            return "CLEAR_FOREST " + cityPos(a, gs) + " " + pos(p.x, p.y);
        }
        if (a instanceof BurnForest) {
            var p = ((BurnForest) a).getTargetPos();
            return "BURN_FOREST " + cityPos(a, gs) + " " + pos(p.x, p.y);
        }
        if (a instanceof GrowForest) {
            var p = ((GrowForest) a).getTargetPos();
            return "GROW_FOREST " + cityPos(a, gs) + " " + pos(p.x, p.y);
        }
        if (a instanceof Destroy) {
            var p = ((Destroy) a).getTargetPos();
            return "DESTROY " + cityPos(a, gs) + " " + pos(p.x, p.y);
        }
        return "UNKNOWN:" + a.getClass().getSimpleName();
    }

    static String strArr(List<String> xs) {
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < xs.size(); i++) {
            if (i > 0) b.append(",");
            b.append("\"").append(xs.get(i)).append("\"");
        }
        return b.append("]").toString();
    }

    // Post-capture ordered unit->city assignments: {"(cx,cy)":["(ux,uy)",...],...}
    static String assignJson(GameState gs) {
        StringBuilder b = new StringBuilder("{");
        boolean firstC = true;
        for (Tribe t : gs.getTribes()) {
            for (int cityId : t.getCitiesID()) {
                City c = (City) gs.getActor(cityId);
                if (!firstC) b.append(",");
                firstC = false;
                b.append("\"").append(pos(c.getPosition().x, c.getPosition().y)).append("\":[");
                boolean firstU = true;
                for (int uid : c.getUnitsID()) {
                    Unit u = (Unit) gs.getActor(uid);
                    if (!firstU) b.append(",");
                    firstU = false;
                    b.append("\"").append(pos(u.getPosition().x, u.getPosition().y)).append("\"");
                }
                b.append("]");
            }
        }
        return b.append("}").toString();
    }

    // Canonical full-state dump. Unit->city links and per-city unit lists are
    // EXCLUDED (Java's seeded shuffles diverge; injected via "assign" instead).
    static String stateJson(GameState gs) {
        Board board = gs.getBoard();
        int size = board.getSize();
        StringBuilder b = new StringBuilder("{");

        b.append("\"tick\":").append(gs.getTick());

        // tiles: only non-default entries, keyed by pos
        b.append(",\"tiles\":{");
        boolean first = true;
        for (int x = 0; x < size; x++) for (int y = 0; y < size; y++) {
            Types.TERRAIN ter = board.getTerrainAt(x, y);
            Types.RESOURCE res = board.getResourceAt(x, y);
            Types.BUILDING bld = board.getBuildingAt(x, y);
            boolean road = board.checkTradeNetwork(x, y);
            int cityId = board.getCityIdAt(x, y);
            String cityOwner = "";
            if (cityId != -1) {
                City c = (City) gs.getActor(cityId);
                cityOwner = pos(c.getPosition().x, c.getPosition().y);
            }
            String v = ter + "|" + (res == null ? "" : res) + "|" + (bld == null ? "" : bld)
                     + "|" + (road ? 1 : 0) + "|" + cityOwner;
            if (!first) b.append(",");
            first = false;
            b.append("\"").append(pos(x, y)).append("\":\"").append(v).append("\"");
        }
        b.append("}");

        // units keyed by pos (city link excluded)
        b.append(",\"units\":{");
        first = true;
        for (int x = 0; x < size; x++) for (int y = 0; y < size; y++) {
            Unit u = board.getUnitAt(x, y);
            if (u == null) continue;
            String base = "";
            if (u.getType().isWaterUnit()) base = "" + board.getBaseLandUnit(u);
            String v = u.getType() + "|" + u.getTribeId() + "|" + u.getCurrentHP() + "|" + u.getMaxHP()
                     + "|" + u.getKills() + "|" + (u.isVeteran() ? 1 : 0) + "|" + u.getStatus() + "|" + base;
            if (!first) b.append(",");
            first = false;
            b.append("\"").append(pos(x, y)).append("\":\"").append(v).append("\"");
        }
        b.append("}");

        // cities keyed by pos (unit lists excluded; temples: level/turns via buildings)
        b.append(",\"cities\":{");
        first = true;
        for (Tribe t : gs.getTribes()) {
            for (int cityId : t.getCitiesID()) {
                City c = (City) gs.getActor(cityId);
                ArrayList<String> templeList = new ArrayList<>();
                for (Building bl : c.getBuildings()) {
                    if (bl instanceof Temple) {
                        Temple tp = (Temple) bl;
                        templeList.add(pos(bl.position.x, bl.position.y) + ":"
                                       + tp.getLevel() + ":" + tp.getTurnsToScore() + ";");
                    }
                }
                Collections.sort(templeList);
                StringBuilder temples = new StringBuilder();
                for (String tstr : templeList) temples.append(tstr);
                String v = c.getTribeId() + "|" + c.getLevel() + "|" + c.getPopulation() + "|"
                         + c.getPopulation_need() + "|" + c.getProduction() + "|" + (c.hasWalls() ? 1 : 0)
                         + "|" + c.getBound() + "|" + c.getPointsWorth() + "|" + (c.isCapital() ? 1 : 0)
                         + "|" + c.getNumUnits() + "|" + temples;
                if (!first) b.append(",");
                first = false;
                b.append("\"").append(pos(c.getPosition().x, c.getPosition().y)).append("\":\"").append(v).append("\"");
            }
        }
        b.append("}");

        // players
        b.append(",\"players\":[");
        Tribe[] tribes = gs.getTribes();
        for (int i = 0; i < tribes.length; i++) {
            Tribe t = tribes[i];
            ArrayList<String> techs = new ArrayList<>();
            for (Types.TECHNOLOGY tech : Types.TECHNOLOGY.values())
                if (t.getTechTree().isResearched(tech)) techs.add(tech.toString());
            Collections.sort(techs);
            ArrayList<String> conn = new ArrayList<>();
            for (int cityId : t.getConnectedCities()) {
                City c = (City) gs.getActor(cityId);
                conn.add(pos(c.getPosition().x, c.getPosition().y));
            }
            Collections.sort(conn);
            if (i > 0) b.append(",");
            b.append("{\"stars\":").append(t.getStars())
             .append(",\"score\":").append(t.getScore())
             .append(",\"kills\":").append(t.getnKills())
             .append(",\"numCities\":").append(t.getNumCities())
             .append(",\"techs\":").append(strArr(techs))
             .append(",\"connected\":").append(strArr(conn))
             .append(",\"result\":\"").append(t.getWinner()).append("\"}");
        }
        b.append("]");

        return b.append("}").toString();
    }
}
