# Sistema IoT com Estimativa de Ocupação CSI para Automação da Climatização Predial — ESP32-C6

Sistema embarcado multi-placa em ESP32-C6 (ESP-IDF v6.0) que monitora um ambiente (temperatura, umidade e ocupação aproximada), controla um ar-condicionado por infravermelho imitando o controle remoto original, e coordena esses dados entre várias unidades ESP32 através de Wi-Fi/MQTT e ESP-NOW.

Este documento une o que antes eram dois projetos separados — **sensores/atuadores** e **rede** — agora integrados em um único firmware (`main.c`) que roda em cada placa da instalação.

---

## Visão geral do hardware

| Periférico | GPIO | Arquivo |
|---|---|---|
| DS18B20 (temperatura, 1-Wire) | 0 | `temperature.c/h` |
| DHT22 (temperatura + umidade) | 1 | `humidity.c/h` |
| TSOP (receptor IR) | 4 | `identify.cpp/h` |
| LED IR (via transistor, emissor) | 5 | `emiter.cpp/h` |
| Botões (placeholder de ocupação) | 6 e 7 | `curtain.c/h` |
| LED RGB integrado da placa | 8 | `curtain.c` |

Cada sensor é implementado como uma **função comum, chamada sob demanda** (não uma tarefa em loop): quem quiser uma leitura chama a função e recebe o resultado na hora, sem depender de um valor "cacheado" atualizado em segundo plano. Isso dá controle total sobre quando cada leitura acontece e evita consumo de CPU invisível. A exceção é o módulo de identificação de IR (`identify.cpp`), que roda como tarefa contínua porque não há como prever quando um sinal infravermelho vai chegar. O mesmo vale para a estimativa de ocupação (`curtain.c`), que roda como tarefa por fazer *polling* contínuo dos botões.

---

## Visão geral da rede

Cada instalação é composta por várias placas ESP32-C6 idênticas (mesmo firmware), cada uma responsável por um ambiente/nó. A coordenação entre elas segue este fluxo:

1. Ao ligar, cada ESP32 descobre **quem ele é** comparando seu próprio MAC de fábrica com uma tabela fixa de dispositivos conhecidos (`MAC.c/h`).
2. Em seguida, conecta ao Wi-Fi, ao broker MQTT e inicializa o ESP-NOW para falar com os outros ESPs da rede local (`coms.c/h`).
3. O laço principal (`main.c`) usa duas vias de comunicação com propósitos diferentes:
   - **"Intercom"** — via MQTT, para trocar dados com um serviço externo (ex.: um servidor/dashboard na internet, como o ThingSpeak configurado em `MQTT.h`).
   - **"Intracom"** — via ESP-NOW, para trocar mensagens diretamente entre os ESPs da instalação, sem depender de internet.
4. Com base nos dados dos sensores locais e nas instruções recebidas do broker, cada placa decide quando acionar o ar-condicionado do seu ambiente através do `emiter`.

---

## `temperature.c` / `temperature.h` — Sensor de temperatura DS18B20

Lê a temperatura ambiente de um sensor DS18B20 conectado por **1-Wire**, um protocolo digital de um único fio onde o mestre (o ESP32) e o sensor se revezam controlando a linha, codificando informação na duração de cada pulso elétrico.

O arquivo implementa o protocolo inteiro "na mão" (bit-banging), sem biblioteca externa:

- **`pin_config`** — configura o GPIO como entrada/saída de dreno aberto (necessário porque o mesmo fio é usado tanto para escrever quanto para ler), com pull-up habilitado como reforço ao resistor externo de 4,7 kΩ.
- **`t_wellnesscheck`** — executa o pulso de reset do 1-Wire e verifica se o sensor respondeu com o pulso de presença, dentro de uma seção crítica (`portENTER_CRITICAL`/`portEXIT_CRITICAL`) para garantir que o timing em microssegundos não seja interrompido pelo escalonador do RTOS.
- **`t_write_bit` / `t_read_bit`** — escrevem ou leem um único bit, seguindo os tempos exatos que o datasheet do DS18B20 exige para cada "slot" de bit.
- **`t_write_byte` / `t_read_byte`** — montam/desmontam um byte completo a partir de 8 chamadas às funções de bit, respeitando a ordem LSB-primeiro do protocolo.
- **`confirm_Sdigit`** — calcula o checksum CRC-8 (padrão Maxim, polinômio `0x8C`) sobre os bytes recebidos e compara com o byte de checksum que o próprio sensor envia junto, para detectar corrupção na transmissão.
- **`t_wake_up`** — envia os comandos que instruem o sensor a iniciar uma conversão de temperatura (o processo demora até ~800 ms na resolução padrão de 12 bits).
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

