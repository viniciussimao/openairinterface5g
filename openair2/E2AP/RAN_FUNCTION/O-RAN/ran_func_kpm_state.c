/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Estado do E2SM-KPM por assinatura e por UE. Veja ran_func_kpm_state.h.
 */

#include "ran_func_kpm_state.h"

#include <stdbool.h>
#include <string.h>

struct kpm_sub_state {
  const void *key; ///< act_def da assinatura (NULL = entrada livre)
  uint64_t last_use; ///< para escolher a entrada usada há mais tempo
  e2_node_level_stats_t node_stats[2];
  bool used[MAX_MOBILES_PER_GNB]; ///< slot local ocupado
  uint32_t ue_id[MAX_MOBILES_PER_GNB]; ///< rrc_ue_id do slot local
};

static kpm_sub_state_t subs[KPM_STATE_MAX_SUBS];
static uint64_t use_tick;

kpm_sub_state_t *kpm_state_get_sub(const void *key)
{
  kpm_sub_state_t *victim = &subs[0];
  for (size_t i = 0; i < KPM_STATE_MAX_SUBS; i++) {
    if (subs[i].key == key) {
      subs[i].last_use = ++use_tick;
      return &subs[i];
    }
    if (subs[i].key == NULL) {
      if (victim->key != NULL)
        victim = &subs[i];
    } else if (victim->key != NULL && subs[i].last_use < victim->last_use) {
      victim = &subs[i];
    }
  }

  /* assinatura nova: entrada livre ou a usada há mais tempo, zerada; os
   * slots dela são zerados quando os UEs forem associados */
  memset(victim, 0, sizeof(*victim));
  victim->key = key;
  victim->last_use = ++use_tick;
  return victim;
}

e2_node_level_stats_t *kpm_state_node_stats(kpm_sub_state_t *sub)
{
  return sub->node_stats;
}

void kpm_state_map_ues(kpm_sub_state_t *sub, const uint32_t *ue_ids, size_t n, size_t *slots)
{
  const size_t base = (size_t)(sub - subs) * MAX_MOBILES_PER_GNB;
  bool keep[MAX_MOBILES_PER_GNB] = {0};
  bool found[MAX_MOBILES_PER_GNB] = {0};

  if (n > MAX_MOBILES_PER_GNB)
    n = MAX_MOBILES_PER_GNB;

  /* 1. UEs que já tinham slot */
  for (size_t i = 0; i < n; i++) {
    for (size_t s = 0; s < MAX_MOBILES_PER_GNB; s++) {
      if (sub->used[s] && !keep[s] && sub->ue_id[s] == ue_ids[i]) {
        keep[s] = true;
        found[i] = true;
        slots[i] = base + s;
        break;
      }
    }
  }

  /* 2. slots de UEs que saíram ficam livres */
  for (size_t s = 0; s < MAX_MOBILES_PER_GNB; s++)
    if (!keep[s])
      sub->used[s] = false;

  /* 3. UEs novos recebem um slot livre, com os contadores zerados */
  for (size_t i = 0; i < n; i++) {
    if (found[i])
      continue;
    for (size_t s = 0; s < MAX_MOBILES_PER_GNB; s++) {
      if (!sub->used[s]) {
        sub->used[s] = true;
        sub->ue_id[s] = ue_ids[i];
        slots[i] = base + s;
        reset_kpm_ue_counters(slots[i]);
        break;
      }
    }
  }
}
