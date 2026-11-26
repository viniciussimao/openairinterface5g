/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Testes da conversão da ação "Slice-level PRB quota" do E2SM-RC
 *        (ran_func_rc_slice.c). Não dependem do MAC nem do RRC: montam a
 *        mensagem de CONTROL na mão, com a árvore de 8.4.3.6.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <vector>

#include "ran_func_rc_slice.h"

extern "C" {
#include "openair2/E2AP/flexric/src/sm/rc_sm/ie/ir/lst_ran_param.h"
#include "openair2/E2AP/flexric/src/sm/rc_sm/ie/ir/ran_param_list.h"
#include "openair2/E2AP/flexric/src/sm/rc_sm/ie/ir/ran_param_struct.h"
#include "openair2/E2AP/flexric/src/sm/rc_sm/ie/ir/ran_parameter_value.h"
}

namespace {

/* As mensagens dos testes vivem até o fim do processo */
std::deque<std::vector<uint8_t>> g_bufs;

ran_parameter_value_t *new_value()
{
  auto *v = new ran_parameter_value_t;
  std::memset(v, 0, sizeof(*v));
  return v;
}

seq_ran_param_t elem_int(uint32_t id, int64_t x)
{
  seq_ran_param_t p;
  std::memset(&p, 0, sizeof(p));
  p.ran_param_id = id;
  p.ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  p.ran_param_val.flag_false = new_value();
  p.ran_param_val.flag_false->type = INTEGER_RAN_PARAMETER_VALUE;
  p.ran_param_val.flag_false->int_ran = x;
  return p;
}

seq_ran_param_t elem_oct(uint32_t id, std::vector<uint8_t> bytes)
{
  g_bufs.push_back(std::move(bytes));
  seq_ran_param_t p;
  std::memset(&p, 0, sizeof(p));
  p.ran_param_id = id;
  p.ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
  p.ran_param_val.flag_true = new_value();
  p.ran_param_val.flag_true->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  p.ran_param_val.flag_true->octet_str_ran.len = g_bufs.back().size();
  p.ran_param_val.flag_true->octet_str_ran.buf = g_bufs.back().data();
  return p;
}

ran_param_struct_t members(const std::vector<seq_ran_param_t> &m)
{
  ran_param_struct_t s;
  s.sz_ran_param_struct = m.size();
  s.ran_param_struct = new seq_ran_param_t[m.size() + 1];
  for (size_t i = 0; i < m.size(); i++)
    s.ran_param_struct[i] = m[i];
  return s;
}

seq_ran_param_t strct(uint32_t id, const std::vector<seq_ran_param_t> &m)
{
  seq_ran_param_t p;
  std::memset(&p, 0, sizeof(p));
  p.ran_param_id = id;
  p.ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  p.ran_param_val.strct = new ran_param_struct_t(members(m));
  return p;
}

seq_ran_param_t lst(uint32_t id, const std::vector<std::vector<seq_ran_param_t>> &items)
{
  seq_ran_param_t p;
  std::memset(&p, 0, sizeof(p));
  p.ran_param_id = id;
  p.ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
  p.ran_param_val.lst = new ran_param_list_t;
  p.ran_param_val.lst->sz_lst_ran_param = items.size();
  p.ran_param_val.lst->lst_ran_param = new lst_ran_param_t[items.size() + 1];
  for (size_t i = 0; i < items.size(); i++)
    p.ran_param_val.lst->lst_ran_param[i].ran_param_struct = members(items[i]);
  return p;
}

/* PLMN 001.01 em BCD (TS 38.413) */
const std::vector<uint8_t> kPlmn00101 = {0x00, 0xf1, 0x10};

/* Membro de política: PLMN + S-NSSAI (SD ausente com sd < 0) */
std::vector<seq_ran_param_t> member(uint8_t sst, int64_t sd, std::vector<uint8_t> plmn = kPlmn00101)
{
  std::vector<seq_ran_param_t> snssai = {elem_oct(RC_SLICE_SST, {sst})};
  if (sd >= 0)
    snssai.push_back(elem_oct(RC_SLICE_SD, {(uint8_t)(sd >> 16), (uint8_t)(sd >> 8), (uint8_t)sd}));
  return {elem_oct(RC_SLICE_PLMN_IDENTITY, plmn), strct(RC_SLICE_S_NSSAI, snssai)};
}

/* Grupo de cotas; ratio < 0 deixa o parâmetro de fora */
std::vector<seq_ran_param_t> group(const std::vector<std::vector<seq_ran_param_t>> &mems, int min, int max, int ded)
{
  std::vector<seq_ran_param_t> g = {strct(RC_SLICE_RRM_POLICY, {lst(RC_SLICE_RRM_POLICY_MEMBER_LIST, mems)})};
  if (min >= 0)
    g.push_back(elem_int(RC_SLICE_MIN_PRB_POLICY_RATIO, min));
  if (max >= 0)
    g.push_back(elem_int(RC_SLICE_MAX_PRB_POLICY_RATIO, max));
  if (ded >= 0)
    g.push_back(elem_int(RC_SLICE_DEDICATED_PRB_POLICY_RATIO, ded));
  return g;
}

e2sm_rc_ctrl_msg_frmt_1_t msg(const std::vector<seq_ran_param_t> &params)
{
  e2sm_rc_ctrl_msg_frmt_1_t m;
  m.sz_ran_param = params.size();
  m.ran_param = new seq_ran_param_t[params.size() + 1];
  for (size_t i = 0; i < params.size(); i++)
    m.ran_param[i] = params[i];
  return m;
}

e2sm_rc_ctrl_msg_frmt_1_t quota_msg(const std::vector<std::vector<seq_ran_param_t>> &groups)
{
  return msg({lst(RC_SLICE_RRM_POLICY_RATIO_LIST, groups)});
}

nr_slice_policy_t pol(uint8_t sst, uint32_t sd, int min, int max, int ded)
{
  return nr_slice_policy_t{sst, sd, nr_slice_ratio_t{(uint8_t)min, (uint8_t)max, (uint8_t)ded}};
}

} // namespace

