// PufferLib 4.0 binding for polytopia. Lives at ocean/polytopia/binding.c in
// a PufferLib checkout (with polytopia_env.h and core/ alongside).
#define OBS_SIZE 1272          // literal for build tooling; asserted below
#include "polytopia_env.h"

#define NUM_ATNS 1
#define ACT_SIZES {197}
#define OBS_TENSOR_T ByteTensor
#define MY_ACTION_MASK 197
#define MY_USES_PERM
#define MY_USES_TAGS
#define Env PolyEnv
#include "vecenv.h"

_Static_assert(OBS_SIZE == POLY_OBS_SIZE, "OBS_SIZE literal drifted from env layout");
_Static_assert(ACTION_N == 197, "ACT_SIZES/MY_ACTION_MASK literals drifted from ACTION_N");

// perm-aware per-slot buffer wiring (self-play seat swaps), chess pattern
void my_setup_perm(StaticVec* vec, Env* env, int slot_base) {
    size_t obs_elem = obs_element_size();
    for (int s = 0; s < env->num_agents; s++) {
        int phys = vec->agent_perm ? vec->agent_perm[slot_base + s] : (slot_base + s);
        env->obs_ptr[s] = (uint8_t*)vec->observations + (size_t)phys * OBS_SIZE * obs_elem;
        env->mask_ptr[s] = vec->action_mask + (size_t)phys * MY_ACTION_MASK;
        env->action_ptr[s] = vec->actions + (size_t)phys * NUM_ATNS;
        env->reward_ptr[s] = vec->rewards + phys;
        env->terminal_ptr[s] = vec->terminals + phys;
    }
}

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = ENV_PLAYERS;
    DictItem* it;
    if ((it = dict_get_unsafe(kwargs, "shaping")) != NULL)
        env->shaping = (float)it->value;
    if ((it = dict_get_unsafe(kwargs, "max_episode_steps")) != NULL)
        env->max_episode_steps = (int32_t)it->value;
    env->fog = 1;   // fog of war on by default
    if ((it = dict_get_unsafe(kwargs, "fog")) != NULL)
        env->fog = (int)it->value;

    // distinct per-env rng streams (envs are created sequentially)
    static uint32_t env_counter = 0;
    uint32_t base = 0x2545f491u;
    if ((it = dict_get_unsafe(kwargs, "seed")) != NULL) base = (uint32_t)it->value;
    env->rng = base * 2654435761u + (++env_counter) * 0x9e3779b9u;
    if (env->rng == 0) env->rng = 1;
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "score", log->score);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "ticks", log->ticks);
    dict_set(out, "p0_winrate", log->p0_winrate);
}