## `identify.cpp` / `identify.h` — Identificação e captura de sinais infravermelhos

Módulo baseado na biblioteca `IRremoteIDF` (github de Jorgecis) que escuta continuamente o receptor IR (TSOP) e decodifica qualquer sinal recebido, imprimindo:
- uma descrição legível do protocolo detectado;
- se for um sinal de ar-condicionado suportado, um resumo dos parâmetros (modo, temperatura, ventilação etc.);
- o código-fonte já pronto do sinal bruto capturado (útil para reproduzir o mesmo sinal depois).

Diferente dos sensores, roda como uma **tarefa em loop infinito** (`identify_task`), porque não há como prever quando um sinal IR vai aparecer — o módulo precisa estar sempre "ouvindo". Ferramenta útil principalmente durante o desenvolvimento, para capturar os timings do controle remoto original usados depois em `emiter.cpp`.

### API pública
```c
void identify_init(void);
```

---

## `emiter.cpp` / `emiter.h` — Emissor de comandos IR para o ar-condicionado

O atuador principal do projeto: monta e transmite comandos infravermelhos que imitam o controle remoto de um ar-condicionado Philco, usando o protocolo **GOODWEATHER** e a classe `IRGoodweatherAc` da biblioteca `IRremoteIDF`.

O `emiter.h` expõe uma API simplificada (`emiter_cmd_t`) com apenas os campos que o protocolo real possui — power, modo, temperatura, ventilação, oscilação da aleta e três "toggles" (turbo, luz do painel, sleep). Cada campo em zero/`*_KEEP` significa "não mexer nesse campo". Isso isola o resto do sistema dos detalhes internos da biblioteca.

### Peças internas
- **`native_mode` / `native_fan` / `native_swing`** — funções tradutoras que convertem os enums da API pública para as constantes que a biblioteca espera, validando se o valor pedido é suportado pelo protocolo.
- **`validate`** — confere todos os campos de um comando antes de qualquer transmissão, incluindo a faixa de temperatura aceita pelo protocolo.
- **`convert_to_LARA`** — monta e transmite manualmente o quadro IR completo (cabeçalho, 6 bytes cada um seguido do seu complemento de bits, e rodapé), usando **timings medidos diretamente do controle remoto real** via captura com o TSOP (módulo `identify`), em vez dos timings genéricos da biblioteca. Isso foi necessário porque os tempos padrão da biblioteca não batiam exatamente com o sinal do controle original.
- **`send_frame`** — envia um frame único, respeitando uma pausa mínima (`interframe`) entre frames consecutivos de um mesmo comando.
- **`send_temp`** — trata a mudança de temperatura, que no protocolo real não é um valor absoluto, mas sim os botões "Up"/"Down"; pode operar em modo de salto direto ou grau a grau (configurável em tempo de compilação via `temp_step`).
- **`emiter_init`** — cria o objeto de estado do ar-condicionado e o objeto de envio de baixo nível, carregando como estado inicial o quadro exato capturado do controle remoto real (ligado, modo frio, 24 °C, ventilação automática, oscilação lenta).
- **`send_ir_command`** — a função central: para cada campo preenchido em um comando, aplica a mudança no objeto de estado e transmite um frame, sempre na ordem liga → modo → temperatura → ventilação → oscilação → toggles → desliga, protegida por um mutex para que dois comandos nunca sejam processados ao mesmo tempo.

Um detalhe importante do protocolo identificado: cada frame carrega o **estado completo** do ar-condicionado, não apenas o campo alterado — por isso o objeto interno precisa manter esse estado persistente entre chamadas.