/* ---- PLMN ---------------------------------------------------------------- */

TEST(rc_slice_plmn, decodes_two_and_three_digit_mnc)
{
  rc_slice_plmn_t p;
  ASSERT_TRUE(rc_slice_plmn_decode(kPlmn00101.data(), 3, &p));
  EXPECT_EQ(p.mcc, 1);
  EXPECT_EQ(p.mnc, 1);
  EXPECT_EQ(p.mnc_digit_len, 2);

  const uint8_t b310410[] = {0x13, 0x00, 0x14};
  ASSERT_TRUE(rc_slice_plmn_decode(b310410, 3, &p));
  EXPECT_EQ(p.mcc, 310);
  EXPECT_EQ(p.mnc, 410);
  EXPECT_EQ(p.mnc_digit_len, 3);
}

TEST(rc_slice_plmn, rejects_bad_length_and_digits)
{
  rc_slice_plmn_t p;
  EXPECT_FALSE(rc_slice_plmn_decode(kPlmn00101.data(), 2, &p));
  const uint8_t bad[] = {0x0a, 0xf1, 0x10};
  EXPECT_FALSE(rc_slice_plmn_decode(bad, 3, &p));
}

/* ---- conversão da mensagem ---------------------------------------------- */

TEST(rc_slice_parse, two_groups)
{
  auto m = quota_msg({group({member(1, 1)}, 0, 70, 0), group({member(1, 2)}, 10, 30, 5)});
  rc_slice_quota_t q[NR_SLICE_MAX_POLICIES];
  char err[128] = {0};
  ASSERT_EQ(rc_slice_quota_parse(&m, q, NR_SLICE_MAX_POLICIES, err, sizeof(err)), 2) << err;
  EXPECT_EQ(q[0].policy.sst, 1);
  EXPECT_EQ(q[0].policy.sd, 1u);
  EXPECT_EQ(q[0].policy.ratio.max_ratio, 70);
  EXPECT_EQ(q[0].plmn.mcc, 1);
  EXPECT_EQ(q[1].policy.sd, 2u);
  EXPECT_EQ(q[1].policy.ratio.min_ratio, 10);
  EXPECT_EQ(q[1].policy.ratio.max_ratio, 30);
  EXPECT_EQ(q[1].policy.ratio.dedicated_ratio, 5);
}

TEST(rc_slice_parse, group_with_two_members_shares_ratios)
{
  auto m = quota_msg({group({member(1, 1), member(1, 2)}, 20, 50, 0)});
  rc_slice_quota_t q[NR_SLICE_MAX_POLICIES];
  ASSERT_EQ(rc_slice_quota_parse(&m, q, NR_SLICE_MAX_POLICIES, nullptr, 0), 2);
  EXPECT_EQ(q[0].policy.ratio.min_ratio, 20);
  EXPECT_EQ(q[1].policy.ratio.min_ratio, 20);
  EXPECT_EQ(q[1].policy.ratio.max_ratio, 50);
}

