/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Leitura das cotas de PRB por slice do arquivo de configuração do
 *        gNB (seção opcional MACRLCs.[0].slice_policies).
 *
 * Exemplo:
 *
 *   MACRLCs = ({
 *     ...
 *     slice_policies = (
 *       { sst = 1; sd = 0x000001; min_ratio = 0; max_ratio = 30; dedicated_ratio = 0; },
 *       { sst = 1; sd = 0x000002; min_ratio = 0; max_ratio = 70; dedicated_ratio = 0; }
 *     );
 *   });
 *
 * Sem a seção, o gNB usa a política de alocação padrão do OAI.
 */

#ifndef GNB_SLICE_POLICY_CONFIG_H
#define GNB_SLICE_POLICY_CONFIG_H

#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

#define GNB_CONFIG_STRING_SLICE_POLICIES "slice_policies"

// clang-format off
#define GNB_SLICE_POLICY_PARAMS_DESC {                                                                              \
/*  optname            helpstr                                     paramflags XXXptr      def val                type      numelt */ \
  {"sst",             "S-NSSAI: slice service type",                     0, .uptr = NULL, .defuintval = 1,        TYPE_UINT, 0}, \
  {"sd",              "S-NSSAI: slice differentiator (0xffffff = sem SD)", 0, .uptr = NULL, .defuintval = 0xffffff, TYPE_UINT, 0}, \
  {"min_ratio",       "PRBs DL garantidos ao slice com demanda, em %",   0, .uptr = NULL, .defuintval = 0,        TYPE_UINT, 0}, \
  {"max_ratio",       "teto de PRBs DL do slice, em %",                  0, .uptr = NULL, .defuintval = 100,      TYPE_UINT, 0}, \
  {"dedicated_ratio", "PRBs DL reservados ao slice, em %",               0, .uptr = NULL, .defuintval = 0,        TYPE_UINT, 0}, \
}
// clang-format on

#define GNB_SLICE_POLICY_SST_IDX 0
#define GNB_SLICE_POLICY_SD_IDX 1
#define GNB_SLICE_POLICY_MIN_IDX 2
#define GNB_SLICE_POLICY_MAX_IDX 3
#define GNB_SLICE_POLICY_DED_IDX 4

/** @brief Lê MACRLCs.[0].slice_policies e aplica as cotas com
 *         nr_slice_policy_set(). Sem a seção, não faz nada. Uma seção
 *         inválida encerra o gNB com a mensagem de erro (AssertFatal).
 * @param mac instância do MAC, já iniciada por mac_top_init_gNB() */
void gnb_read_slice_policies(gNB_MAC_INST *mac);

#endif /* GNB_SLICE_POLICY_CONFIG_H */
