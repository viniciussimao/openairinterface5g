/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "ran_func_rc_slice.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../flexric/src/sm/rc_sm/ie/ir/lst_ran_param.h"
#include "../../flexric/src/sm/rc_sm/ie/ir/ran_param_list.h"
#include "../../flexric/src/sm/rc_sm/ie/ir/ran_param_struct.h"
#include "../../flexric/src/sm/rc_sm/ie/ir/ran_parameter_value.h"

static void set_err(char *err, size_t err_len, const char *fmt, ...)
{
  if (err == NULL || err_len == 0)
    return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, err_len, fmt, ap);
  va_end(ap);
}

static const seq_ran_param_t *find_param(const seq_ran_param_t *params, size_t n, uint32_t id)
{
  for (size_t i = 0; i < n; i++)
    if (params[i].ran_param_id == id)
      return &params[i];
  return NULL;
}

/// Membro de uma STRUCTURE; NULL se faltar
static const seq_ran_param_t *strct_find(const ran_param_struct_t *s, uint32_t id)
{
  return s == NULL ? NULL : find_param(s->ran_param_struct, s->sz_ran_param_struct, id);
}

/// Valor de um ELEMENT, com ou sem a marca de chave
static const ran_parameter_value_t *element_value(const seq_ran_param_t *p)
{
  if (p == NULL)
    return NULL;
  if (p->ran_param_val.type == ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE)
    return p->ran_param_val.flag_true;
  if (p->ran_param_val.type == ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE)
    return p->ran_param_val.flag_false;
  return NULL;
}

/// Inteiro sem sinal de um ELEMENT: INTEGER, ou OCTET STRING de len octetos (big endian)
static bool element_uint(const seq_ran_param_t *p, size_t octets, uint64_t max, uint64_t *out)
{
  const ran_parameter_value_t *v = element_value(p);
  if (v == NULL)
    return false;
  uint64_t x = 0;
  if (v->type == INTEGER_RAN_PARAMETER_VALUE) {
    if (v->int_ran < 0)
      return false;
    x = (uint64_t)v->int_ran;
  } else if (v->type == OCTET_STRING_RAN_PARAMETER_VALUE && octets > 0) {
    if (v->octet_str_ran.buf == NULL || v->octet_str_ran.len != octets)
      return false;
    for (size_t i = 0; i < octets; i++)
      x = (x << 8) | v->octet_str_ran.buf[i];
  } else {
    return false;
  }
  if (x > max)
    return false;
  *out = x;
  return true;
}

/// Cota opcional (0..100); ausente vale def
static bool ratio_or_default(const ran_param_struct_t *grp, uint32_t id, uint8_t def, uint8_t *out)
{
  const seq_ran_param_t *p = strct_find(grp, id);
  if (p == NULL) {
    *out = def;
    return true;
  }
  uint64_t x;
  if (!element_uint(p, 0, 100, &x))
    return false;
  *out = (uint8_t)x;
  return true;
}

bool rc_slice_plmn_decode(const uint8_t *buf, size_t len, rc_slice_plmn_t *out)
{
  if (buf == NULL || len != 3)
    return false;
  /* TS 38.413, 9.3.3.5: MCC1 nos bits baixos do 1º octeto, MCC2 nos altos;
   * MCC3 nos baixos do 2º e MNC3 nos altos (0xF com MNC de 2 dígitos);
   * MNC1 nos baixos do 3º e MNC2 nos altos. */
  const unsigned mcc1 = buf[0] & 0xf, mcc2 = buf[0] >> 4, mcc3 = buf[1] & 0xf;
  const unsigned mnc3 = buf[1] >> 4, mnc1 = buf[2] & 0xf, mnc2 = buf[2] >> 4;
  if (mcc1 > 9 || mcc2 > 9 || mcc3 > 9 || mnc1 > 9 || mnc2 > 9 || (mnc3 > 9 && mnc3 != 0xf))
    return false;
  out->mcc = mcc1 * 100 + mcc2 * 10 + mcc3;
  if (mnc3 == 0xf) {
    out->mnc = mnc1 * 10 + mnc2;
    out->mnc_digit_len = 2;
  } else {
    out->mnc = mnc1 * 100 + mnc2 * 10 + mnc3;
    out->mnc_digit_len = 3;
  }
  return true;
}

static int parse_member(const ran_param_struct_t *m, rc_slice_quota_t *q, char *err, size_t err_len)
{
  const ran_parameter_value_t *plmn = element_value(strct_find(m, RC_SLICE_PLMN_IDENTITY));
  if (plmn == NULL || plmn->type != OCTET_STRING_RAN_PARAMETER_VALUE
      || !rc_slice_plmn_decode(plmn->octet_str_ran.buf, plmn->octet_str_ran.len, &q->plmn)) {
    set_err(err, err_len, "membro sem PLMN Identity (ID 6) válido");
    return -1;
  }

  const seq_ran_param_t *snssai = strct_find(m, RC_SLICE_S_NSSAI);
  if (snssai == NULL || snssai->ran_param_val.type != STRUCTURE_RAN_PARAMETER_VAL_TYPE) {
    set_err(err, err_len, "membro sem S-NSSAI (ID 7)");
    return -1;
  }
  const ran_param_struct_t *s = snssai->ran_param_val.strct;

  uint64_t sst;
  if (!element_uint(strct_find(s, RC_SLICE_SST), 1, 0xff, &sst)) {
    set_err(err, err_len, "S-NSSAI sem SST (ID 8) válido");
    return -1;
  }
  uint64_t sd = NR_SLICE_SD_NONE;
  const seq_ran_param_t *sd_p = strct_find(s, RC_SLICE_SD);
  if (sd_p != NULL && !element_uint(sd_p, 3, NR_SLICE_SD_NONE, &sd)) {
    set_err(err, err_len, "SD (ID 9) inválido no slice de SST %u", (unsigned)sst);
    return -1;
  }
  q->policy.sst = (uint8_t)sst;
  q->policy.sd = (uint32_t)sd;
  return 0;
}

