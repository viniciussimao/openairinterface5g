/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Estado do E2SM-KPM por assinatura e por UE.
 *
 * Várias medidas (RRU.PrbTotDl, DRB.UEThpDl, DRB.PdcpSduVolumeDL...) são a
 * diferença entre o contador atual e o do relatório anterior. Antes, esse
 * "anterior" ficava em vetores globais indexados pela posição do UE na lista
 * de cada relatório, e a referência de PRBs da célula era uma só. Com duas
 * assinaturas ao mesmo tempo (por exemplo, uma por slice), o UE de uma
 * assinatura usava o contador do UE da outra, e cada relatório reiniciava a
 * referência de PRBs das outras.
 *
 * Aqui, cada assinatura tem a própria referência da célula, e cada par
 * (assinatura, UE) tem a própria posição (slot) nos vetores de contadores.
 * A assinatura é identificada pela definição de ação (act_def), que o agente
 * aloca uma vez por assinatura; o UE, pelo rrc_ue_id.
 *
 * Limitações:
 *  - o agente não avisa quando uma assinatura é removida. A entrada dela só
 *    é reaproveitada quando a tabela enche (a usada há mais tempo sai);
 *  - se uma assinatura nova receber o endereço de act_def de uma removida, o
 *    primeiro relatório dela parte dos contadores da antiga;
 *  - o primeiro relatório de cada UE numa assinatura parte de zero, como no
 *    upstream: a xApp deve descartá-lo.
 */

#ifndef RAN_FUNC_KPM_STATE_H
#define RAN_FUNC_KPM_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "ran_func_kpm_subs.h"

/// assinaturas KPM acompanhadas ao mesmo tempo
#define KPM_STATE_MAX_SUBS 8
/// posições nos vetores de contadores por UE (ver ran_func_kpm_subs.c)
#define KPM_STATE_MAX_SLOTS (KPM_STATE_MAX_SUBS * MAX_MOBILES_PER_GNB)

typedef struct kpm_sub_state kpm_sub_state_t;

/** @brief Estado da assinatura identificada por key (o act_def dela). Uma
 *         assinatura nova ocupa uma entrada livre ou a usada há mais tempo,
 *         que é zerada.
 *  @return nunca NULL */
kpm_sub_state_t *kpm_state_get_sub(const void *key);

/** @brief Referência da célula da assinatura: [0] = relatório anterior,
 *         [1] = atual, como o node_stats que as medidas recebem. */
e2_node_level_stats_t *kpm_state_node_stats(kpm_sub_state_t *sub);

/** @brief Associa os UEs do relatório atual a slots dos vetores de
 *         contadores. Um UE que já estava no relatório anterior mantém o
 *         slot; um UE novo recebe um slot zerado; os slots de UEs que saíram
 *         ficam livres.
 *  @param ue_ids rrc_ue_id de cada UE do relatório
 *  @param n número de UEs (até MAX_MOBILES_PER_GNB)
 *  @param slots saída: slot de cada UE, a passar como ue_idx às medidas */
void kpm_state_map_ues(kpm_sub_state_t *sub, const uint32_t *ue_ids, size_t n, size_t *slots);

#endif /* RAN_FUNC_KPM_STATE_H */
