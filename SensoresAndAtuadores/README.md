# Sensores e Atuadores — ESP32-C6

Sistema embarcado em ESP32-C6 (ESP-IDF v6.0) que monitora um ambiente (temperatura, umidade e ocupação aproximada) e controla um ar-condicionado por infravermelho, imitando o controle remoto original. Este documento descreve o que cada arquivo de sensor/atuador faz, como se comunica com o hardware e quais funções expõe para o resto do projeto.

## Visão geral do hardware

| Periférico | GPIO | Arquivo |
|---|---|---|
| DS18B20 (temperatura, 1-Wire) | 0 | `temperature.c/h` |
| DHT22 (temperatura + umidade) | 1 | `humidity.c/h` |
| TSOP (receptor IR) | 4 | `identify.cpp/h` |
| LED IR (via transistor, emissor) | 5 | `emiter.cpp/h` |
| Botões (placeholder de ocupação) | 6 e 7 | `curtain.c/h` |
| LED RGB integrado da placa | 8 | `curtain.c` |

Cada sensor é implementado como uma **função comum, chamada sob demanda** (não uma tarefa em loop): quem quiser uma leitura chama a função e recebe o resultado na hora, sem depender de um valor "cacheado" atualizado em segundo plano. Isso dá controle total sobre quando cada leitura acontece e evita consumo de CPU invisível. A exceção é o módulo de identificação de IR (`identify.cpp`), que roda como tarefa contínua porque não há como prever quando um sinal infravermelho vai chegar.

---

## `temperature.c` / `temperature.h` — Sensor de temperatura DS18B20

Lê a temperatura ambiente de um sensor DS18B20 conectado por **1-Wire**, um protocolo digital de um único fio onde o mestre (o ESP32) e o sensor se revezam controlando a linha, codificando informação na duração de cada pulso elétrico.

O arquivo implementa o protocolo inteiro "na mão" (bit-banging), sem biblioteca externa:

- **`pin_config`** — configura o GPIO como entrada/saída de dreno aberto (necessário porque o mesmo fio é usado tanto para escrever quanto para ler), com pull-up habilitado como reforço ao resistor externo de 4,7 kΩ.
- **`t_wellnesscheck`** — executa o pulso de reset do 1-Wire e verifica se o sensor respondeu com o pulso de presença, dentro de uma seção crítica (`portENTER_CRITICAL`/`portEXIT_CRITICAL`) para garantir que o timing em microssegundos não seja interrompido pelo escalonador do RTOS.
- **`t_write_bit` / `t_read_bit`** — escrevem ou leem um único bit, seguindo os tempos exatos que o datasheet do DS18B20 exige para cada "slot" de bit.
- **`t_write_byte` / `t_read_byte`** — montam/desmontam um byte completo a partir de 8 chamadas às funções de bit, respeitando a ordem LSB-primeiro do protocolo.
- **`confirm_Sdigit`** — calcula o checksum CRC-8 (padrão Maxim, polinômio `0x8C`) sobre os bytes recebidos e compara com o byte de checksum que o próprio sensor envia junto, para detectar corrupção na transmissão.
- **`t_wake_up`** — envia os comandos que instruem o sensor a iniciar uma conversão de temperatura (o processo demora até ~750 ms na resolução padrão de 12 bits).
- **`t_read_sensor`** — lê o "scratchpad" de 9 bytes do sensor, valida o CRC, descarta leituras corrompidas ou vazias, e converte os dois bytes de temperatura (em complemento de dois, escala 1/16 °C) para um `float` em graus Celsius.

### API pública
```c
void DS18B20_init(void);
bool DS18B20_read(float *out_celsius);
```
`DS18B20_init` configura o pino e testa a presença do sensor uma vez, no início do programa. `DS18B20_read` dispara a conversão, aguarda o tempo necessário e devolve a leitura via ponteiro, retornando `false` em caso de falha (sensor ausente, CRC inválido, etc.).

---

## `humidity.c` / `humidity.h` — Sensor de temperatura e umidade DHT22

Lê temperatura e umidade de um sensor DHT22, também por bit-banging, mas com um protocolo próprio do fabricante (diferente do 1-Wire): a informação de cada bit é codificada pela **duração do nível alto** de um pulso, não pela alternância de nível como no DS18B20.

- **`pin_config`** — mesma ideia do módulo de temperatura: pino em dreno aberto com pull-up.
- **`bit_length`** — espera a linha atingir um nível específico e devolve quanto tempo (em microssegundos) isso levou, ou `-1` se estourar um timeout — usado tanto para sincronizar o início da comunicação quanto para medir a duração de cada bit.
- **`h_read_data`** — executa a sequência de "acordar" o sensor, espera a resposta de presença (três transições de nível) e lê os 40 bits (5 bytes) do quadro de dados, decidindo bit a bit se a duração do nível alto correspondeu a um 0 (~26 µs) ou um 1 (~70 µs), usando 45 µs como limiar.
- **`confirm_Sdigit`** — verificação de integridade mais simples que o CRC do DS18B20: soma os 4 primeiros bytes e compara com o 5º byte enviado pelo sensor como checksum.
- Conversão dos valores: separa os 16 bits de umidade e os 16 bits de temperatura, dividindo por 10 para obter uma casa decimal; a temperatura usa o bit mais significativo como sinalizador de valor negativo (diferente do complemento de dois do DS18B20).

### API pública
```c
void DHT22_init(void);
bool DHT22_read(dht22_reading_t *out);
```
`dht22_reading_t` é uma struct com dois campos (`temperature`, `humidity`), já que o sensor sempre lê os dois valores juntos em uma única transação.

---

## `identify.cpp` — Identificação e captura de sinais infravermelhos

