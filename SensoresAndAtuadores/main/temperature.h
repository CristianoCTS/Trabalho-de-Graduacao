#pragma once

#include <stdbool.h>
#include <stdint.h>

// Inicializa o barramento 1-Wire e sobe a tarefa de leitura periodica.
// Deve ser chamada antes de readIR(), pois readIR() bloqueia para sempre.
void temperature_init(void);

// Ultima temperatura valida lida, em graus Celsius.
// Retorna false enquanto nenhuma leitura valida tiver acontecido.
bool temperature_get(float *out_celsius);