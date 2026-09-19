#pragma once

#include <stdbool.h>
#include <stdint.h>

// O DHT22 entrega temperatura e umidade na mesma leitura, entao as duas voltam
// juntas nesta struct (C nao tem tupla nativa).
typedef struct {
    float temperature;   // graus Celsius
    float humidity;      // umidade relativa em %
} dht22_reading_t;

// Inicializa o pino do DHT22 e sobe a tarefa de leitura periodica.
void humidity_init(void);

// Ultima leitura valida do sensor.
// Retorna false enquanto nenhuma leitura valida tiver acontecido.
bool humidity_get(dht22_reading_t *out);