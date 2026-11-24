/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Leitura das cotas de PRB por slice. Veja gnb_slice_policy_config.h.
 */

#include "gnb_slice_policy_config.h"

#include <stdio.h>

#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "common/utils/assertions.h"
#include "common/utils/LOG/log.h"
#include "MACRLC_nr_paramdef.h"
#include "LAYER2/NR_MAC_gNB/nr_slice_policy.h"

void gnb_read_slice_policies(gNB_MAC_INST *mac)
{
  char prefix[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(prefix, sizeof(prefix), "%s.[0]", MACRLC_LIST);
  GET_PARAMS_LIST(SlicePolicyList, SlicePolicyParams, GNB_SLICE_POLICY_PARAMS_DESC, GNB_CONFIG_STRING_SLICE_POLICIES, prefix);

  const int n = SlicePolicyList.numelt;
  if (n <= 0)
    return;
  AssertFatal(n <= NR_SLICE_MAX_POLICIES,
              "%s.%s: %d slices, mas o máximo é %d\n",
              prefix,
              GNB_CONFIG_STRING_SLICE_POLICIES,
              n,
              NR_SLICE_MAX_POLICIES);

  nr_slice_policy_t policies[NR_SLICE_MAX_POLICIES] = {0};
  for (int i = 0; i < n; i++) {
    const paramdef_t *p = SlicePolicyList.paramarray[i];
    const uint32_t sst = *p[GNB_SLICE_POLICY_SST_IDX].uptr;
    const uint32_t min = *p[GNB_SLICE_POLICY_MIN_IDX].uptr;
    const uint32_t max = *p[GNB_SLICE_POLICY_MAX_IDX].uptr;
    const uint32_t ded = *p[GNB_SLICE_POLICY_DED_IDX].uptr;
    /* as cotas cabem em uint8_t; valores maiores são recusados aqui, antes
     * da conversão, e o resto das regras fica com nr_slice_policy_validate() */
    AssertFatal(sst <= 255, "%s.%s.[%d]: sst %u fora de 0..255\n", prefix, GNB_CONFIG_STRING_SLICE_POLICIES, i, sst);
    AssertFatal(min <= 100 && max <= 100 && ded <= 100,
                "%s.%s.[%d]: min_ratio, max_ratio e dedicated_ratio vão de 0 a 100\n",
                prefix,
                GNB_CONFIG_STRING_SLICE_POLICIES,
                i);
    policies[i] = (nr_slice_policy_t){
        .sst = sst,
        .sd = *p[GNB_SLICE_POLICY_SD_IDX].uptr,
        .ratio = {.min_ratio = min, .max_ratio = max, .dedicated_ratio = ded},
    };
  }

  char err[160];
  const int rc = nr_slice_policy_set(mac, policies, n, err, sizeof(err));
  AssertFatal(rc == 0, "%s.%s inválido: %s\n", prefix, GNB_CONFIG_STRING_SLICE_POLICIES, err);
}
