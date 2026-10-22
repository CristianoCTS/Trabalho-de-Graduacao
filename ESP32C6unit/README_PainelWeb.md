# Painel Web + Servidor: como as variáveis se movem

O **Painel Web** (`thingspeakV11.html`, página estática no Netlify) é a interface do operador. Quem publica no ThingSpeak é o **Servidor**. O painel só envia uma lista de dados ao servidor e continua **lendo** os gráficos direto do ThingSpeak (TS).

```
                  lista (POST /api/lista)                 update (field1..4)
  Usuário ──► Painel Web ───────────────────► Servidor ───────────────────► TS
                  ▲   ◄──── aceita/recusada ────┘   ◄──── nº do registro ────┘
                  │
                  └────────── leituras (gráficos e valores iniciais) ◄───────── TS
```

---

## Tabela de movimentação de variáveis

| Elemento | Adquire | Recebe | De | Processa | Envia | Para |
|---|---|---|---|---|---|---|
| **Painel Web** | Não atribuído | 1. Temperatura desejada (16 a 27 °C)<br>2. Tipo de controlador (1 a 5)<br>3. Estado ON/OFF<br>4. Gasto energético<br>5. Senha<br>6. Temperatura por área<br>7. Ocupação<br>8. Avisos<br>9. Gasto por controlador<br>10. Último controlador, temperatura e estado (ao abrir a página)<br>11. Resposta do envio (aceita ou recusada, com o motivo) | 1 a 5. Usuário<br>6. TS (canais dos ARs)<br>7 a 9. TS (canal do broker)<br>10. TS (canal de controle)<br>11. Servidor | Valida a temperatura (16 a 27) e a senha<br>Campo vazio vira `null`<br>Gasto igual ao último enviado vira `null`<br>Ao abrir, usa os valores do item 10; se faltarem, usa Direto, 23 °C e OFF<br>Atualiza os gráficos a cada 20 s | 1. Lista: controlador, temperatura, estado, gasto e senha (ao clicar em Confirmar) | 1. Servidor |
| **Servidor** | Não atribuído | 1. Lista do painel<br>2. Chave de escrita, senha e origem permitida<br>3. Último estado salvo<br>4. Nº do registro criado (0 = recusado) | 1. Painel Web<br>2. Arquivo `.env`<br>3. Arquivo `estado.json`<br>4. TS | Valida a lista; temperatura `null` mantém a anterior<br>Exige senha se o controlador ou o estado mudou<br>Detecta subida (0→1), descida (1→0) e gasto novo<br>Estado 1: publica a cada 17 s<br>Após a descida: mais 4 envios e para<br>Subida ou gasto novo: publica na hora<br>Nunca dois envios a menos de 17 s um do outro<br>Se o TS recusar, mantém as pendências e tenta de novo | 1. field1 controlador, field2 temperatura, field4 estado e field3 gasto (só quando novo)<br>2. Resposta ao envio (200, 400, 401, 403, 413 ou 429)<br>3. Última lista (`GET /api/estado`; o painel não usa hoje) | 1. TS (canal de controle)<br>2. Painel Web<br>3. Quem consultar |
| **ThingSpeak (TS)** | Não atribuído | 1. field1 a field4 do canal de controle<br>2. Temperatura por área, ocupação, avisos e gasto por controlador | 1. Servidor<br>2. Unidades ESP e broker | Guarda o histórico<br>Aceita no máximo 1 atualização a cada 15 s por canal | 1. Nº do registro (0 = recusado)<br>2. Último controlador, temperatura e estado (fields 1, 2 e 4 do canal de controle)<br>3. Temperatura por área (field 1 dos canais dos ARs)<br>4. Ocupação (field 6), avisos (field 8) e gasto por controlador (fields 3, 4 e 5) do canal do broker | 1. Servidor<br>2 a 4. Painel Web (leitura direta, com a chave de leitura de cada canal) |

---

## Canais e fields

 Papel | Fields |
|---|---|---|
| Controle (escrito só pelo servidor) | 1 = controlador, 2 = temperatura desejada, 3 = gasto energético (só quando há gasto novo), 4 = estado do sistema |
| Ar 1 e Ar 2 | field 1 = temperatura da área |
| Broker | fields 3, 4 e 5 = gasto durante os controladores 1, 2 e 3; field 6 = ocupação; field 8 = avisos |

Controladores: 1 LigaDesliga, 2 PI, 3 Adaptativo, 4 Degrau, 5 Direto. Os controladores 4 e 5 não têm field de gasto, então o gráfico de gasto fica vazio para eles.

## A lista que o painel envia

```json
{ "controlador": 2, "temperatura": 22, "estado": 1, "gasto": null, "senha": null }
```

- `null` significa "não preenchido". Para `temperatura`, o servidor mantém a temperatura anterior; para `gasto`, não há gasto novo.
- A temperatura fora de 16 a 27 °C e a senha errada bloqueiam o envio inteiro no painel.
- Estado: 1 = ligado, 0 = desligado.

## Quando o servidor publica no TS

| Situação | O que acontece |
|---|---|
| Estado 1 | Publica a cada 17 s |
| Estado vai de 1 para 0 | Mais 4 envios (17 s entre si), depois para |
| Estado vai de 0 para 1 | Publica na hora e volta ao ciclo de 17 s |
| Gasto novo (número diferente do último enviado) | Publica uma vez com o field 3, mesmo com estado 0 |
| Estado 0 e sem rodadas restantes | Não publica; mudanças ficam guardadas e saem na próxima subida |

Se um envio for recusado pelo TS (resposta `0`) ou falhar, as pendências são mantidas e a nova tentativa vem no próximo envio permitido.

## Senha

- O **painel** confere a senha digitada com a do seu `CONFIG` e barra na hora. Ela é exigida quando o controlador ou o toggle mudou; se for digitada em qualquer caso, precisa estar certa.
- O **servidor** confere de novo com a senha do `.env`, só quando o controlador ou o estado mudam em relação à última lista dele (ou quando ele ainda não tem lista). Se estiver errada, responde 401. Depois de 10 senhas erradas em 15 minutos, bloqueia novas tentativas por um tempo.
- Temperatura e gasto não pedem senha.

