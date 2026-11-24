/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Validação das cotas por slice e cálculo do orçamento de PRBs.
 *        Veja nr_slice_budget.h.
 */

#include "nr_slice_budget.h"

#include <stdarg.h>
#include <stdio.h>

static bool fail(char *err, size_t err_len, const char *fmt, ...)
{
  if (err != NULL && err_len > 0) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
  }
  return false;
}

bool nr_slice_policy_validate(const nr_slice_policy_t *policies, int n, char *err, size_t err_len)
{
  if (n < 0 || n > NR_SLICE_MAX_POLICIES)
    return fail(err, err_len, "número de slices %d fora de 0..%d", n, NR_SLICE_MAX_POLICIES);
  if (n > 0 && policies == NULL)
    return fail(err, err_len, "lista de cotas nula");

  int sum_min = 0;
  for (int i = 0; i < n; i++) {
    const nr_slice_policy_t *p = &policies[i];
    const nr_slice_ratio_t *r = &p->ratio;
    if (p->sd > NR_SLICE_SD_NONE)
      return fail(err, err_len, "slice %d: sd 0x%x maior que 0xffffff", i, p->sd);
    if (r->max_ratio > 100)
      return fail(err, err_len, "slice %d.%06x: max_ratio %d maior que 100", p->sst, p->sd, r->max_ratio);
    if (r->min_ratio > r->max_ratio)
      return fail(err, err_len, "slice %d.%06x: min_ratio %d maior que max_ratio %d", p->sst, p->sd, r->min_ratio, r->max_ratio);
    if (r->dedicated_ratio > r->min_ratio)
      return fail(err,
                  err_len,
                  "slice %d.%06x: dedicated_ratio %d maior que min_ratio %d",
                  p->sst,
                  p->sd,
                  r->dedicated_ratio,
                  r->min_ratio);
    for (int j = 0; j < i; j++) {
      if (policies[j].sst == p->sst && policies[j].sd == p->sd)
        return fail(err, err_len, "slice %d.%06x repetido", p->sst, p->sd);
    }
    sum_min += r->min_ratio;
  }
  if (sum_min > 100)
    return fail(err, err_len, "soma dos min_ratio (%d) maior que 100", sum_min);
  return true;
}

static int min_int(int a, int b)
{
  return a < b ? a : b;
}

static int max_int(int a, int b)
{
  return a > b ? a : b;
}

void nr_slice_compute_budget(int n_prb,
                             int n_groups,
                             const nr_slice_ratio_t *ratio,
                             const int *demand,
                             int rr_start,
                             int *budget)
{
  int cap[NR_SLICE_MAX_GROUPS];
  int pool = max_int(n_prb, 0);

  if (n_groups <= 0)
    return;
  if (n_groups > NR_SLICE_MAX_GROUPS)
    n_groups = NR_SLICE_MAX_GROUPS;

  /* 1. Dedicados: saem do conjunto comum para todos os grupos, com ou sem
   *    demanda. Um grupo sem demanda não usa os dele, e ninguém mais usa. */
  for (int g = 0; g < n_groups; g++) {
    budget[g] = 0;
    cap[g] = 0;
    pool -= n_prb * ratio[g].dedicated_ratio / 100;
  }
  pool = max_int(pool, 0);

  /* 2. Mínimo de cada grupo com demanda, limitado à demanda. Os dedicados
   *    já estão reservados; aqui só sai do conjunto a parte do mínimo que
   *    passa deles. */
  for (int g = 0; g < n_groups; g++) {
    if (demand[g] <= 0)
      continue;
    const int ded = n_prb * ratio[g].dedicated_ratio / 100;
    const int min = n_prb * ratio[g].min_ratio / 100;
    const int max = n_prb * ratio[g].max_ratio / 100;
    const int above_ded = max_int(min_int(min, demand[g]) - ded, 0);
    const int take = min_int(above_ded, pool);
    pool -= take;
    budget[g] = ded + take;
    /* teto: o max_ratio, sem passar da demanda (conservação de trabalho),
     * mas nunca abaixo do que o grupo já recebeu */
    cap[g] = max_int(budget[g], min_int(max, demand[g]));
  }

  /* 3. Preenchimento até o nível: partes iguais entre os grupos ainda
   *    abaixo do teto, até o conjunto acabar ou todos chegarem ao teto. */
  while (pool > 0) {
    int active = 0;
    for (int g = 0; g < n_groups; g++)
      if (demand[g] > 0 && budget[g] < cap[g])
        active++;
    if (active == 0)
      break;

    const int share = pool / active;
    if (share == 0) {
      /* sobra menor que o número de grupos: 1 PRB por grupo, em round-robin */
      for (int k = 0; k < n_groups && pool > 0; k++) {
        const int g = (rr_start + k) % n_groups;
        if (demand[g] > 0 && budget[g] < cap[g]) {
          budget[g]++;
          pool--;
        }
      }
      continue;
    }

    for (int g = 0; g < n_groups; g++) {
      if (demand[g] <= 0 || budget[g] >= cap[g])
        continue;
      const int give = min_int(share, cap[g] - budget[g]);
      budget[g] += give;
      pool -= give;
    }
  }
}
