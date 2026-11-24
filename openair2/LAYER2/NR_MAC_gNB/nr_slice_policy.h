/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Escalonamento DL por slice: cotas de PRB por S-NSSAI no modelo
 *        RRMPolicyRatio (3GPP TS 28.541), aplicadas por slot sobre o
 *        proportional fair do OAI.
 *
 * A API (nr_slice_policy_set/get) é a única porta de entrada das cotas: a
 * configuração do gNB a usa na subida, e um handler de E2SM-RC (CONTROL
 * estilo 2, "Slice-level PRB quota") pode usá-la depois. Sem cotas, o MAC
 * usa a política padrão, sem nenhuma mudança.
 */

#ifndef NR_SLICE_POLICY_H
#define NR_SLICE_POLICY_H

#include "nr_slice_budget.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

/** @brief Troca as cotas por slice de uma vez, sob o sched_lock.
 *
 * Com n > 0, valida a lista e passa o DL a usar nr_dl_slice_rb_alloc();
 * com n == 0, volta à política de alocação que estava em uso antes.
 * Uma lista inválida não muda nada.
 *
 * @param mac instância do MAC
 * @param policies lista de cotas (NULL se n == 0)
 * @param n número de cotas (0..NR_SLICE_MAX_POLICIES)
 * @param err buffer para a mensagem de erro (pode ser NULL)
 * @param err_len tamanho do buffer
 * @return 0 se aplicou, -1 se a lista é inválida */
int nr_slice_policy_set(gNB_MAC_INST *mac, const nr_slice_policy_t *policies, int n, char *err, size_t err_len);

/** @brief Lê as cotas em vigor, sob o sched_lock.
 * @param mac instância do MAC
 * @param out saída (até max cotas)
 * @param max tamanho de out
 * @return número de cotas em vigor (0 = sem cotas) */
int nr_slice_policy_get(gNB_MAC_INST *mac, nr_slice_policy_t *out, int max);

/** @brief Política de alocação de PRBs DL por slice (nr_dl_rb_alloc_fn).
 *
 * Agrupa os candidatos pela S-NSSAI do primeiro DRB, calcula o orçamento
 * de cada slice com nr_slice_compute_budget() e chama
 * nr_dl_proportional_fair() dentro de cada slice, com o orçamento como
 * limite. A ordem dos slices alterna a cada chamada (round-robin). */
int nr_dl_slice_rb_alloc(const nr_dl_sched_params_t *params, nr_dl_candidate_t *candidates, int n_candidates);

#endif /* NR_SLICE_POLICY_H */