### API pública
```c
bool emiter_init(void);
bool send_ir_command(const emiter_cmd_t *cmd);
// Exemplo: send_ir_command(&(emiter_cmd_t){ .power = EMITER_ON });
```

---

## `curtain.c` / `curtain.h` — Estimativa de ocupação (placeholder)

Simula, por enquanto, o sensor de ocupação que futuramente será uma barreira infravermelha própria, instalada em outro microcontrolador e comunicada por ESP-NOW (contando entradas e saídas de pessoas pela porta). Enquanto essa comunicação não está pronta, dois botões físicos fazem esse papel manualmente:

- Botão do GPIO 7 sozinho → simula "uma pessoa entrou" (`carga_termica = 1`) e acende o LED integrado da placa em vermelho por um instante.
- Botão do GPIO 6 sozinho → simula "uma pessoa saiu" (`carga_termica = -1`) e acende o LED em azul.
- Os dois pressionados juntos → evento neutro (`carga_termica = 0`), com o LED em roxo.

A leitura dos botões é feita por *polling* (verificação periódica do nível do pino) com debounce, dentro de uma tarefa (`curtain_task`) que roda continuamente detectando a borda de "botão acabou de ser pressionado". A variável `carga_termica` funciona como uma "caixa de correio" de um único evento: o loop principal do programa (`main.c`) é responsável por ler e zerar esse valor a cada ciclo, acumulando-o em `data[3]` (ocupação) e evitando contar o mesmo evento mais de uma vez.

O controle do LED usa o componente `led_strip` (protocolo WS2812), necessário porque o LED integrado da placa não é um simples GPIO liga/desliga, e sim um dispositivo endereçável com protocolo de dados próprio.

### API pública
```c
extern int16_t carga_termica;
void curtain_init(void);
```

---

## `MAC.c` / `MAC.h` — Identidade do dispositivo

Resolve um problema comum em sistemas com múltiplas placas idênticas rodando o mesmo firmware: **como cada ESP32 sabe qual é o seu papel** na rede, sem precisar gravar um firmware diferente para cada uma?

A solução usada aqui é uma tabela fixa, escrita no código, que associa o **endereço MAC de fábrica** de cada placa (único e imutável por dispositivo) a um índice/papel na rede:

```c
typedef struct {
    int com;
    uint8_t mac[6];
    bool Iam;
} ESP_t;
```

- `mac` — o endereço MAC de 6 bytes daquele ESP específico.
- `com` — um identificador numérico associado a esse dispositivo (por exemplo, um número de sala ou de nó).
- `Iam` — marcado como `true` para a entrada que corresponde ao dispositivo que está rodando o código agora.

A tabela `ESP[]`, em `MAC.c`, lista os MACs de todos os ESP32 conhecidos da instalação.

**`mac_init()`** lê o MAC de fábrica do dispositivo atual (`esp_read_mac`, usando a interface Wi-Fi como referência), percorre a tabela `ESP[]` procurando uma correspondência exata, e:
- marca `Iam = true` na entrada correspondente;
- guarda o índice encontrado na variável global `ESP_Iam` (ou deixa em `-1` se o MAC não constar na tabela — ou seja, uma placa "desconhecida" rodando o firmware).

`NUM_ESPS` é calculado automaticamente a partir do tamanho do array, então adicionar ou remover dispositivos da tabela não exige atualizar nenhum número manualmente.

`discover_mac()` é uma função auxiliar de depuração que apenas imprime o MAC de fábrica do dispositivo, útil para descobrir o endereço de uma placa nova antes de cadastrá-la na tabela `ESP[]`.

### API pública
```c
extern const int NUM_ESPS;
extern ESP_t ESP[];
extern int ESP_Iam;
void mac_init(void);
void discover_mac(void);
```

---

## `coms.c` / `coms.h` — Conectividade e troca de mensagens

Concentra toda a configuração de rede (Wi-Fi, MQTT, ESP-NOW) e expõe funções simples para enviar/receber dados, escondendo os detalhes de cada protocolo do resto do programa.

