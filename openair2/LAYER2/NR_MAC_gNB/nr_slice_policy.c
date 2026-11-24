/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Escalonamento DL por slice. Veja nr_slice_policy.h.
 *
 * O estado fica neste arquivo, e não em gNB_MAC_INST, para o patch não
 * mexer em nr_mac_gNB.h. Há uma única instância do MAC no gNB. Todo acesso
 * ao estado acontece sob o sched_lock: a API o toma, e o escalonador já o
 * segura quando chama a política de alocação.
 */

#include <string.h>

#include "nr_slice_policy.h"
#include "common/utils/LOG/log.h"
#include "common/utils/assertions.h"
#include "common/utils/nr/nr_common.h"
#include "gNB_scheduler_dlsch_default_policies.h"
#include "mac_proto.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.h"

/// Menor alocação de dados novos do proportional fair do OAI
#define SLICE_MIN_RB 5
/// Período do log de PRBs por slice, em frames (100 frames = 1 s)
#define SLICE_LOG_PERIOD_FRAMES 100

typedef struct {
  rnti_t rnti;
  uint32_t total_rbs; ///< UE->mac_stats.dl.total_rbs na última leitura
  uint32_t total_rbs_retx; ///< UE->mac_stats.dl.total_rbs_retx na última leitura
} slice_ue_counter_t;

static struct {
  nr_slice_policy_t policy[NR_SLICE_MAX_POLICIES];
  int n; ///< cotas em vigor; 0 = desligado
  nr_dl_rb_alloc_fn saved_rb_alloc; ///< política anterior, restaurada com n == 0
  int rr; ///< slice que chama o proportional fair primeiro no próximo slot

  /* log periódico */
  bool log_started;
  frame_t log_frame; ///< frame do último log
  uint64_t avail_prb; ///< PRBs disponíveis somados nos slots do período
  slice_ue_counter_t ue[MAX_MOBILES_PER_GNB];
  int n_ue;
} slice_state;

/// Candidatos reordenados por slice (o escalonador roda sob o sched_lock)
static nr_dl_candidate_t slice_sorted[MAX_MOBILES_PER_GNB];

/* ---- API ---------------------------------------------------------------- */

int nr_slice_policy_set(gNB_MAC_INST *mac, const nr_slice_policy_t *policies, int n, char *err, size_t err_len)
{
  if (!nr_slice_policy_validate(policies, n, err, err_len))
    return -1;

  NR_SCHED_LOCK(&mac->sched_lock);
  if (n > 0) {
    memcpy(slice_state.policy, policies, n * sizeof(*policies));
    if (slice_state.n == 0) {
      slice_state.saved_rb_alloc = mac->dl_rb_alloc;
      mac->dl_rb_alloc = nr_dl_slice_rb_alloc;
    }
  } else if (slice_state.n > 0) {
    mac->dl_rb_alloc = slice_state.saved_rb_alloc;
  }
  slice_state.n = n;
  slice_state.rr = 0;
  slice_state.log_started = false;
  NR_SCHED_UNLOCK(&mac->sched_lock);

  if (n == 0) {
    LOG_I(NR_MAC, "[SLICE] cotas por slice desligadas: DL volta à política padrão\n");
    return 0;
  }
  for (int i = 0; i < n; i++) {
    const nr_slice_policy_t *p = &policies[i];
    LOG_I(NR_MAC,
          "[SLICE] cota DL do slice %d.%06x: min_ratio %d, max_ratio %d, dedicated_ratio %d\n",
          p->sst,
          p->sd,
          p->ratio.min_ratio,
          p->ratio.max_ratio,
          p->ratio.dedicated_ratio);
  }
  return 0;
}

int nr_slice_policy_get(gNB_MAC_INST *mac, nr_slice_policy_t *out, int max)
{
  NR_SCHED_LOCK(&mac->sched_lock);
  const int n = slice_state.n;
  const int copy = n < max ? n : max;
  if (copy > 0)
    memcpy(out, slice_state.policy, copy * sizeof(*out));
  NR_SCHED_UNLOCK(&mac->sched_lock);
  return n;
}

/* ---- Auxiliares --------------------------------------------------------- */

