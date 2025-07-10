
Projeto Final – Batalha Naval Multicliente com Sockets
=======================================================

Apresentação
------------
Este projeto propõe a implementação de uma versão simplificada do clássico jogo Batalha Naval,
utilizando comunicação via sockets TCP em C. Dois clientes se conectam a um servidor central,
que gerencia o emparelhamento e o controle da partida.

Objetivos
---------
- Aplicar conceitos de programação concorrente e distribuída com sockets.
- Praticar controle de múltiplos clientes em um servidor centralizado.
- Implementar um protocolo de comunicação customizado.
- Garantir robustez e sincronização em uma aplicação em tempo real.

Regras do Jogo
--------------
- Tabuleiro: 8x8 posições.
- Navios:
  - 1 × Submarino (tamanho 1)
  - 2 × Fragatas (tamanho 2)
  - 1 × Destroyer (tamanho 3)
- Turnos alternados entre jogadores.
- Jogada válida: coordenada (ex: "B4").
- Resultado: água, acerto ou afundamento.
- Vitória: ao afundar todos os navios do adversário.

Estrutura de Diretórios
------------------------
battleship/
├── client/           # Código do cliente
├── server/           # Código do servidor
├── common/           # Definições comuns (protocol.h)
├── Makefile          # Compilação
└── README.md         # Instruções

Compilação
----------
Use o comando `make` na raiz do projeto.

Execução
--------
1. Compile com `make`
2. Execute `./server/battleserver`
3. Execute `./client/battleclient` em duas instâncias


---

## 🔄 Protocolo de Comunicação – Diagrama e Explicação

### 1. Comando `JOIN`

```plaintext
[ Cliente A ]                          [ Servidor ]                          [ Cliente B ]
     | -------- "JOIN <nome_A>" ----------> |                                      |
     |                                      | <-------- "AGUARDE JOGADOR" -------- |
     |                                      | <-------- "JOIN <nome_B>" ---------- |
     |                                      | -------- "JOGO INICIADO" --------->  |
     | <-------- "JOGO INICIADO" ---------- | -------- "JOGO INICIADO" --------->  |
     | <-------- "Você é o Jogador 1" ----- | -------- "Você é o Jogador 2" -----> |
```

**Descrição:** Os clientes enviam `JOIN <nome>` para se registrarem no servidor. Quando dois clientes estão conectados, o servidor envia `JOGO INICIADO` e define os papéis (Jogador 1 e 2).

---

### 2. Comando `READY`

```plaintext
[ Cliente A ]                           [ Servidor ]                           [ Cliente B ]
     | -------- "READY" -------------------> |                                       |
     | <------ "AGUARDANDO ADVERSÁRIO" -----| -- "AGUARDANDO ADVERSÁRIO" --------> |
     |                                       | <------------------- "READY" --------|
     | <--------- "INÍCIO DO JOGO" --------- | --------- "INÍCIO DO JOGO" --------> |
     | <------ "Seu turno!" (Jogador 1) ---- | ---- "Aguarde o turno do outro" ---> |
```

**Descrição:** Após posicionar os navios, cada cliente envia `READY`. O jogo só começa quando ambos estiverem prontos. O servidor então informa quem começa o jogo.

---

### 3. Comando `POS <tipo> <x> <y> <orientacao>`

**Exemplo:**
```plaintext
POS FRAGATA 2 5 H
```

**Descrição:** Usado durante a fase de posicionamento. Cada cliente envia várias mensagens `POS` para informar as posições de seus navios. O servidor valida e armazena cada navio.

---

### 4. Comando `FIRE <x> <y>`

```plaintext
[ Jogador Ativo ] ---> "FIRE 3 4" ---> [ Servidor ] ---> "HIT"/"MISS"/"SUNK" ---> [ Ambos ]
```

**Descrição:** Durante seu turno, o jogador envia `FIRE` com a coordenada do ataque. O servidor responde:
- `HIT`: acerto em navio
- `MISS`: água
- `SUNK`: navio afundado

O servidor alterna os turnos automaticamente após cada ataque.

---

### 5. Comandos de Resultado Final

