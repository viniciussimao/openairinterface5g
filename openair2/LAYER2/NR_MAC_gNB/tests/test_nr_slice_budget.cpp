/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Testes da validação das cotas por slice e do cálculo do orçamento
 *        de PRBs (nr_slice_budget.c). Não dependem do resto do MAC.
 *
 * Os exemplos usam 106 PRBs (40 MHz, 30 kHz). Grupos 0 e 1 são slices com
 * cota; o último é o grupo padrão (min 0, max 100, ded 0), dos UEs cuja
 * S-NSSAI não tem cota.
 */

#include <gtest/gtest.h>

#include <cstring>

#include "nr_slice_budget.h"

namespace {

const nr_slice_ratio_t kDefault = {0, 100, 0};

nr_slice_ratio_t ratio(int min, int max, int ded)
{
  return nr_slice_ratio_t{(uint8_t)min, (uint8_t)max, (uint8_t)ded};
}

int sum(const int *v, int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    s += v[i];
  return s;
}

} // namespace

/* ---- orçamento ----------------------------------------------------------- */

// T1: os dois slices saturados, max 30/70
TEST(nr_slice_budget, dispute_30_70)
{
  const nr_slice_ratio_t r[] = {ratio(0, 30, 0), ratio(0, 70, 0), kDefault};
  const int demand[] = {200, 200, 0};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 31); // floor(106 * 0,30)
  EXPECT_EQ(budget[1], 74); // floor(106 * 0,70)
  EXPECT_EQ(budget[2], 0);
}

// T2: slice 2 ocioso; o slice 1 vai até o max dele e não além
TEST(nr_slice_budget, idle_slice_conserves_up_to_max)
{
  const nr_slice_ratio_t r[] = {ratio(0, 30, 0), ratio(0, 70, 0), kDefault};
  const int demand[] = {200, 0, 0};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 31);
  EXPECT_EQ(budget[1], 0);
  EXPECT_EQ(budget[2], 0);
}

// Demanda menor que a fatia: o resto vai para o outro slice, até o max dele
TEST(nr_slice_budget, work_conserving_below_share)
{
  const nr_slice_ratio_t r[] = {ratio(0, 50, 0), ratio(0, 100, 0), kDefault};
  const int demand[] = {10, 200, 0};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 10);
  EXPECT_EQ(budget[1], 96);
}

// T3: o min do slice menor é respeitado sob disputa
TEST(nr_slice_budget, min_under_dispute)
{
  const nr_slice_ratio_t r[] = {ratio(20, 30, 0), ratio(0, 100, 0), kDefault};
  const int demand[] = {200, 200, 0};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_GE(budget[0], 21); // floor(106 * 0,20)
  EXPECT_EQ(budget[0], 31); // e chega ao max, porque há PRBs sobrando
  EXPECT_EQ(budget[1], 75);
}

// O min vem antes da divisão do resto: com min 50 e max 50, o slice tem
// os 50% mesmo disputando com um slice de max 100
TEST(nr_slice_budget, min_reserved_before_sharing)
{
  const nr_slice_ratio_t r[] = {ratio(50, 50, 0), ratio(0, 100, 0), kDefault};
  const int demand[] = {200, 200, 0};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 53);
  EXPECT_EQ(budget[1], 53);
}

// O min é limitado pela demanda: o que o slice não pede volta para os outros
TEST(nr_slice_budget, min_limited_by_demand)
{
  const nr_slice_ratio_t r[] = {ratio(40, 100, 0), ratio(0, 100, 0), kDefault};
  const int demand[] = {10, 200, 0};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 10);
  EXPECT_EQ(budget[1], 96);
}

// T4: dedicados sem demanda não são reaproveitados
TEST(nr_slice_budget, dedicated_without_demand_is_not_reused)
{
  const nr_slice_ratio_t r[] = {ratio(20, 20, 20), ratio(0, 100, 0), kDefault};
  const int demand[] = {0, 200, 0};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 0);
  EXPECT_EQ(budget[1], 85); // 106 - floor(106 * 0,20)
  EXPECT_EQ(sum(budget, 3), 85);
}

// Dedicados com demanda pequena: o slice fica com os dedicados inteiros
TEST(nr_slice_budget, dedicated_with_small_demand)
{
  const nr_slice_ratio_t r[] = {ratio(20, 20, 20), ratio(0, 100, 0), kDefault};
  const int demand[] = {5, 200, 0};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 21);
  EXPECT_EQ(budget[1], 85);
}

// Sobra do arredondamento: 1 PRB por grupo, a partir de rr_start
TEST(nr_slice_budget, rounding_leftover_round_robin)
{
  const nr_slice_ratio_t r[] = {ratio(0, 100, 0), ratio(0, 100, 0), ratio(0, 100, 0)};
  const int demand[] = {50, 50, 50};
  int budget[3];

  nr_slice_compute_budget(10, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 4);
  EXPECT_EQ(budget[1], 3);
  EXPECT_EQ(budget[2], 3);

  nr_slice_compute_budget(10, 3, r, demand, 1, budget);
  EXPECT_EQ(budget[0], 3);
  EXPECT_EQ(budget[1], 4);
  EXPECT_EQ(budget[2], 3);
}

// A sobra do arredondamento não passa do max_ratio
TEST(nr_slice_budget, rounding_leftover_respects_max)
{
  const nr_slice_ratio_t r[] = {ratio(0, 50, 0), ratio(0, 50, 0), kDefault};
  const int demand[] = {50, 50, 0};
  int budget[3];
  nr_slice_compute_budget(7, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 3); // floor(7 * 0,50)
  EXPECT_EQ(budget[1], 3);
  EXPECT_EQ(sum(budget, 3), 6); // 1 PRB fica sem uso
}