/// S-NSSAI do primeiro DRB do UE, como em collect_dl_candidates()
static nssai_t ue_nssai(const NR_UE_info_t *UE)
{
  const seq_arr_t *lcs = &UE->UE_sched_ctrl.lc_config;
  for (size_t j = 0; j < seq_arr_size(lcs); j++) {
    const nr_lc_config_t *c = seq_arr_at(lcs, j);
    if (c->lcid >= 4)
      return c->nssai;
  }
  return (nssai_t){.sst = 0, .sd = NR_SLICE_SD_NONE};
}

/// Grupo da S-NSSAI: o índice da cota, ou slice_state.n (grupo padrão)
static int slice_group(nssai_t nssai)
{
  for (int i = 0; i < slice_state.n; i++) {
    if (slice_state.policy[i].sst == nssai.sst && slice_state.policy[i].sd == nssai.sd)
      return i;
  }
  return slice_state.n;
}

/// PRBs que o candidato pediria, como o proportional fair estima
static int candidate_demand(const nr_dl_sched_params_t *params, const nr_dl_candidate_t *cand, int max_rb)
{
  if (cand->is_retx)
    return cand->retx_rbSize;
  if (cand->pending_bytes == 0)
    return SLICE_MIN_RB; // só CE da MAC (TA, troca de feixe)

  const int mcs = cand->sched_pdsch.mcs;
  const uint8_t Qm = nr_get_Qm_dl(mcs, cand->mcs_table);
  const uint16_t R = nr_get_code_rate_dl(mcs, cand->mcs_table);
  const NR_pdsch_dmrs_t dmrs = get_dl_dmrs_params(params->cell->common_channels.ServingCellConfigCommon,
                                                  &cand->UE->current_DL_BWP,
                                                  &cand->sched_pdsch.tda_info,
                                                  cand->sched_pdsch.nrOfLayers);
  const int oh = 3 * 4 + (cand->UE->UE_sched_ctrl.ta_apply ? 2 : 0);
  uint32_t tbs = 0;
  uint16_t rbs = 0;
  nr_find_nb_rb(Qm,
                R,
                1,
                cand->sched_pdsch.nrOfLayers,
                cand->sched_pdsch.tda_info.nrOfSymbols,
                dmrs.N_PRB_DMRS * dmrs.N_DMRS_SLOT,
                cand->pending_bytes + oh,
                SLICE_MIN_RB,
                max_rb,
                &tbs,
                &rbs);
  return rbs;
}

/// PRBs livres para PDSCH no BWP do candidato, com os símbolos do TDA dele
static int free_prbs(const nr_dl_sched_params_t *params, const nr_dl_candidate_t *cand)
{
  const uint16_t *vrb_map = params->vrb_map[cand->alloc_beam_idx];
  int n = 0;
  for (int rb = cand->bwp_start; rb < cand->bwp_start + cand->bwp_size; rb++)
    if ((vrb_map[rb] & cand->alloc_slbitmap) == 0)
      n++;
  return n;
}

static slice_ue_counter_t *find_counter(rnti_t rnti)
{
  for (int i = 0; i < slice_state.n_ue; i++)
    if (slice_state.ue[i].rnti == rnti)
      return &slice_state.ue[i];
  return NULL;
}

/* ---- Log periódico ------------------------------------------------------ */

/** Escreve, cerca de uma vez por segundo, os PRBs DL usados por slice e por
 *  UE (novas transmissões e retransmissões separadas) e os PRBs sem uso.
 *  Os números vêm de UE->mac_stats.dl, que contam o rbSize final de cada
 *  PDSCH. "Disponíveis" soma os PRBs livres dos slots em que a política
 *  rodou; slots sem nenhum candidato não entram. */
