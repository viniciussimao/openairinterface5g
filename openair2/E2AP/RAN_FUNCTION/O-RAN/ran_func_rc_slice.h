/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief E2SM-RC CONTROL estilo 2 ("Radio Resource Allocation Control"),
 *        ação 6 ("Slice-level PRB quota"), E2SM-RC v1.03, 7.6.3 e 8.4.3.6.
 *
 * Converte a mensagem de CONTROL formato 1 numa lista de cotas por slice
 * (nr_slice_policy_t) e mescla essa lista com as cotas em vigor. Não depende
 * do MAC nem do RRC: o handler em ran_func_rc.c lê as cotas em vigor, chama
 * estas funções e aplica o resultado com nr_slice_policy_set().
 *
 * Árvore de parâmetros da ação 6. Nos valores, um item de LIST não leva ID:
 * é direto a STRUCTURE com os membros do item (2 e 5 só aparecem na
 * definição anunciada no E2 Setup).
 *
 *   1 RRM Policy Ratio List (LIST)
 *     2 RRM Policy Ratio Group (STRUCTURE, item)
 *       3 RRM Policy (STRUCTURE)
 *         4 RRM Policy Member List (LIST)
 *           5 RRM Policy Member (STRUCTURE, item)
 *             6 PLMN Identity (ELEMENT, OCTET STRING de 3 octetos)
 *             7 S-NSSAI (STRUCTURE)
 *               8 SST (ELEMENT, OCTET STRING de 1 octeto ou INTEGER)
 *               9 SD (ELEMENT opcional, OCTET STRING de 3 octetos ou INTEGER)
 *       10 Min PRB Policy Ratio (ELEMENT opcional, INTEGER 0..100, padrão 0)
 *       11 Max PRB Policy Ratio (ELEMENT opcional, INTEGER 0..100, padrão 100)
 *       12 Dedicated PRB Policy Ratio (ELEMENT opcional, INTEGER 0..100, padrão 0)
 *
 * Os padrões seguem o RRMPolicyRatio da 3GPP TS 28.541. Todos os membros de
 * um grupo recebem as mesmas cotas.
 */

#ifndef RAN_FUNC_RC_SLICE_H
#define RAN_FUNC_RC_SLICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "../../flexric/src/sm/rc_sm/ie/ir/e2sm_rc_ctrl_msg_frmt_1.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_slice_budget.h"

#define RC_CTRL_STYLE_RADIO_RESOURCE_ALLOC 2
#define RC_CTRL_ACT_SLICE_PRB_QUOTA 6

/// IDs dos parâmetros da ação 6 (E2SM-RC v1.03, 8.4.3.6)
typedef enum {
  RC_SLICE_RRM_POLICY_RATIO_LIST = 1,
  RC_SLICE_RRM_POLICY_RATIO_GROUP = 2,
  RC_SLICE_RRM_POLICY = 3,
  RC_SLICE_RRM_POLICY_MEMBER_LIST = 4,
  RC_SLICE_RRM_POLICY_MEMBER = 5,
  RC_SLICE_PLMN_IDENTITY = 6,
  RC_SLICE_S_NSSAI = 7,
  RC_SLICE_SST = 8,
  RC_SLICE_SD = 9,
  RC_SLICE_MIN_PRB_POLICY_RATIO = 10,
  RC_SLICE_MAX_PRB_POLICY_RATIO = 11,
  RC_SLICE_DEDICATED_PRB_POLICY_RATIO = 12,
} rc_slice_param_id_e;

/// PLMN decodificado do PLMN Identity (TS 38.413, 9.3.3.5)
typedef struct rc_slice_plmn {
  uint16_t mcc;
  uint16_t mnc;
  uint8_t mnc_digit_len; ///< 2 ou 3
} rc_slice_plmn_t;

/// Cota de um membro de política, com o PLMN dele
typedef struct rc_slice_quota {
  rc_slice_plmn_t plmn;
  nr_slice_policy_t policy;
} rc_slice_quota_t;

/** @brief Decodifica um PLMN Identity de 3 octetos em BCD.
 * @return false se o tamanho ou algum dígito for inválido */
bool rc_slice_plmn_decode(const uint8_t *buf, size_t len, rc_slice_plmn_t *out);

/** @brief Converte a mensagem de CONTROL da ação 6 em cotas.
 *
 * Exige o parâmetro 1 (LIST). Uma lista vazia é válida e devolve 0: o
 * handler a usa para remover todas as cotas. Não valida as regras entre
 * cotas (soma dos mínimos, repetição): isso é com nr_slice_policy_validate().
 *
 * @param msg mensagem de CONTROL formato 1
 * @param out saída (até max cotas)
 * @param max tamanho de out
 * @param err buffer para a mensagem de erro (pode ser NULL)
 * @param err_len tamanho do buffer
 * @return número de cotas, ou -1 se a mensagem é inválida */
int rc_slice_quota_parse(const e2sm_rc_ctrl_msg_frmt_1_t *msg, rc_slice_quota_t *out, int max, char *err, size_t err_len);

/** @brief Mescla as cotas recebidas com as cotas em vigor.
 *
 * Cada cota recebida substitui a do mesmo S-NSSAI ou entra no fim da lista;
 * as cotas em vigor de outros S-NSSAI ficam como estão.
 *
 * @return número de cotas em out, ou -1 se passar de max */
int rc_slice_quota_merge(const nr_slice_policy_t *cur,
                         int n_cur,
                         const rc_slice_quota_t *upd,
                         int n_upd,
                         nr_slice_policy_t *out,
                         int max,
                         char *err,
                         size_t err_len);

#ifdef __cplusplus
}
#endif

#endif /* RAN_FUNC_RC_SLICE_H */
