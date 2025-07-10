#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_MSG 256
#define PORT 8080
#define BOARD_SIZE 8 // Tamanho do tabuleiro (8x8)
#define MAX_SHIPS 4  // Total de navios por jogador (1S + 2F + 1D = 4)

// Comandos do cliente para o servidor
#define CMD_JOIN "JOIN"
#define CMD_POS "POS"
#define CMD_READY "READY"
#define CMD_FIRE "FIRE"

// Comandos/mensagens do servidor para o cliente
#define CMD_PLAY "PLAY" // Servidor envia para o jogador que deve jogar
#define CMD_HIT "HIT"   // Acertou um navio
#define CMD_MISS "MISS" // Errou o tiro
#define CMD_SUNK "SUNK" // Afundou um navio
#define CMD_WIN "WIN"   // Vitoria (o jogador venceu)
#define CMD_LOSE "LOSE" // Derrota (o jogador perdeu)
#define CMD_END "END"   // Fim de jogo geral (servidor encerra)

#endif // PROTOCOL_H