static void slice_log(gNB_MAC_INST *mac, frame_t frame)
{
  if (!slice_state.log_started) {
    slice_state.log_started = true;
    slice_state.log_frame = frame;
    slice_state.avail_prb = 0;
    slice_state.n_ue = 0;
    FOR_EACH_CONNECTED_UE(&mac->UE_info, UE) {
      if (slice_state.n_ue >= MAX_MOBILES_PER_GNB)
        break;
      slice_state.ue[slice_state.n_ue++] = (slice_ue_counter_t){.rnti = UE->rnti,
                                                                .total_rbs = UE->mac_stats.dl.total_rbs,
                                                                .total_rbs_retx = UE->mac_stats.dl.total_rbs_retx};
    }
    return;
  }

  const int elapsed = (frame - slice_state.log_frame + 1024) % 1024;
  if (elapsed < SLICE_LOG_PERIOD_FRAMES)
    return;

  const int n_groups = slice_state.n + 1;
  uint64_t g_new[NR_SLICE_MAX_GROUPS] = {0};
  uint64_t g_retx[NR_SLICE_MAX_GROUPS] = {0};
  slice_ue_counter_t now[MAX_MOBILES_PER_GNB];
  int n_now = 0;

  /* primeira passada: soma por slice e guarda os contadores novos */
  FOR_EACH_CONNECTED_UE(&mac->UE_info, UE) {
    if (n_now >= MAX_MOBILES_PER_GNB)
      break;
    const NR_mac_dir_stats_t *st = &UE->mac_stats.dl;
    const slice_ue_counter_t *prev = find_counter(UE->rnti);
    /* UE novo no período: começa a contar a partir de agora */
    const uint32_t d_new = prev ? st->total_rbs - prev->total_rbs : 0;
    const uint32_t d_retx = prev ? st->total_rbs_retx - prev->total_rbs_retx : 0;
    const int g = slice_group(ue_nssai(UE));
    g_new[g] += d_new;
    g_retx[g] += d_retx;
    now[n_now++] = (slice_ue_counter_t){.rnti = UE->rnti, .total_rbs = st->total_rbs, .total_rbs_retx = st->total_rbs_retx};
  }

  uint64_t used = 0;
  for (int g = 0; g < n_groups; g++)
    used += g_new[g] + g_retx[g];
  const uint64_t avail = slice_state.avail_prb;
  const uint64_t unused = avail > used ? avail - used : 0;

  LOG_I(NR_MAC,
        "[SLICE] período %.2f s: PRBs DL disponíveis %lu, usados %lu, sem uso %lu\n",
        elapsed / 100.0,
        (unsigned long)avail,
        (unsigned long)used,
        (unsigned long)unused);
  for (int g = 0; g < n_groups; g++) {
    const uint64_t total = g_new[g] + g_retx[g];
    const double share = used > 0 ? 100.0 * total / used : 0.0;
    if (g < slice_state.n) {
      const nr_slice_policy_t *p = &slice_state.policy[g];
      LOG_I(NR_MAC,
            "[SLICE]   slice %d.%06x (min %d, max %d, ded %d): %lu PRBs (%.1f%% dos usados), novas %lu, retx %lu\n",
            p->sst,
            p->sd,
            p->ratio.min_ratio,
            p->ratio.max_ratio,
            p->ratio.dedicated_ratio,
            (unsigned long)total,
            share,
            (unsigned long)g_new[g],
            (unsigned long)g_retx[g]);
    } else if (total > 0) {
      LOG_I(NR_MAC,
            "[SLICE]   grupo padrão (sem cota): %lu PRBs (%.1f%% dos usados), novas %lu, retx %lu\n",
            (unsigned long)total,
            share,
            (unsigned long)g_new[g],
            (unsigned long)g_retx[g]);
    }
  }

  /* segunda passada: uma linha por UE */
  FOR_EACH_CONNECTED_UE(&mac->UE_info, UE) {
    const slice_ue_counter_t *prev = find_counter(UE->rnti);
    if (prev == NULL)
      continue;
    const NR_mac_dir_stats_t *st = &UE->mac_stats.dl;
    const uint32_t d_new = st->total_rbs - prev->total_rbs;
    const uint32_t d_retx = st->total_rbs_retx - prev->total_rbs_retx;
    const nssai_t nssai = ue_nssai(UE);
    LOG_I(NR_MAC,
          "[SLICE]     UE %04x (slice %d.%06x): %u PRBs, novas %u, retx %u\n",
          UE->rnti,
          nssai.sst,
          nssai.sd,
          d_new + d_retx,
          d_new,
          d_retx);
  }

  memcpy(slice_state.ue, now, n_now * sizeof(*now));
  slice_state.n_ue = n_now;
  slice_state.avail_prb = 0;
  slice_state.log_frame = frame;
}

/* ---- Política de alocação ----------------------------------------------- */

