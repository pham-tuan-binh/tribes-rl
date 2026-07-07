// Agent policies for the website.
// RandomAgent: uniform over the legal-action mask (placeholder until trained
// weights are exported — the interface stays the same: pick(game, player)).
export class RandomAgent {
  constructor(endTurnBias = 0.12) { this.endTurnBias = endTurnBias; }

  pick(game, player) {
    const legal = game.legalActions(player);
    if (legal.length === 0) return null;
    // small bias toward ending the turn so games keep moving
    const endTurn = game.tiles + 0;   // ENV_TILES + V_END_TURN
    if (legal.includes(endTurn) && Math.random() < this.endTurnBias) return endTurn;
    return legal[(Math.random() * legal.length) | 0];
  }
}

// Trained policy: puffernet forward pass inside the WASM module (masked
// softmax sampling over the same 197-action head used in training).
export class TrainedAgent {
  pick(game, player) {
    const a = game.agentAct(player);
    return a >= 0 ? a : null;
  }
}
