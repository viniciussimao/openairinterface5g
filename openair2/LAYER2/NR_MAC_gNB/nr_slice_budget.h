/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Cotas de PRB por slice no modelo RRMPolicyRatio (3GPP TS 28.541):
 *        validação das cotas e cálculo do orçamento de PRBs de cada slice
 *        num slot. Este arquivo não depende do resto do MAC, para poder ser
 *        testado sozinho (tests/test_nr_slice_budget.cpp).
 */

#ifndef NR_SLICE_BUDGET_H
#define NR_SLICE_BUDGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Número máximo de slices com cota. O escalonador usa um grupo a mais,
/// o grupo padrão, para os UEs cuja S-NSSAI não tem cota.
#define NR_SLICE_MAX_POLICIES 8
#define NR_SLICE_MAX_GROUPS (NR_SLICE_MAX_POLICIES + 1)

/// SD ausente ("sem SD"), conforme a TS 23.003, seção 28.4.2
#define NR_SLICE_SD_NONE 0xffffff

/// Cotas de um slice, em porcentagem dos PRBs: 0 <= dedicated <= min <= max <= 100
typedef struct nr_slice_ratio {
  uint8_t min_ratio; ///< garantidos quando o slice tem demanda (inclui os dedicados)
  uint8_t max_ratio; ///< teto do slice, mesmo com PRBs sobrando
  uint8_t dedicated_ratio; ///< reservados ao slice; sem demanda, ficam sem uso
} nr_slice_ratio_t;

/// Cota de um slice identificado pela S-NSSAI (sst + sd)
typedef struct nr_slice_policy {
  uint8_t sst;
  uint32_t sd; ///< NR_SLICE_SD_NONE quando o slice não tem SD
  nr_slice_ratio_t ratio;
} nr_slice_policy_t;

/** @brief Confere uma lista de cotas.
 *
 * Regras: no máximo NR_SLICE_MAX_POLICIES slices, sd <= 0xffffff,
 * dedicated <= min <= max <= 100, soma dos min_ratio <= 100 e nenhuma
 * S-NSSAI repetida.
 *
 * @param policies lista de cotas (pode ser NULL se n == 0)
 * @param n número de cotas
 * @param err buffer para a mensagem de erro (pode ser NULL)
 * @param err_len tamanho do buffer
 * @return true se a lista é válida */
bool nr_slice_policy_validate(const nr_slice_policy_t *policies, int n, char *err, size_t err_len);

/** @brief Calcula o orçamento de PRBs de cada grupo num slot.
 *
 * Passos, todos em PRBs inteiros (arredondamento para baixo):
 *  1. reserva os dedicados de todos os grupos, com ou sem demanda;
 *  2. garante a cada grupo com demanda o mínimo, limitado à demanda dele
 *     (os dedicados entram no mínimo);
 *  3. reparte o resto em partes iguais entre os grupos com demanda não
 *     atendida (preenchimento até o nível), sem passar do max_ratio nem da
 *     demanda. A sobra do arredondamento entra aqui, 1 PRB por grupo, a
 *     partir de rr_start (round-robin).
 *
 * Os dedicados de um grupo sem demanda não vão para nenhum outro grupo.
 *
 * @param n_prb PRBs disponíveis no slot
 * @param n_groups número de grupos (até NR_SLICE_MAX_GROUPS)
 * @param ratio cotas de cada grupo
 * @param demand PRBs que cada grupo pediria (0 = sem demanda)
 * @param rr_start grupo que recebe primeiro a sobra de 1 PRB
 * @param budget saída: orçamento de cada grupo; um grupo sem demanda recebe 0 */
void nr_slice_compute_budget(int n_prb,
                             int n_groups,
                             const nr_slice_ratio_t *ratio,
                             const int *demand,
                             int rr_start,
                             int *budget);

#ifdef __cplusplus
}
#endif

#endif /* NR_SLICE_BUDGET_H */