Módulo baseado na biblioteca `IRremoteIDF` do github de Jorgecis que escuta continuamente o receptor IR (TSOP) e decodifica qualquer sinal recebido, imprimindo:
- uma descrição legível do protocolo detectado;
- se for um sinal de ar-condicionado suportado, um resumo dos parâmetros (modo, temperatura, ventilação etc.);
- o código-fonte já pronto do sinal bruto capturado (útil para reproduzir o mesmo sinal depois).

Diferente dos sensores, roda como uma **tarefa em loop infinito** (`identify_task`), porque não há como prever quando um sinal IR vai aparecer — o módulo precisa estar sempre "ouvindo".

### API pública
```c
void identify_init(void);
```

---

## `emiter.cpp` / `emiter.h` — Emissor de comandos IR para o ar-condicionado

O atuador principal do projeto: monta e transmite comandos infravermelhos que imitam o controle remoto de um ar-condicionado Philco, usando o protocolo **GOODWEATHER** e a classe `IRGoodweatherAc` da biblioteca `IRremoteIDF`.

### Também baseado na biclioteca `IRremoteIDF` do github de Jorgecis
O `emiter.h` expõe uma API simplificada (`emiter_cmd_t`) com apenas os campos que o protocolo real possui — power, modo, temperatura, ventilação, oscilação da aleta e três "toggles" (turbo, luz do painel, sleep). Cada campo em zero/"KEEP" significa "não mexer nesse campo". Isso isola o resto do sistema dos detalhes internos da biblioteca.

### Peças internas
- **`native_mode` / `native_fan` / `native_swing`** — funções tradutoras que convertem os enums da API pública para as constantes que a biblioteca espera, validando se o valor pedido é suportado pelo protocolo.
- **`validate`** — confere todos os campos de um comando antes de qualquer transmissão, incluindo a faixa de temperatura aceita pelo protocolo.
- **`convert_to_LARA`** — monta e transmite manualmente o quadro IR completo (cabeçalho, 6 bytes cada um seguido do seu complemento de bits, e rodapé), usando **timings medidos diretamente do controle remoto real** via captura com o TSOP, em vez dos timings genéricos da biblioteca. Isso foi necessário porque os tempos padrão da biblioteca não batiam exatamente com o sinal do controle original.
- **`send_frame`** — envia um frame único, respeitando uma pausa mínima entre frames consecutivos de um mesmo comando.
- **`send_temp`** — trata a mudança de temperatura, que no protocolo real não é um valor absoluto, mas sim os botões "Up"/"Down"; pode operar em modo de salto direto ou grau a grau (configurável em tempo de compilação).
- **`emiter_init`** — cria o objeto de estado do ar-condicionado e o objeto de envio de baixo nível, carregando como estado inicial o quadro exato capturado do controle remoto real (ligado, modo frio, 24 °C, ventilação automática, oscilação lenta).
- **`send_ir_command`** — a função central: para cada campo preenchido em um comando, aplica a mudança no objeto de estado e transmite um frame, sempre na ordem liga → modo → temperatura → ventilação → oscilação → toggles → desliga, protegida por um mutex para que dois comandos nunca sejam processados ao mesmo tempo.

Um detalhe importante do protocolo identificado: cada frame carrega o **estado completo** do ar-condicionado, não apenas o campo alterado — por isso o objeto interno precisa manter esse estado persistente entre chamadas.

### API pública
```c
bool emiter_init(void);
bool send_ir_command(const emiter_cmd_t *cmd);
```

---

## `curtain.c` / `curtain.h` — Estimativa de ocupação (placeholder)

Simula, por enquanto, o sensor de ocupação que futuramente será uma barreira infravermelha própria, instalada em outro microcontrolador e comunicada por ESP-NOW (contando entradas e saídas de pessoas pela porta). Enquanto essa comunicação não está pronta, dois botões físicos fazem esse papel manualmente:

- Botão do GPIO 7 sozinho → simula "uma pessoa entrou" (`carga_termica = 1`) e acende o LED integrado da placa em vermelho por um instante.
- Botão do GPIO 6 sozinho → simula "uma pessoa saiu" (`carga_termica = -1`) e acende o LED em azul.
- Os dois pressionados juntos → evento neutro (`carga_termica = 0`), com o LED em roxo.

A leitura dos botões é feita por *polling* (verificação periódica do nível do pino) com debounce, dentro de uma tarefa (`curtain_task`) que roda continuamente detectando a borda de "botão acabou de ser pressionado". A variável `carga_termica` funciona como uma "caixa de correio" de um único evento: o loop principal do programa (`main.c`) é responsável por ler e zerar esse valor a cada ciclo, evitando contar o mesmo evento mais de uma vez.

O controle do LED usa o componente `led_strip` (protocolo WS2812), necessário porque o LED integrado da placa não é um simples GPIO liga/desliga, e sim um dispositivo endereçável com protocolo de dados próprio.

### API pública
```c
extern int16_t carga_termica;
void curtain_init(void);
```

---

## `main.c` — Ponto de entrada

Inicializa todos os módulos (`curtain_init`, `emiter_init`, `DHT22_init`, `DS18B20_init`) e roda um laço principal que, a cada 100 ms:
1. Consome o evento de ocupação pendente (`carga_termica`) e atualiza um contador acumulado (`ocupacao`).
2. Lê a temperatura do DS18B20 e a temperatura/umidade do DHT22.
3. Imprime os valores lidos.

Este arquivo ainda não contém a lógica de decisão automática do ar-condicionado (quando ligar, mudar temperatura, modo, etc. com base nas leituras) — atualmente ele só coleta e exibe os dados dos sensores e da estimativa de ocupação. A integração entre essas leituras e o `emiter` (comandos ao ar-condicionado) é o próximo passo do projeto.

---
