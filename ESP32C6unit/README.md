# Módulo de Comunicação Multi-ESP (Wi-Fi + MQTT + ESP-NOW)

Este projeto coordena vários ESP32 na mesma instalação: cada placa se identifica pelo próprio endereço MAC, troca dados locais entre si por **ESP-NOW** e publica/recebe informações de fora pela internet via **MQTT**. Este documento descreve o papel de cada arquivo e como as peças se encaixam.

## Visão geral do fluxo

1. Ao ligar, cada ESP32 descobre **quem ele é** comparando seu próprio MAC com uma tabela fixa de dispositivos conhecidos (`MAC.c/h`).
2. Em seguida, conecta ao Wi-Fi, ao broker MQTT e inicializa o ESP-NOW para falar com os outros ESPs da rede local (`coms.c/h`).
3. O laço principal (`main.c`) usa duas vias de comunicação com propósitos diferentes:
   - **"Intercom"** — via MQTT, para trocar dados com um serviço externo (ex.: um servidor/dashboard na internet).
   - **"Intracom"** — via ESP-NOW, para trocar mensagens diretamente entre os ESPs da instalação, sem depender de internet.

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

### API pública
```c
extern const int NUM_ESPS;
extern ESP_t ESP[];
extern int ESP_Iam;
void mac_init(void);
```

---

## `coms.c` / `coms.h` — Conectividade e troca de mensagens

Concentra toda a configuração de rede (Wi-Fi, MQTT, ESP-NOW) e expõe funções simples para enviar/receber dados, escondendo os detalhes de cada protocolo do resto do programa.

### Inicialização (`coms_init`)
Executa, em sequência:
1. `nvs_flash_init()` — inicializa o armazenamento não volátil, exigido internamente pelo driver de Wi-Fi do ESP-IDF.
2. `mac_init()` — descobre a identidade do dispositivo (visto acima).
3. `credentials_init()` — carrega as credenciais de rede (SSID, senha, dados do broker MQTT); essas definições vêm de um cabeçalho externo (`MQTT.h`) não incluído nesta pasta, que concentra strings sensíveis como senha de Wi-Fi e credenciais do MQTT.
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
| `intercom_send(data)` | MQTT | Publica um array de 3 floats como uma string `field1=...&field2=...&field3=...`, formato compatível com serviços de dashboard que aceitam esse padrão de campos. |
| `wake_up(data)` | MQTT | Publica no mesmo formato, mas em um tópico diferente (`MQTT.pub[0]`), usado para tentar "acordar"/alertar via MQTT. |
| `intercom_read(out)` | MQTT | Copia a última mensagem MQTT recebida para o buffer do chamador e limpa o buffer interno. |
| `intracom_send(data, slave_index)` | ESP-NOW | Envia um único valor float (convertido em texto) a um ESP específico da rede local (`slave_index`), ou a todos de uma vez (`slave_index == -1`). |
| `intracom_read(out_msg)` | ESP-NOW | Copia a última mensagem local recebida para o buffer do chamador e limpa o buffer interno. |

---

## `main.c` — Lógica de coordenação

Orquestra os dois canais de comunicação em um laço contínuo, com uma lógica de sincronização e monitoramento de "presença" entre um ESP considerado principal (índice 0 na tabela, tratado como `ESP0`) e os demais.

Principais elementos:
- **`data[]`** — vetor de 3 floats representando o estado local do dispositivo: temperatura, ocupação e um terceiro campo de "mensagens", conforme o comentário no código.
- **`old_data[]`** — cópia do estado da iteração anterior, usada para detectar mudanças (por exemplo, uma transição de ocupação de 0 para 1).
- **Sincronização inicial**: quando a ocupação passa de 0 para 1, o dispositivo (se for o `ESP0`, índice 0) avisa os demais via `intracom_send` e publica seus dados via `intercom_send`, registrando o instante em `LastTime`.
- **Leitura de instruções remotas**: `intercom_read` recebe uma string de 10 caracteres (`instrucao`) codificando três pares de números, três dígitos de mensagem e um código de comando (formato comentado no código como `ABCDEFGHIJ`), decodificados caractere a caractere com `atoi`/aritmética de char.
- **Janela de heartbeat**: se um determinado intervalo de tempo (`MinInterval`) se passa sem confirmação do `ESP0`, os demais dispositivos assumem que ele pode estar "apagado" (`blacked_out`) e tentam acordá-lo via `wake_up` (MQTT).
- **Leitura do canal local**: `intracom_read` verifica mensagens ESP-NOW recebidas, interpretando valores numéricos especiais (`100.0` como sinal de sincronização, `200.0` como confirmação de que o `ESP0` continua ativo).
- O laço roda a cada 200 ms (`vTaskDelay`).

> Os trechos de obtenção de dados (`data[0]++`, `data[1] = 1`) estão como placeholders/simulação no código atual — comentados como "obtido pelo sensor de temperatura" e "obtido pela cortina" — indicando que a integração real com os sensores físicos ainda será conectada a essas variáveis.

---

## Dependências externas não incluídas nesta pasta

- **`MQTT.h`** — não está entre os arquivos aqui descritos, mas é referenciado por `main.c` e `coms.c`. Deve conter as definições de credenciais e configuração usadas por `coms_init` e pelas funções de publicação: `SSID`, `SSIDp`, `HOSTNAME`, `MQTT_BROKER`, `MQTT_port`, `MQTTc`, `MQTTu`, `MQTTp`, `MQTTsub`, `MQTTpub`, além da função `credentials_init()` e da estrutura `MQTT` usada em `wake_up` (`MQTT.pub[0]`).