- `WIN`: enviado ao jogador vencedor
- `LOSE`: enviado ao jogador derrotado
- `FIM`: encerra a conexão após o fim da partida

---

## ✅ Exemplo de Fluxo Completo

1. Cliente A: `JOIN Alice`
2. Cliente B: `JOIN Bob`
3. Ambos posicionam navios com `POS`
4. Ambos enviam `READY`
5. Servidor: `INÍCIO DO JOGO`
6. Jogadores se alternam com `FIRE x y`
7. Servidor envia `HIT`, `MISS`, `SUNK`
8. Um jogador recebe `WIN`, outro `LOSE`
9. Ambos recebem `FIM`

---


---



---

### 6. Comando `PLAY`

```plaintext
[ Servidor ] ---> "PLAY 1" ---> [ Jogador 1 ]
[ Servidor ] ---> "PLAY 2" ---> [ Jogador 2 ]
```

**Descrição:** O servidor envia o comando `PLAY <id>` para indicar qual jogador tem a vez. Esse comando é fundamental para sincronizar os turnos. Apenas o jogador cujo ID foi indicado pode enviar um `FIRE`.

- `PLAY 1` → Jogador 1 pode jogar.
- `PLAY 2` → Jogador 2 pode jogar.

O jogador adversário deve permanecer aguardando.

---> "PLAY" ---> [ Jogador Ativo ]
```

**Descrição:** Enviado pelo servidor ao jogador que deve jogar no momento. Esse comando sinaliza que é o turno do jogador realizar uma jogada com `FIRE x y`.

---

### 7. Comando `END`

```plaintext
[ Servidor ] ---> "END" ---> [ Ambos os Jogadores ]
```

**Descrição:** Enviado ao final da partida para indicar o encerramento da sessão. Serve como comando de controle para que os clientes fechem a conexão e imprimam o resultado final.

---

## 📘 Resumo do Protocolo

| Comando | Origem      | Destino        | Descrição                                         |
|---------|-------------|----------------|---------------------------------------------------|
| JOIN    | Cliente     | Servidor       | Solicita entrada no jogo                          |
| READY   | Cliente     | Servidor       | Informa que o jogador posicionou seus navios      |
| POS     | Cliente     | Servidor       | Envia posição de um navio                         |
| PLAY    | Servidor    | Cliente        | Informa ao jogador que é seu turno                |
| FIRE    | Cliente     | Servidor       | Realiza ataque a uma coordenada                   |
| HIT/MISS/SUNK | Servidor | Ambos os jogadores | Informa o resultado de um ataque            |
| WIN/LOSE| Servidor    | Cliente        | Informa o resultado da partida                    |
| END     | Servidor    | Ambos          | Encerra o jogo e a comunicação                    |

---


---

## 📤 Instruções para Submissão

### 📁 Estrutura Esperada

A submissão deve conter os seguintes itens organizados da seguinte forma:

```
battleship/
├── client/
│   └── battleclient.c
├── server/
│   └── battleserver.c
├── common/
│   └── protocol.h
├── Makefile
├── README.md
└── exemplo_execucao.txt   (opcional, mas recomendado)
```

### ✅ O que deve estar incluído

- Código-fonte completo e funcional (`.c` e `.h`);
- Makefile com alvos `all` e `clean`;
- README com:
  - Regras do jogo;
  - Descrição do protocolo de comunicação;
  - Instruções de compilação e execução;
- Prints de execução ou um arquivo `exemplo_execucao.txt` com exemplo de uma partida.

### 📦 Formato de Entrega

- Comprimir toda a pasta `battleship/` em um único arquivo `.zip`.
- Nomear o arquivo de acordo com o padrão: `battleship_<nome_do_grupo>.zip`.

### 📬 Como Enviar

- Enviar o `.zip` via a plataforma designada pelo professor (ex: Google Classroom ou SIGAA).
- Apenas um integrante do grupo deve realizar a submissão.

### ⏰ Prazo

- O prazo final para submissão é de **10 dias corridos** a partir da data de liberação do projeto.
- Submissões após o prazo estarão sujeitas à penalidade conforme as regras da disciplina.

---