TEST(rc_slice_parse, defaults_and_missing_sd)
{
  auto m = quota_msg({group({member(2, -1)}, -1, -1, -1)});
  rc_slice_quota_t q[1];
  ASSERT_EQ(rc_slice_quota_parse(&m, q, 1, nullptr, 0), 1);
  EXPECT_EQ(q[0].policy.sst, 2);
  EXPECT_EQ(q[0].policy.sd, (uint32_t)NR_SLICE_SD_NONE);
  EXPECT_EQ(q[0].policy.ratio.min_ratio, 0);
  EXPECT_EQ(q[0].policy.ratio.max_ratio, 100);
  EXPECT_EQ(q[0].policy.ratio.dedicated_ratio, 0);
}

TEST(rc_slice_parse, sst_and_sd_as_integer)
{
  auto m = quota_msg({group({{elem_oct(RC_SLICE_PLMN_IDENTITY, kPlmn00101),
                              strct(RC_SLICE_S_NSSAI, {elem_int(RC_SLICE_SST, 1), elem_int(RC_SLICE_SD, 0xabcdef)})}},
                            0, 40, 0)});
  rc_slice_quota_t q[1];
  ASSERT_EQ(rc_slice_quota_parse(&m, q, 1, nullptr, 0), 1);
  EXPECT_EQ(q[0].policy.sd, 0xabcdefu);
}

TEST(rc_slice_parse, empty_list_means_clear)
{
  auto m = quota_msg({});
  rc_slice_quota_t q[1];
  EXPECT_EQ(rc_slice_quota_parse(&m, q, 1, nullptr, 0), 0);
}

TEST(rc_slice_parse, rejects_invalid_messages)
{
  rc_slice_quota_t q[NR_SLICE_MAX_POLICIES];
  char err[128];

  auto no_list = msg({elem_int(RC_SLICE_MAX_PRB_POLICY_RATIO, 50)});
  EXPECT_EQ(rc_slice_quota_parse(&no_list, q, NR_SLICE_MAX_POLICIES, err, sizeof(err)), -1);

  auto over = quota_msg({group({member(1, 1)}, 0, 101, 0)});
  EXPECT_EQ(rc_slice_quota_parse(&over, q, NR_SLICE_MAX_POLICIES, err, sizeof(err)), -1);

  auto no_members = quota_msg({group({}, 0, 50, 0)});
  EXPECT_EQ(rc_slice_quota_parse(&no_members, q, NR_SLICE_MAX_POLICIES, err, sizeof(err)), -1);

  auto bad_plmn = quota_msg({group({member(1, 1, {0x00, 0xf1})}, 0, 50, 0)});
  EXPECT_EQ(rc_slice_quota_parse(&bad_plmn, q, NR_SLICE_MAX_POLICIES, err, sizeof(err)), -1);

  auto dup = quota_msg({group({member(1, 1)}, 0, 50, 0), group({member(1, 1)}, 0, 30, 0)});
  EXPECT_EQ(rc_slice_quota_parse(&dup, q, NR_SLICE_MAX_POLICIES, err, sizeof(err)), -1);
  EXPECT_NE(std::strstr(err, "repetido"), nullptr) << err;

  auto two = quota_msg({group({member(1, 1), member(1, 2)}, 0, 50, 0)});
  EXPECT_EQ(rc_slice_quota_parse(&two, q, 1, err, sizeof(err)), -1);
}

/* ---- mescla com as cotas em vigor --------------------------------------- */

TEST(rc_slice_merge, updates_appends_and_keeps_others)
{
  const nr_slice_policy_t cur[] = {pol(1, 1, 0, 30, 0), pol(1, 2, 0, 70, 0)};
  rc_slice_quota_t upd[2] = {};
  upd[0].policy = pol(1, 2, 0, 40, 0);
  upd[1].policy = pol(1, 3, 10, 20, 0);

  nr_slice_policy_t out[NR_SLICE_MAX_POLICIES];
  ASSERT_EQ(rc_slice_quota_merge(cur, 2, upd, 2, out, NR_SLICE_MAX_POLICIES, nullptr, 0), 3);
  EXPECT_EQ(out[0].ratio.max_ratio, 30);
  EXPECT_EQ(out[1].sd, 2u);
  EXPECT_EQ(out[1].ratio.max_ratio, 40);
  EXPECT_EQ(out[2].sd, 3u);
  EXPECT_EQ(out[2].ratio.min_ratio, 10);
}

TEST(rc_slice_merge, rejects_overflow)
{
  const nr_slice_policy_t cur[] = {pol(1, 1, 0, 30, 0)};
  rc_slice_quota_t upd[1] = {};
  upd[0].policy = pol(1, 2, 0, 40, 0);
  nr_slice_policy_t out[1];
  EXPECT_EQ(rc_slice_quota_merge(cur, 1, upd, 1, out, 1, nullptr, 0), -1);
}