int nr_dl_slice_rb_alloc(const nr_dl_sched_params_t *params, nr_dl_candidate_t *candidates, int n_candidates)
{
  const int n_groups = slice_state.n + 1;
  nr_slice_ratio_t ratio[NR_SLICE_MAX_GROUPS];
  int demand[NR_SLICE_MAX_GROUPS] = {0};
  int budget[NR_SLICE_MAX_GROUPS] = {0};
  int count[NR_SLICE_MAX_GROUPS] = {0};
  int first[NR_SLICE_MAX_GROUPS] = {0};
  int group_of[MAX_MOBILES_PER_GNB];

  for (int g = 0; g < slice_state.n; g++)
    ratio[g] = slice_state.policy[g].ratio;
  ratio[slice_state.n] = (nr_slice_ratio_t){.min_ratio = 0, .max_ratio = 100, .dedicated_ratio = 0};

  if (n_candidates > MAX_MOBILES_PER_GNB)
    n_candidates = MAX_MOBILES_PER_GNB;

  /* PRBs disponíveis no slot: os livres no BWP do primeiro candidato ativo.
   * No laboratório, todos os UEs usam o mesmo BWP e o mesmo TDA. */
  int n_prb = 0;
  for (int i = 0; i < n_candidates; i++) {
    if (!candidates[i].skipped) {
      n_prb = free_prbs(params, &candidates[i]);
      break;
    }
  }
  if (n_prb == 0)
    return 0;

  /* demanda e tamanho de cada slice */
  for (int i = 0; i < n_candidates; i++) {
    const int g = slice_group(candidates[i].nssai);
    group_of[i] = g;
    count[g]++;
    if (!candidates[i].skipped)
      demand[g] += candidate_demand(params, &candidates[i], n_prb);
  }

  nr_slice_compute_budget(n_prb, n_groups, ratio, demand, slice_state.rr, budget);

  /* reordena os candidatos por slice (contagem), para cada slice ocupar um
   * trecho contínuo, como o proportional fair espera */
  for (int g = 1; g < n_groups; g++)
    first[g] = first[g - 1] + count[g - 1];
  int fill[NR_SLICE_MAX_GROUPS];
  memcpy(fill, first, sizeof(fill));
  for (int i = 0; i < n_candidates; i++)
    slice_sorted[fill[group_of[i]]++] = candidates[i];
  memcpy(candidates, slice_sorted, n_candidates * sizeof(*candidates));

  int n_scheduled = 0;
  for (int k = 0; k < n_groups; k++) {
    const int g = (slice_state.rr + k) % n_groups;
    if (demand[g] == 0 || count[g] == 0)
      continue;
    const int remaining = params->max_num_ue - n_scheduled;
    if (remaining <= 0)
      break;

    nr_dl_candidate_t *slice_cands = candidates + first[g];
    int active = 0;
    for (int i = 0; i < count[g]; i++)
      if (!slice_cands[i].skipped)
        active++;
    if (active == 0)
      continue;

    nr_dl_sched_params_t p = *params;
    p.max_num_ue = remaining < active ? remaining : active;

    /* Orçamento menor que a menor alocação do proportional fair: o slice só
     * atende retransmissões, que pedem o mesmo número de PRBs da original.
     * Os outros candidatos ficam de fora neste slot. */
    bool was_skipped[MAX_MOBILES_PER_GNB];
    const bool retx_only = budget[g] < SLICE_MIN_RB;
    for (int i = 0; i < count[g]; i++) {
      was_skipped[i] = slice_cands[i].skipped;
      if (retx_only && !slice_cands[i].is_retx)
        slice_cands[i].skipped = true;
    }
    const int rb_limit = retx_only ? SLICE_MIN_RB : budget[g];
    for (int b = 0; b < p.num_beams; b++)
      p.n_rb_avail[b] = rb_limit;

    n_scheduled += nr_dl_proportional_fair(&p, slice_cands, count[g]);

    for (int i = 0; i < count[g]; i++)
      slice_cands[i].skipped = was_skipped[i];
  }

  slice_state.rr = (slice_state.rr + 1) % n_groups;
  slice_state.avail_prb += n_prb;
  slice_log(params->mac, params->frame);
  return n_scheduled;
}
