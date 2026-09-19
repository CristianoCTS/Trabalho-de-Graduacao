#pragma once
#include <stdint.h>

// Inicializa o canal RMT de recepcao e sobe a tarefa que fica escutando o IR.
// Retorna imediatamente; a escuta acontece em uma tarefa propria.
void receiver_init(void);