int rc_slice_quota_parse(const e2sm_rc_ctrl_msg_frmt_1_t *msg, rc_slice_quota_t *out, int max, char *err, size_t err_len)
{
  if (msg == NULL) {
    set_err(err, err_len, "mensagem vazia");
    return -1;
  }
  const seq_ran_param_t *lst_p = find_param(msg->ran_param, msg->sz_ran_param, RC_SLICE_RRM_POLICY_RATIO_LIST);
  if (lst_p == NULL || lst_p->ran_param_val.type != LIST_RAN_PARAMETER_VAL_TYPE) {
    set_err(err, err_len, "falta o RRM Policy Ratio List (ID 1, LIST)");
    return -1;
  }
  const ran_param_list_t *lst = lst_p->ran_param_val.lst;
  const size_t n_grp = lst == NULL ? 0 : lst->sz_lst_ran_param;

  int n = 0;
  for (size_t g = 0; g < n_grp; g++) {
    const ran_param_struct_t *grp = &lst->lst_ran_param[g].ran_param_struct;

    nr_slice_ratio_t ratio;
    if (!ratio_or_default(grp, RC_SLICE_MIN_PRB_POLICY_RATIO, 0, &ratio.min_ratio)
        || !ratio_or_default(grp, RC_SLICE_MAX_PRB_POLICY_RATIO, 100, &ratio.max_ratio)
        || !ratio_or_default(grp, RC_SLICE_DEDICATED_PRB_POLICY_RATIO, 0, &ratio.dedicated_ratio)) {
      set_err(err, err_len, "grupo %zu: cota (IDs 10 a 12) fora de 0..100", g);
      return -1;
    }

    const seq_ran_param_t *pol = strct_find(grp, RC_SLICE_RRM_POLICY);
    if (pol == NULL || pol->ran_param_val.type != STRUCTURE_RAN_PARAMETER_VAL_TYPE) {
      set_err(err, err_len, "grupo %zu: falta o RRM Policy (ID 3)", g);
      return -1;
    }
    const seq_ran_param_t *mem_p = strct_find(pol->ran_param_val.strct, RC_SLICE_RRM_POLICY_MEMBER_LIST);
    if (mem_p == NULL || mem_p->ran_param_val.type != LIST_RAN_PARAMETER_VAL_TYPE || mem_p->ran_param_val.lst == NULL
        || mem_p->ran_param_val.lst->sz_lst_ran_param == 0) {
      set_err(err, err_len, "grupo %zu: falta o RRM Policy Member List (ID 4) ou ele está vazio", g);
      return -1;
    }
    const ran_param_list_t *mem = mem_p->ran_param_val.lst;

    for (size_t m = 0; m < mem->sz_lst_ran_param; m++) {
      if (n >= max) {
        set_err(err, err_len, "mais de %d membros de política", max);
        return -1;
      }
      rc_slice_quota_t *q = &out[n];
      memset(q, 0, sizeof(*q));
      char sub[96] = {0};
      if (parse_member(&mem->lst_ran_param[m].ran_param_struct, q, sub, sizeof(sub)) != 0) {
        set_err(err, err_len, "grupo %zu, membro %zu: %s", g, m, sub);
        return -1;
      }
      q->policy.ratio = ratio;
      for (int k = 0; k < n; k++) {
        if (out[k].policy.sst == q->policy.sst && out[k].policy.sd == q->policy.sd) {
          set_err(err, err_len, "S-NSSAI %u.%06x repetido na mensagem", (unsigned)q->policy.sst, (unsigned)q->policy.sd);
          return -1;
        }
      }
      n++;
    }
  }
  return n;
}

int rc_slice_quota_merge(const nr_slice_policy_t *cur,
                         int n_cur,
                         const rc_slice_quota_t *upd,
                         int n_upd,
                         nr_slice_policy_t *out,
                         int max,
                         char *err,
                         size_t err_len)
{
  if (n_cur > max) {
    set_err(err, err_len, "%d cotas em vigor, mais que %d", n_cur, max);
    return -1;
  }
  int n = n_cur;
  if (n_cur > 0)
    memmove(out, cur, n_cur * sizeof(*out));

  for (int u = 0; u < n_upd; u++) {
    const nr_slice_policy_t *p = &upd[u].policy;
    int i = 0;
    while (i < n && !(out[i].sst == p->sst && out[i].sd == p->sd))
      i++;
    if (i == n) {
      if (n >= max) {
        set_err(err, err_len, "mais de %d slices com cota", max);
        return -1;
      }
      n++;
    }
    out[i] = *p;
  }
  return n;
}