// N pequeno: um slice pode ficar com menos de 5 PRBs; a política de
// alocação então só atende retransmissões nesse slice
TEST(nr_slice_budget, small_n_below_min_allocation)
{
  const nr_slice_ratio_t r[] = {ratio(0, 30, 0), ratio(0, 70, 0), kDefault};
  const int demand[] = {50, 50, 0};
  int budget[3];
  nr_slice_compute_budget(8, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 2);
  EXPECT_EQ(budget[1], 5);
  EXPECT_LE(sum(budget, 3), 8);
}

// UE sem slice: o grupo padrão (max 100) recebe o que os slices não usam
TEST(nr_slice_budget, default_group_takes_leftover)
{
  const nr_slice_ratio_t r[] = {ratio(0, 30, 0), ratio(0, 70, 0), kDefault};
  const int demand[] = {200, 0, 200};
  int budget[3];
  nr_slice_compute_budget(106, 3, r, demand, 0, budget);
  EXPECT_EQ(budget[0], 31);
  EXPECT_EQ(budget[1], 0);
  EXPECT_EQ(budget[2], 75);
}

// Nunca distribui mais PRBs do que existem
TEST(nr_slice_budget, never_exceeds_n_prb)
{
  const nr_slice_ratio_t r[] = {ratio(30, 60, 10), ratio(40, 90, 20), kDefault};
  for (int n_prb = 0; n_prb <= 273; n_prb++) {
    for (int d = 0; d <= 300; d += 37) {
      const int demand[] = {d, 300 - d, d / 2};
      int budget[3];
      nr_slice_compute_budget(n_prb, 3, r, demand, n_prb % 3, budget);
      EXPECT_LE(sum(budget, 3), n_prb);
      for (int g = 0; g < 3; g++) {
        EXPECT_GE(budget[g], 0);
        if (demand[g] == 0)
          EXPECT_EQ(budget[g], 0);
      }
      EXPECT_LE(budget[0], n_prb * 60 / 100);
      EXPECT_LE(budget[1], n_prb * 90 / 100);
    }
  }
}

/* ---- validação ----------------------------------------------------------- */

TEST(nr_slice_policy_validate, accepts_valid_list)
{
  const nr_slice_policy_t p[] = {{1, 0x000001, ratio(10, 30, 5)}, {1, 0x000002, ratio(20, 70, 0)}};
  char err[128] = {0};
  EXPECT_TRUE(nr_slice_policy_validate(p, 2, err, sizeof(err))) << err;
  EXPECT_TRUE(nr_slice_policy_validate(nullptr, 0, err, sizeof(err)));
}

TEST(nr_slice_policy_validate, rejects_bad_ratios)
{
  char err[128];
  const nr_slice_policy_t ded_gt_min[] = {{1, 1, ratio(10, 30, 20)}};
  EXPECT_FALSE(nr_slice_policy_validate(ded_gt_min, 1, err, sizeof(err)));
  EXPECT_NE(std::strstr(err, "dedicated_ratio"), nullptr);

  const nr_slice_policy_t min_gt_max[] = {{1, 1, ratio(40, 30, 0)}};
  EXPECT_FALSE(nr_slice_policy_validate(min_gt_max, 1, err, sizeof(err)));
  EXPECT_NE(std::strstr(err, "min_ratio"), nullptr);

  const nr_slice_policy_t max_gt_100[] = {{1, 1, ratio(0, 101, 0)}};
  EXPECT_FALSE(nr_slice_policy_validate(max_gt_100, 1, err, sizeof(err)));
}

TEST(nr_slice_policy_validate, rejects_sum_of_min_above_100)
{
  const nr_slice_policy_t p[] = {{1, 1, ratio(60, 100, 0)}, {1, 2, ratio(50, 100, 0)}};
  char err[128];
  EXPECT_FALSE(nr_slice_policy_validate(p, 2, err, sizeof(err)));
  EXPECT_NE(std::strstr(err, "soma"), nullptr);
}

TEST(nr_slice_policy_validate, rejects_duplicates_and_bad_sd)
{
  char err[128];
  const nr_slice_policy_t dup[] = {{1, 1, ratio(0, 50, 0)}, {1, 1, ratio(0, 50, 0)}};
  EXPECT_FALSE(nr_slice_policy_validate(dup, 2, err, sizeof(err)));

  const nr_slice_policy_t bad_sd[] = {{1, 0x1000000, ratio(0, 50, 0)}};
  EXPECT_FALSE(nr_slice_policy_validate(bad_sd, 1, err, sizeof(err)));

  // mesmo SST com SD diferente é outro slice
  const nr_slice_policy_t same_sst[] = {{1, 1, ratio(0, 50, 0)}, {1, 2, ratio(0, 50, 0)}};
  EXPECT_TRUE(nr_slice_policy_validate(same_sst, 2, err, sizeof(err))) << err;
}

TEST(nr_slice_policy_validate, rejects_too_many_slices)
{
  nr_slice_policy_t p[NR_SLICE_MAX_POLICIES + 1];
  for (int i = 0; i <= NR_SLICE_MAX_POLICIES; i++)
    p[i] = nr_slice_policy_t{1, (uint32_t)i, ratio(0, 10, 0)};
  char err[128];
  EXPECT_TRUE(nr_slice_policy_validate(p, NR_SLICE_MAX_POLICIES, err, sizeof(err))) << err;
  EXPECT_FALSE(nr_slice_policy_validate(p, NR_SLICE_MAX_POLICIES + 1, err, sizeof(err)));
}