### Inicialização (`coms_init`)
Executa, em sequência:
1. `nvs_flash_init()` — inicializa o armazenamento não volátil, exigido internamente pelo driver de Wi-Fi do ESP-IDF.
2. `mac_init()` — descobre a identidade do dispositivo (visto acima).
3. `credentials_init()` — carrega, a partir da tabela `MQTT` definida em `MQTT.h`, as credenciais específicas do dispositivo (hostname, usuário, senha, client id, tópicos de publicação/assinatura), indexadas por `ESP_Iam`.
4. Configura e conecta o Wi-Fi em modo estação (`WIFI_MODE_STA`), aguardando bloqueado até a conexão ser confirmada pelo `event_handler`.
5. Configura e conecta o cliente MQTT, também aguardando bloqueado até a confirmação de conexão.
6. Monta a lista de "slaves" do ESP-NOW — todos os ESPs da tabela `ESP[]` exceto o próprio dispositivo — e registra cada um como *peer* do ESP-NOW, permitindo o envio direto de mensagens entre eles sem precisar de roteador.

### Callbacks de evento
- **`event_handler`** — reage a eventos do Wi-Fi: inicia a conexão quando a interface sobe, marca como conectado ao obter IP, e tenta reconectar automaticamente em caso de queda.
- **`mqtt_event_handler`** — reage a eventos do cliente MQTT: marca conexão/desconexão, inscreve-se no tópico configurado (`MQTTsub`) ao conectar, e copia qualquer mensagem recebida para o buffer `mqtt_received`.
- **`receive_msg`** — callback do ESP-NOW, acionado sempre que outro ESP da rede local envia algo; copia a mensagem recebida para o buffer `received_msg`.

### Funções de envio/recebimento
| Função | Via | Propósito |
|---|---|---|
| `intercom_send(data)` | MQTT | Publica um array de 3 floats como uma string `field1=...&field2=...&field3=...`, formato compatível com serviços de dashboard que aceitam esse padrão de campos (ex.: ThingSpeak). |
| `wake_up(data)` | MQTT | Publica no mesmo formato, mas em um tópico diferente (`MQTT.pub[0]`), usado para tentar "acordar"/alertar o `ESP0` via MQTT. |
| `intercom_read(out)` | MQTT | Copia a última mensagem MQTT recebida para o buffer do chamador e limpa o buffer interno. |
| `intracom_send(data, slave_index)` | ESP-NOW | Envia um único valor float (convertido em texto) a um ESP específico da rede local (`slave_index`), ou a todos de uma vez (`slave_index == -1`). |
| `intracom_read(out_msg)` | ESP-NOW | Copia a última mensagem local recebida para o buffer do chamador e limpa o buffer interno. |

### API pública
```c
void coms_init(void);
void wake_up(const float *data);
void intercom_send(const float *data);
void intercom_read(char *out);
void intracom_send(const float *data, int slave_index);
void intracom_read(char *out_msg);
```

### Dependência externa: `MQTT.h`
Concentra as credenciais e a configuração de rede usadas por `coms_init` e pelas funções de publicação: `SSID`, `SSIDp`, `MQTT_BROKER`, `MQTT_port`, e a struct `MQTT_t` com um conjunto de credenciais (hostname, usuário, senha, client id, tópicos) por dispositivo, indexado por `ESP_Iam`. A função `credentials_init()` copia os valores correspondentes ao dispositivo atual para variáveis globais (`HOSTNAME`, `MQTTu`, `MQTTp`, `MQTTc`, `MQTTid`, `MQTTpub`, `MQTTsub`), usadas depois por `coms_init`.

---

## `main.c` — Ponto de entrada e lógica de coordenação

Inicializa todos os módulos (`curtain_init`, `emiter_init`, `DHT22_init`, `DS18B20_init`, `coms_init`) e roda um laço principal a cada 200 ms que integra leitura de sensores, comunicação em rede e controle do ar-condicionado.

