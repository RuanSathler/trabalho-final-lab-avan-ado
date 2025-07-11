#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <ctype.h> // Para toupper

#include "../common/protocol.h" 

void imprimir_tabuleiro(char tab[BOARD_SIZE][BOARD_SIZE]) {
    printf("  1 2 3 4 5 6 7 8\n");
    for (int i = 0; i < BOARD_SIZE; i++) {
        printf("%c ", 'A' + i);
        for (int j = 0; j < BOARD_SIZE; j++) {
            printf("%c ", tab[i][j]);
        }
        printf("\n");
    }
}

int main(int argc, char const *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <IP do Servidor>\n", argv[0]);
        return 1;
    }
    const char *server_ip = argv[1];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    char buffer[MAX_MSG] = {0};
    char nome[50];
    char meu_tab[BOARD_SIZE][BOARD_SIZE];     // Mostra ao usuário o que ele posicionou
    char tab_adversario[BOARD_SIZE][BOARD_SIZE]; // Marca acertos e erros no tabuleiro do oponente

    // Inicializa os tabuleiros com espaços vazios
    memset(meu_tab, ' ', sizeof(meu_tab));
    memset(tab_adversario, ' ', sizeof(tab_adversario));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Converte o endereço IP de string para formato binário
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("\nEndereco de IP invalido ou nao suportado \n");
        return 1;
    }

    // Tenta conectar ao servidor
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro ao conectar");
        return 1;
    }

    // Recebe a primeira mensagem do servidor (e.g., "Aguardando outro jogador..." ou "Conectado. Preparando...")
    ssize_t n = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (n <= 0) {
        printf("Conexão encerrada pelo servidor ou erro na recepção inicial.\n");
        close(sock);
        return 1;
    }
    buffer[n] = '\0';
    buffer[strcspn(buffer, "\n")] = 0; // Remove a nova linha
    printf("Servidor: %s\n", buffer);

    // Se o jogo está cheio, encerra o cliente imediatamente
    if (strstr(buffer, "Jogo cheio")) {
        close(sock);
        return 0;
    }

    printf("Digite seu nome: ");
    fgets(nome, sizeof(nome), stdin);
    nome[strcspn(nome, "\n")] = 0; // Remove a nova linha do nome

    // Envia o comando JOIN com o nome do jogador
    char join_msg[MAX_MSG];
    snprintf(join_msg, sizeof(join_msg), "%s %s", CMD_JOIN, nome);
    send(sock, join_msg, strlen(join_msg), 0);
    // Não espera resposta para JOIN, assume que foi bem-sucedido (o servidor já aceitou a conexão)

    // --- Fase de Posicionamento ---
    printf("\n--- FASE DE POSICIONAMENTO ---\n");
    printf("Posicione seus navios no tabuleiro %dx%d\n", BOARD_SIZE, BOARD_SIZE);
    printf("Voce deve posicionar: 1 SUBMARINO (S), 2 FRAGATAS (F), 1 DESTROYER (D)\n");
    printf("Use formato: %s <TIPO/LETRA> <Coordenada> <O>\n", CMD_POS);
    printf("Coordenada e no formato LetraNumero (ex: A1, H8). O e orientacao H (Horizontal) ou V (Vertical)\n");
    printf("Exemplo: %s F A1 H (para uma Fragata) ou %s SUBMARINO C4 V\n", CMD_POS, CMD_POS);

    int pos_submarino = 0; // Contadores locais para o cliente
    int pos_fragata = 0;
    int pos_destroyer = 0;
    int pronto_para_jogar = 0;

    while (!pronto_para_jogar) {
        printf("\nNavios restantes para posicionar: Submarino(%d/1), Fragata(%d/2), Destroyer(%d/1)\n",
               pos_submarino, pos_fragata, pos_destroyer);
        printf("Seu tabuleiro:\n");
        imprimir_tabuleiro(meu_tab);
        printf("Digite comando %s ou %s para terminar posicionamento:\n", CMD_POS, CMD_READY);
        
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0; // Remove a nova linha

        // Verifica se o comando é READY
        if (strncmp(buffer, CMD_READY, strlen(CMD_READY)) == 0) {
            // Verifica se todos os navios foram posicionados localmente antes de enviar READY
            if (pos_submarino == 1 && pos_fragata == 2 && pos_destroyer == 1) {
                send(sock, buffer, strlen(buffer), 0); // Envia o comando READY

                printf("Servidor: READY recebido. Aguardando adversario...\n"); // Feedback imediato ao usuário

                // Loop para esperar mensagens do servidor após READY
                while (1) {
                    n = recv(sock, buffer, sizeof(buffer)-1, 0); // Espera bloqueante
                    if (n <= 0) {
                        printf("Servidor desconectado durante a espera pelo inicio do jogo ou erro.\n");
                        close(sock);
                        return 1;
                    }
                    buffer[n] = '\0';
                    buffer[strcspn(buffer, "\n")] = 0;
                    printf("Servidor: %s\n", buffer); // Exibe a mensagem do servidor

                    if (strstr(buffer, "INICIO DO JOGO")) {
                        pronto_para_jogar = 1; // Seta a flag para sair do loop externo de posicionamento
                        break; // Sai IMEDIATAMENTE do loop interno para processar a próxima mensagem (PLAY/AGUARDE) no loop principal
                    }
                     // Lidar com a desconexão do adversário se ocorrer durante a espera
                    if (strstr(buffer, "O adversario desconectou")) {
                        printf("O adversario desconectou. Jogo encerrado.\n");
                        close(sock);
                        return 0;
                    }
                }
            } else {
                printf("Voce ainda nao posicionou todos os 4 navios (1 Submarino, 2 Fragatas, 1 Destroyer).\n");
            }
        }
        // Verifica se o comando é POS (posicionar navio)
        else if (strncmp(buffer, CMD_POS, strlen(CMD_POS)) == 0) {
            char tipo_str[20];
            char row_char_in;
            int col_num_in;
            char orientation_char;
            
            // Tentativa de parsear o comando POS no formato "POS TIPO A1 O"
            if (sscanf(buffer, CMD_POS " %s %c%d %c", tipo_str, &row_char_in, &col_num_in, &orientation_char) == 4) {
                row_char_in = toupper(row_char_in);
                orientation_char = toupper(orientation_char);

                // Validações locais (básicas)
                if (row_char_in < 'A' || row_char_in > ('A' + BOARD_SIZE - 1) || col_num_in < 1 || col_num_in > BOARD_SIZE) {
                    printf("Coordenada invalida. Use Letra (A-%c) e Numero (1-%d).\n", 'A' + BOARD_SIZE - 1, BOARD_SIZE);
                    continue;
                }
                if (orientation_char != 'H' && orientation_char != 'V') {
                    printf("Orientacao invalida. Use 'H' para Horizontal ou 'V' para Vertical.\n");
                    continue;
                }

                // Traduz para o formato do servidor (0-indexed)
                int x_coord_0_indexed = row_char_in - 'A';
                int y_coord_0_indexed = col_num_in - 1;

                // Monta o comando para o servidor
                char server_cmd[MAX_MSG];
                snprintf(server_cmd, sizeof(server_cmd), "%s %s %d %d %c", CMD_POS, tipo_str, x_coord_0_indexed, y_coord_0_indexed, orientation_char);
                
                // Envia para o servidor para validação completa e posicionamento
                send(sock, server_cmd, strlen(server_cmd), 0);

                // Espera a resposta do servidor
                n = recv(sock, buffer, sizeof(buffer)-1, 0);
                if (n <= 0) {
                    printf("Servidor desconectado durante o posicionamento.\n");
                    close(sock);
                    return 1;
                }
                buffer[n] = '\0';
                buffer[strcspn(buffer, "\n")] = 0;
                printf("Servidor: %s\n", buffer);

                // Se o servidor aprovou, atualiza o tabuleiro e contadores locais do cliente
                if (strcmp(buffer, "Navio posicionado com sucesso.") == 0) {
                    char simb = ' ';
                    int ship_len = 0;

                    if (strcmp(tipo_str, "SUBMARINO") == 0 || strcmp(tipo_str, "S") == 0) {
                        simb = 'S'; ship_len = 1; pos_submarino++;
                    } else if (strcmp(tipo_str, "FRAGATA") == 0 || strcmp(tipo_str, "F") == 0) {
                        simb = 'F'; ship_len = 2; pos_fragata++;
                    } else if (strcmp(tipo_str, "DESTROYER") == 0 || strcmp(tipo_str, "D") == 0) {
                        simb = 'D'; ship_len = 3; pos_destroyer++;
                    }

                    // Marca o navio no tabuleiro do cliente usando as coordenadas 0-indexed
                    if (orientation_char == 'H') {
                        for (int i = 0; i < ship_len; i++)
                            if (y_coord_0_indexed + i < BOARD_SIZE) meu_tab[x_coord_0_indexed][y_coord_0_indexed + i] = simb;
                    } else { // 'V'
                        for (int i = 0; i < ship_len; i++)
                            if (x_coord_0_indexed + i < BOARD_SIZE) meu_tab[x_coord_0_indexed + i][y_coord_0_indexed] = simb;
                    }
                }
            } else {
                printf("Comando POS invalido. Formato esperado: POS <TIPO/LETRA> <Coordenada> <O> (ex: POS F A1 H)\n");
            }
        }
        // Se o comando não é POS nem READY
        else {
            printf("Comando desconhecido ou invalido na fase de posicionamento. Use POS ou READY.\n");
        }
    } // Fim do while (!pronto_para_jogar)

    // --- Início da Fase de Jogo Principal ---
    printf("\n--- INICIO DO JOGO ---\n");
    printf("Seu tabuleiro:\n");
    imprimir_tabuleiro(meu_tab);
    printf("\nTabuleiro do Adversario (seus tiros):\n");
    imprimir_tabuleiro(tab_adversario);

    int last_fire_x = -1, last_fire_y = -1; // Armazena o último tiro para atualizar o tabuleiro

    // O bloco de código anterior foi removido. A lógica de processamento de turno
    // agora é tratada exclusivamente pelo loop principal abaixo, que processará
    // o buffer preenchido na fase de posicionamento.

    // Loop principal da fase de jogo: processa o buffer atual, depois espera pelo próximo.
    while (1) {
        // Loop para processar todos os comandos recebidos no buffer
        char *command = strtok(buffer, "\n");
        while (command != NULL) {
            // A ordem aqui é crucial. Primeiro checa os comandos que podem conter outros como substring.
            if (strstr(command, "OPPONENT_FIRE")) {
                int opp_x, opp_y;
                char result_str[10];
                if (sscanf(command, "OPPONENT_FIRE %d %d %s", &opp_x, &opp_y, result_str) == 3) {
                    if (strcmp(result_str, CMD_SUNK) == 0 || strcmp(result_str, CMD_HIT) == 0) {
                        meu_tab[opp_x][opp_y] = 'X';
                        printf("Seu navio em %c%d foi atingido!\n", 'A' + opp_x, opp_y + 1);
                    } else {
                        meu_tab[opp_x][opp_y] = 'O';
                        printf("O adversario atirou em %c%d e errou!\n", 'A' + opp_x, opp_y + 1);
                    }
                }
            }
            else if (strstr(command, CMD_WIN)) {
                printf("\n--- FIM DE JOGO: VOCE VENCEU! ---\n");
                imprimir_tabuleiro(meu_tab);
                printf("\nTabuleiro adversario final:\n");
                imprimir_tabuleiro(tab_adversario);
                goto end_game; // Usar goto para sair de loops aninhados de forma limpa
            }
            else if (strstr(command, CMD_LOSE)) {
                printf("\n--- FIM DE JOGO: VOCE PERDEU! ---\n");
                imprimir_tabuleiro(meu_tab);
                printf("\nTabuleiro adversario final:\n");
                imprimir_tabuleiro(tab_adversario);
                goto end_game;
            }
            else if (strstr(command, CMD_END)) {
                printf("Jogo encerrado pelo servidor.\n");
                goto end_game;
            }
            else if (strstr(command, CMD_PLAY)) {
                printf("\n--- SEU TURNO! --- \n");
                printf("Seu tabuleiro:\n");
                imprimir_tabuleiro(meu_tab);
                printf("\nTabuleiro do Adversario (seus tiros):\n");
                imprimir_tabuleiro(tab_adversario);
                printf("Digite %s <Coordenada> (ex: A1):\n", CMD_FIRE);

                char fire_cmd_input[MAX_MSG];
                while(1) {
                    fgets(fire_cmd_input, sizeof(fire_cmd_input), stdin);
                    fire_cmd_input[strcspn(fire_cmd_input, "\n")] = 0;

                    char row_char_in;
                    int col_num_in;
                    if (sscanf(fire_cmd_input, CMD_FIRE " %c%d", &row_char_in, &col_num_in) == 2) {
                        row_char_in = toupper(row_char_in);

                        if (row_char_in < 'A' || row_char_in > ('A' + BOARD_SIZE - 1) || col_num_in < 1 || col_num_in > BOARD_SIZE) {
                            printf("Coordenada invalida. Use Letra (A-%c) e Numero (1-%d).\n", 'A' + BOARD_SIZE - 1, BOARD_SIZE);
                            continue;
                        }
                        
                        // Traduz para o formato do servidor (0-indexed)
                        int x_coord_0_indexed = row_char_in - 'A';
                        int y_coord_0_indexed = col_num_in - 1;

                        // Guarda as coordenadas do tiro para atualizar o tabuleiro do adversário depois
                        last_fire_x = x_coord_0_indexed;
                        last_fire_y = y_coord_0_indexed;

                        // Monta o comando para o servidor
                        char server_cmd[MAX_MSG];
                        snprintf(server_cmd, sizeof(server_cmd), "%s %d %d", CMD_FIRE, x_coord_0_indexed, y_coord_0_indexed);
                        
                        send(sock, server_cmd, strlen(server_cmd), 0);
                        break; // Sai do loop de leitura de FIRE
                    } else {
                        printf("Comando FIRE invalido. Formato: %s <Coordenada> (ex: A1). Tente novamente.\n", CMD_FIRE);
                    }
                }
            }
            else if (strstr(command, CMD_SUNK)) {
                printf("Voce AFUNDOU um navio!\n");
                if (last_fire_x != -1) tab_adversario[last_fire_x][last_fire_y] = 'X';
            }
            else if (strstr(command, CMD_HIT)) {
                printf("Voce ACERTOU um navio!\n");
                if (last_fire_x != -1) tab_adversario[last_fire_x][last_fire_y] = 'X';
            }
            else if (strstr(command, CMD_MISS)) {
                printf("Voce ERROU!\n");
                if (last_fire_x != -1) tab_adversario[last_fire_x][last_fire_y] = 'O';
            }
            else if (strstr(command, "AGUARDE")) {
                printf("Aguarde a vez do adversario...\n");
            }
            command = strtok(NULL, "\n"); // Pega o próximo comando no buffer
        }

        // AGORA, espera pela próxima mensagem do servidor para a próxima iteração do loop
        n = recv(sock, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) {
            printf("Servidor desconectado. Fim de jogo.\n");
            break;
        }
        buffer[n] = '\0';
    } // Fim do while (1) da fase de jogo

end_game:; // Rótulo para o goto

    close(sock); // Fecha o socket ao final do jogo
    printf("Conexao com o servidor encerrada.\n");
    return 0;
}