### Estado principal
- **`data[]`** — vetor de 5 floats com o estado local do dispositivo: `{DS18B20, DHT22_t, DHT22_h, Ocupação, Mensagens}`.
- **`old_data[]`** — cópia do estado da iteração anterior, usada para detectar a transição de ocupação de 0 para 1 (chegada de alguém no ambiente).
- **`temp_alvo`** / **`last_temp`** — temperatura desejada recebida do broker e a última temperatura efetivamente enviada ao ar-condicionado, usadas para só reenviar um comando IR quando o alvo realmente muda.
- **`broker_msg`** — código de mensagem recebido do broker para o dispositivo atual; o valor `9` é a convenção usada para "desligar o ar-condicionado".
- **`HVACon`** — flag que rastreia se o ar-condicionado está, do ponto de vista deste firmware, ligado ou desligado, evitando reenviar comandos de power redundantes e permitindo repetir a tentativa em caso de falha de transmissão.
- **`command`** — código de comando decodificado da última instrução recebida via MQTT, usado para decidir se uma resposta deve ser enviada ao broker.

### Ciclo do laço principal
1. **Comunicação MQTT — despertar (a cada nova ocupação detectada)**: quando `data[3]` passa de `0` para `1` (chegada de alguém), o dispositivo `ESP0` avisa os demais via `intracom_send`, publica seus dados via `intercom_send`, e liga o ar-condicionado (`send_ir_command` com `power = EMITER_ON`), atualizando `HVACon`.
2. **Obtenção de dados** (a cada `data_interval` = 8 s): consome o evento pendente de `carga_termica` (zerando-o), acumula-o em `data[3]`, e lê `DS18B20_read`/`DHT22_read`, atualizando `data[0..2]` quando a leitura é válida.
3. **Envio periódico ao broker** (a cada `broker_interval` = 15 s): se houver um `command == 1` pendente (pedido do broker), publica `data` via `intercom_send`.
4. **Controle do HVAC** (a cada `HVAC_interval` = 17 s):
   - Lê uma eventual instrução do broker (`intercom_read`), decodificando a string de 10 caracteres `instrucao` (formato `ABCDEFGHIJ`) em três temperaturas-alvo e três mensagens (uma tripla por ESP da rede) mais um código de comando.
   - Atualiza `temp_alvo` e `broker_msg` com os valores correspondentes a `ESP_Iam`.
   - Se `temp_alvo` mudou desde o último envio, transmite um comando de temperatura ao ar-condicionado (`send_ir_command` com `temp_c`).
   - Se `broker_msg == 9` e o ar-condicionado está marcado como ligado (`HVACon`), transmite o comando de desligar; em caso de sucesso, zera `broker_msg` e marca `HVACon = false`, evitando repetir o comando até uma nova instrução do broker pedir isso de novo — e retentando automaticamente no próximo ciclo caso a transmissão falhe.
5. **Comunicação ESP-NOW**: lê `intracom_read` e, se a mensagem for o valor de sincronização (`100.0`), atualiza `LastBrokerMsg`, sinalizando que este dispositivo está sincronizado com o `ESP0`.

Todos os comandos ao ar-condicionado são montados com `memset(&cmd, 0, sizeof(cmd))` seguido do preenchimento apenas dos campos desejados, para garantir que campos não usados fiquem no valor `*_KEEP` e não sejam alterados sem intenção — já que `emiter_cmd_t cmd = {0}` não pode ser reatribuído dessa forma em C fora da declaração.

---

## Dependências externas de bibliotecas

- **`IRremoteIDF`** (github de Jorgecis) — biblioteca usada por `emiter.cpp` (classe `IRGoodweatherAc`, `IRsend`) e `identify.cpp` (`IRrecv`, `IRutils`, `IRac`) para codificação/decodificação de sinais infravermelhos.
- **`led_strip`** — componente do ESP-IDF usado por `curtain.c` para controlar o LED RGB endereçável (WS2812) integrado da placa.

## Estrutura de arquivos

```
main/
├── main.c              # Ponto de entrada e coordenação geral
├── MAC.c / MAC.h        # Identidade do dispositivo (tabela de MACs)
├── coms.c / coms.h      # Wi-Fi, MQTT e ESP-NOW
├── MQTT.h               # Credenciais e configuração de rede (sensível)
├── temperature.c / .h   # Sensor DS18B20 (1-Wire)
├── humidity.c / .h      # Sensor DHT22
├── identify.cpp / .h    # Captura e decodificação de sinais IR
├── emiter.cpp / .h      # Emissor de comandos IR para o ar-condicionado
└── curtain.c / .h       # Estimativa de ocupação (placeholder) + LED RGB
```
