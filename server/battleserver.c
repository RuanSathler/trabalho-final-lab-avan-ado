#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "../common/protocol.h"

#define MAX_PLAYERS 2

// Estrutura para representar um jogador
typedef struct {
    int id;
    int socket;
    char name[50];
    char board[BOARD_SIZE][BOARD_SIZE]; // Tabuleiro do jogador
    int ships_sunk; // Quantidade de navios afundados do adversário para este jogador
    int ready; // 0 = nao pronto, 1 = pronto
    pthread_mutex_t lock; // Mutex para proteger o acesso aos dados do jogador
    int pos_submarino; // Contadores de navios posicionados
    int pos_fragata;
    int pos_destroyer;
    // Array para registrar os navios para facilitar a verificação de afundamento
    // Formato: {x_origem, y_origem, orientacao_char_as_int, comprimento, hits_recebidos, tipo_char_as_int}
    int ships[MAX_SHIPS][6];
    int num_ships_placed; // Quantidade de navios efetivamente posicionados (para o array ships)
} Player;

// Tipos de navio e seus comprimentos
typedef struct {
    char symbol;
    int length;
    const char* name;
    int max_count; // Quantidade máxima desse tipo de navio por jogador
} ShipType;

ShipType ship_types[] = {
    {'S', 1, "SUBMARINO", 1},
    {'F', 2, "FRAGATA", 2},
    {'D', 3, "DESTROYER", 1}
};
#define NUM_SHIP_TYPES (sizeof(ship_types) / sizeof(ShipType))

// Variáveis globais do servidor
Player players[MAX_PLAYERS];
int num_connected_players = 0;
// =================== INÍCIO: REGIÃO DE PARALELISMO ===================
// Declaração do mutex global para proteger o array de jogadores e variáveis globais
pthread_mutex_t players_mutex = PTHREAD_MUTEX_INITIALIZER;
// Condição para sincronizar quando ambos os jogadores estão prontos
pthread_cond_t all_players_ready_cond = PTHREAD_COND_INITIALIZER;
// Condição para sincronizar a vez de cada jogador
pthread_cond_t turn_cond = PTHREAD_COND_INITIALIZER; // Para sincronizar os turnos
// =================== FIM: REGIÃO DE PARALELISMO ===================
int current_player_turn = -1; // -1 = nenhum, 0 = player 0, 1 = player 1
int game_started = 0; // Flag para indicar se o jogo começou
int game_over = 0; // Flag para indicar se o jogo terminou

// --- Funções Auxiliares de Validação ---

// Encontra o ShipType dado o símbolo ou nome
ShipType* get_ship_type_info(const char* identifier) {
    for (int i = 0; i < NUM_SHIP_TYPES; i++) {
        if (strcmp(identifier, ship_types[i].name) == 0 ||
            (strlen(identifier) == 1 && identifier[0] == ship_types[i].symbol)) {
            return &ship_types[i];
        }
    }
    return NULL;
}

// Envia uma mensagem para o socket do jogador, adicionando uma nova linha
void send_to_player(int player_socket, const char* message) {
    char full_message[MAX_MSG];
    // Garante que a mensagem termine com \n e seja nula terminada
    snprintf(full_message, sizeof(full_message), "%s\n", message);
    send(player_socket, full_message, strlen(full_message), 0);
}

// Inicializa o tabuleiro e os contadores de navios de um jogador
void init_player_state(Player *player) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            player->board[i][j] = ' ';
        }
    }
    player->pos_submarino = 0;
    player->pos_fragata = 0;
    player->pos_destroyer = 0;
    player->num_ships_placed = 0; // Resetar contagem de navios no array
    player->ships_sunk = 0; // Resetar navios afundados
    player->ready = 0; // Garantir que o jogador não esteja pronto por padrão
}

// Verifica se o navio está dentro dos limites do tabuleiro
int is_valid_position(Player *player, int x, int y, char orientation, int ship_len) {
    if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
        printf("DEBUG: is_valid_position: Coordenadas iniciais (%d,%d) fora dos limites (0-%d).\n", x, y, BOARD_SIZE - 1);
        return 0; // Fora dos limites iniciais
    }

    if (orientation == 'H' || orientation == 'h') {
        if (y + ship_len > BOARD_SIZE) {
            printf("DEBUG: is_valid_position: Navio horizontal fora dos limites (y=%d + len=%d > BOARD_SIZE=%d).\n", y, ship_len, BOARD_SIZE);
            return 0;
        }
    } else if (orientation == 'V' || orientation == 'v') {
        if (x + ship_len > BOARD_SIZE) {
            printf("DEBUG: is_valid_position: Navio vertical fora dos limites (x=%d + len=%d > BOARD_SIZE=%d).\n", x, ship_len, BOARD_SIZE);
            return 0;
        }
    } else {
        printf("DEBUG: is_valid_position: Orientacao invalida '%c'.\n", orientation);
        return 0; // Orientação inválida
    }
    return 1;
}

// Verifica se o navio se sobrepõe a outro navio já posicionado
int is_overlapping(Player *player, int x, int y, char orientation, int ship_len) {
    for (int i = 0; i < ship_len; i++) {
        int current_x = x;
        int current_y = y;

        if (orientation == 'H' || orientation == 'h') {
            current_y = y + i;
        } else { // 'V' ou 'v'
            current_x = x + i;
        }

        // Essa verificação de limite aqui é mais para segurança contra erros lógicos em loops,
        // já que is_valid_position() deveria garantir que não acessaremos fora.
        if (current_x < 0 || current_x >= BOARD_SIZE || current_y < 0 || current_y >= BOARD_SIZE) {
             printf("DEBUG: is_overlapping: Erro interno de limite de loop - (%d,%d) fora do tabuleiro. Isso nao deveria acontecer se is_valid_position foi chamada primeiro.\n", current_x, current_y);
             return 1; // Considerar como sobreposição se o cálculo for ruim
        }

        if (player->board[current_x][current_y] != ' ') {
            printf("DEBUG: is_overlapping: Sobreposicao detectada em (%d,%d) com navio '%c'.\n", current_x, current_y, player->board[current_x][current_y]);
            return 1; // Sobreposição
        }
    }
    return 0; // Nenhuma sobreposição
}

// Lida com o comando POS (posicionamento de navios)
void handle_pos_command(Player *player, char* command) {
    char tipo_navio_str[20];
    int x, y;
    char o; // Orientacao 'H' ou 'V'

    // Formato: POS <TIPO> <X> <Y> <O>
    if (sscanf(command, CMD_POS " %s %d %d %c", tipo_navio_str, &x, &y, &o) != 4) {
        send_to_player(player->socket, "Comando POS invalido. Formato: POS <TIPO/LETRA> <X> <Y> <O>");
        printf("DEBUG: Jogador %s enviou formato invalido POS: '%s'\n", player->name, command);
        return;
    }

    ShipType* ship_info = get_ship_type_info(tipo_navio_str);
    if (!ship_info) {
        send_to_player(player->socket, "Tipo de navio invalido.");
        printf("DEBUG: Jogador %s enviou tipo de navio '%s' nao encontrado.\n", player->name, tipo_navio_str);
        return;
    }

    pthread_mutex_lock(&player->lock); // Proteger o estado do jogador durante o posicionamento

    // Verifica a contagem de navios para o tipo ANTES de qualquer outra validação
    int *count_ptr = NULL;
    if (ship_info->symbol == 'S') count_ptr = &player->pos_submarino;
    else if (ship_info->symbol == 'F') count_ptr = &player->pos_fragata;
    else if (ship_info->symbol == 'D') count_ptr = &player->pos_destroyer;

    if (count_ptr == NULL || *count_ptr >= ship_info->max_count) {
        char msg[MAX_MSG];
        snprintf(msg, sizeof(msg), "Limite de navios do tipo %s atingido (%d/%d).", ship_info->name, *count_ptr, ship_info->max_count);
        send_to_player(player->socket, msg);
        printf("DEBUG: Jogador %s: Limite de %s atingido ou tipo nao mapeado para contagem: %d/%d\n", player->name, ship_info->name, *count_ptr, ship_info->max_count);
        pthread_mutex_unlock(&player->lock);
        return;
    }

    // Validações de posicionamento no tabuleiro
    if (!is_valid_position(player, x, y, o, ship_info->length)) {
        send_to_player(player->socket, "Posicionamento invalido: Fora dos limites do tabuleiro.");
        pthread_mutex_unlock(&player->lock);
        return;
    }
    if (is_overlapping(player, x, y, o, ship_info->length)) {
        send_to_player(player->socket, "Posicionamento invalido: Sobreposicao com outro navio.");
        pthread_mutex_unlock(&player->lock);
        return;
    }
    
    // Verifica se ainda há espaço no array de ships
    if (player->num_ships_placed >= MAX_SHIPS) {
        send_to_player(player->socket, "Erro interno: Capacidade maxima de navios no array atingida.");
        printf("DEBUG: Jogador %s: Tentou posicionar mais de MAX_SHIPS navios no array.\n", player->name);
        pthread_mutex_unlock(&player->lock);
        return;
    }

    // Se tudo ok, posiciona o navio no tabuleiro
    for (int i = 0; i < ship_info->length; i++) {
        if (o == 'H' || o == 'h') {
            player->board[x][y + i] = ship_info->symbol;
        } else { // 'V' ou 'v'
            player->board[x + i][y] = ship_info->symbol;
        }
    }

    // Atualiza os contadores de navios por tipo
    (*count_ptr)++;
    
    // Armazena as informações do navio no array para checar afundamento depois
    // Indices: {0:x, 1:y, 2:orientation_as_int, 3:length, 4:hits_received, 5:symbol_as_int}
    player->ships[player->num_ships_placed][0] = x;
    player->ships[player->num_ships_placed][1] = y;
    player->ships[player->num_ships_placed][2] = (int)o; // Armazena char como int
    player->ships[player->num_ships_placed][3] = ship_info->length;
    player->ships[player->num_ships_placed][4] = 0; // Hits recebidos (inicialmente 0)
    player->ships[player->num_ships_placed][5] = (int)ship_info->symbol; // Armazena char como int

    player->num_ships_placed++; // Incrementa o contador de navios posicionados no array

    send_to_player(player->socket, "Navio posicionado com sucesso.");
    pthread_mutex_unlock(&player->lock);
    printf("DEBUG: Jogador %s posicionou %s em (%d,%d) %c. Contagem: S:%d, F:%d, D:%d. Total navios registrados no array: %d\n",
           player->name, ship_info->name, x, y, o, player->pos_submarino, player->pos_fragata, player->pos_destroyer, player->num_ships_placed);
}

// Lida com o comando READY
void handle_ready_command(Player *player) {
    // Verifica se todos os navios foram posicionados: 1 SUBMARINO, 2 FRAGATAS, 1 DESTROYER
    if (player->pos_submarino == 1 && player->pos_fragata == 2 && player->pos_destroyer == 1) {
        pthread_mutex_lock(&players_mutex);
        player->ready = 1;
        send_to_player(player->socket, "READY recebido. Aguardando adversario...");
        printf("DEBUG: Jogador %s esta pronto.\n", player->name);

        // Verifica se ambos os jogadores estão prontos para iniciar o jogo
        if (players[0].ready && players[1].ready && !game_started) {
            game_started = 1;
            // Define o jogador 0 como o primeiro a jogar (pode ser randomizado no futuro)
            current_player_turn = 0;
            printf("DEBUG: Ambos os jogadores estao prontos. Jogo iniciando! Turno do jogador %s.\n", players[current_player_turn].name);
            pthread_cond_broadcast(&all_players_ready_cond); // Notifica as threads para iniciar o jogo
        }
        pthread_mutex_unlock(&players_mutex);
    } else {
        char msg[MAX_MSG];
        snprintf(msg, sizeof(msg), "Erro: Voce ainda nao posicionou todos os navios (1 Submarino, 2 Fragatas, 1 Destroyer).");
        send_to_player(player->socket, msg);
        printf("DEBUG: Jogador %s tentou READY mas nao posicionou todos os navios: S:%d, F:%d, D:%d\n",
               player->name, player->pos_submarino, player->pos_fragata, player->pos_destroyer);
    }
}

// Lida com o comando FIRE (ataque)
void handle_fire_command(Player *attacker, char* command) {
    if (!game_started || game_over) {
        send_to_player(attacker->socket, "O jogo nao comecou ou ja terminou.");
        return;
    }

    int target_player_id = (attacker->id == 0) ? 1 : 0;
    Player *defender = &players[target_player_id];

    if (attacker->id != current_player_turn) {
        send_to_player(attacker->socket, "Nao e sua vez de jogar.");
        return;
    }

    int x, y;
    if (sscanf(command, CMD_FIRE " %d %d", &x, &y) != 2) {
        send_to_player(attacker->socket, "Comando FIRE invalido. Formato: FIRE <X> <Y>");
        return;
    }

    if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
        send_to_player(attacker->socket, "Coordenadas de tiro invalidas (0-7).");
        return;
    }
    
    // =================== INÍCIO: REGIÃO CRÍTICA INDIVIDUAL (defensor) ===================
    pthread_mutex_lock(&defender->lock); // Proteger o tabuleiro do defensor

    // Evita atirar na mesma posição já atingida (X) ou errada (O)
    if (defender->board[x][y] == 'X' || defender->board[x][y] == 'O') {
        send_to_player(attacker->socket, "Voce ja atirou nesta posicao. Tente outra.");
        pthread_mutex_unlock(&defender->lock);
        return;
    }

    char target_cell = defender->board[x][y];
    char msg_to_attacker[MAX_MSG];
    char msg_to_defender[MAX_MSG];

    if (target_cell != ' ') { // Acertou um navio (S, F ou D)
        defender->board[x][y] = 'X'; // Marca como atingido no tabuleiro do defensor
        snprintf(msg_to_attacker, sizeof(msg_to_attacker), "%s", CMD_HIT);

        // Encontrar o navio atingido e incrementar hits
        int ship_hit_index = -1;
        for(int i = 0; i < defender->num_ships_placed; i++) {
            // Recupere as informações do navio do array
            int ship_x = defender->ships[i][0];
            int ship_y = defender->ships[i][1];
            char ship_o = (char)defender->ships[i][2]; // Converte de int para char
            int ship_len = defender->ships[i][3];

            if (ship_o == 'H' || ship_o == 'h') {
                if (x == ship_x && y >= ship_y && y < ship_y + ship_len) {
                    ship_hit_index = i;
                    break;
                }
            } else { // Vertical
                if (y == ship_y && x >= ship_x && x < ship_x + ship_len) {
                    ship_hit_index = i;
                    break;
                }
            }
        }

        if (ship_hit_index != -1) {
            defender->ships[ship_hit_index][4]++; // Incrementa hits (índice 4 é 'hits_recebidos')
            printf("DEBUG: Navio '%c' de %s em (%d,%d) recebeu %d/%d hits.\n",
                   (char)defender->ships[ship_hit_index][5], defender->name, defender->ships[ship_hit_index][0], defender->ships[ship_hit_index][1], // Usamos o symbol_char aqui
                   defender->ships[ship_hit_index][4], defender->ships[ship_hit_index][3]); // Hits e Length

            if (defender->ships[ship_hit_index][4] == defender->ships[ship_hit_index][3]) {
                // Navio afundado
                snprintf(msg_to_attacker, sizeof(msg_to_attacker), "%s", CMD_SUNK);
                attacker->ships_sunk++; // Atacante afundou um navio

                printf("DEBUG: Jogador %s afundou um navio do jogador %s. Total afundados por %s: %d.\n",
                       attacker->name, defender->name, attacker->name, attacker->ships_sunk);

                if (attacker->ships_sunk == MAX_SHIPS) { // Todos os 4 navios do adversário afundados
                    game_over = 1; // Fim de jogo
                    send_to_player(attacker->socket, CMD_WIN);
                    send_to_player(defender->socket, CMD_LOSE);
                    printf("DEBUG: Jogo terminou. Jogador %s venceu.\n", attacker->name);
                }
            }
        }
        // Mensagem para o defensor deve refletir se o navio foi afundado ou apenas atingido
        snprintf(msg_to_defender, sizeof(msg_to_defender), "OPPONENT_FIRE %d %d %s", x, y, (strcmp(msg_to_attacker, CMD_SUNK) == 0) ? CMD_SUNK : CMD_HIT);

    } else { // Errou o tiro
        defender->board[x][y] = 'O'; // Marca como erro no tabuleiro do defensor
        snprintf(msg_to_attacker, sizeof(msg_to_attacker), "%s", CMD_MISS);
        snprintf(msg_to_defender, sizeof(msg_to_defender), "OPPONENT_FIRE %d %d %s", x, y, CMD_MISS);
    }

    send_to_player(attacker->socket, msg_to_attacker); // Resposta ao atacante
    send_to_player(defender->socket, msg_to_defender); // Notificação ao defensor

    pthread_mutex_unlock(&defender->lock); // Liberar o lock do tabuleiro do defensor
    // =================== FIM: REGIÃO CRÍTICA INDIVIDUAL ===================

    // Troca o turno, se o jogo não terminou
    pthread_mutex_lock(&players_mutex); // =================== INÍCIO: REGIÃO CRÍTICA GLOBAL ===================
    if (!game_over) {
        current_player_turn = target_player_id;
        send_to_player(attacker->socket, "AGUARDE");
        send_to_player(defender->socket, CMD_PLAY);
        printf("DEBUG: Turno trocado para Jogador %s.\n", players[current_player_turn].name);
        // =================== INÍCIO: SINCRONIZAÇÃO ENTRE THREADS (troca de turno) ===================
        pthread_cond_broadcast(&turn_cond); // Notifica as threads que o turno mudou
        // =================== FIM: SINCRONIZAÇÃO ENTRE THREADS ===================
    }
    pthread_mutex_unlock(&players_mutex); // =================== FIM: REGIÃO CRÍTICA GLOBAL ===================
}

// Thread para lidar com a comunicação de cada cliente
void *handle_client(void *arg) {
    Player *player = (Player *)arg;
    char buffer[MAX_MSG];
    ssize_t n;

    printf("DEBUG: Thread do cliente %s (ID: %d) iniciada.\n", player->name, player->id);

    // Envia a mensagem inicial ANTES de esperar pelo JOIN para evitar deadlock.
    pthread_mutex_lock(&players_mutex);
    if (num_connected_players == 1) { // Este é o primeiro jogador a se conectar (0-indexed)
         send_to_player(player->socket, "Aguardando outro jogador...");
    } else if (num_connected_players == 2) { // Este é o segundo jogador
        send_to_player(player->socket, "Conectado. Preparando para o jogo.");
    }
    pthread_mutex_unlock(&players_mutex);

    // Agora, espera pelo comando JOIN do cliente
    n = recv(player->socket, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        printf("DEBUG: Cliente %s desconectou antes de enviar JOIN.\n", player->name);
        close(player->socket);
        pthread_exit(NULL);
    }
    buffer[n] = '\0';
    buffer[strcspn(buffer, "\n")] = 0;

    if (strncmp(buffer, CMD_JOIN, strlen(CMD_JOIN)) == 0) {
        sscanf(buffer, CMD_JOIN " %s", player->name);
        printf("DEBUG: Jogador %s (ID: %d) se juntou ao jogo.\n", player->name, player->id);
    } else {
        send_to_player(player->socket, "Comando invalido. Use JOIN <seu_nome>.");
        close(player->socket);
        pthread_exit(NULL);
    }

    // Fase de posicionamento
    while (!player->ready && !game_over) { // Adicionado !game_over para sair em caso de desconexão do outro
        n = recv(player->socket, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) {
            printf("DEBUG: Cliente %s desconectou durante o posicionamento.\n", player->name);
            game_over = 1; // Sinaliza o fim do jogo globalmente
            // Tentar notificar o outro jogador sobre a desconexão
            int other_player_id = (player->id == 0) ? 1 : 0;
            if (players[other_player_id].socket != 0) { // Se o outro jogador ainda está conectado
                 send_to_player(players[other_player_id].socket, "O adversario desconectou durante o posicionamento. Jogo encerrado.");
            }
            pthread_mutex_lock(&players_mutex); // Lock para manipular flags de pronto
            players[0].ready = 0; // Garante que o outro jogador nao espere infinitamente
            players[1].ready = 0;
            pthread_mutex_unlock(&players_mutex);
            pthread_cond_broadcast(&all_players_ready_cond); // Notifica as threads para sair do wait
            close(player->socket);
            pthread_exit(NULL);
        }
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\n")] = 0; // Remove a nova linha

        if (strncmp(buffer, CMD_POS, strlen(CMD_POS)) == 0) {
            handle_pos_command(player, buffer);
        } else if (strncmp(buffer, CMD_READY, strlen(CMD_READY)) == 0) {
            handle_ready_command(player);
        } else {
            send_to_player(player->socket, "Comando invalido na fase de posicionamento. Use POS <TIPO> <X> <Y> <O> ou READY.");
            printf("DEBUG: Jogador %s enviou comando invalido na fase de pos: '%s'\n", player->name, buffer);
        }
    }

    // Se o jogo acabou por desconexão durante o posicionamento, esta thread termina
    if (game_over) {
        close(player->socket);
        pthread_exit(NULL);
    }

    // Esperar que o outro jogador também esteja pronto
    pthread_mutex_lock(&players_mutex);
    while (!game_started) {
         // =================== INÍCIO: SINCRONIZAÇÃO ENTRE THREADS (ambos prontos) ===================
        pthread_cond_wait(&all_players_ready_cond, &players_mutex);
        // =================== FIM: SINCRONIZAÇÃO ENTRE THREADS ===================
        // Verifica novamente se o jogo terminou enquanto esperava (ex: outro jogador desconectou)
        if (game_over) {
            pthread_mutex_unlock(&players_mutex);
            close(player->socket);
            pthread_exit(NULL);
        }
    }
    pthread_mutex_unlock(&players_mutex);

    // Envia a mensagem de inicio de jogo e quem começa
    // Isso é feito apenas uma vez por jogador
    // Combina as mensagens para evitar problemas de recepção no cliente
    if (player->id == current_player_turn) {
        send_to_player(player->socket, "INICIO DO JOGO. E sua vez! PLAY");
    } else {
        send_to_player(player->socket, "INICIO DO JOGO. Aguarde a vez do adversario. AGUARDE");
    }

    // --- Fase de Jogo Principal ---
    while (!game_over) {
        pthread_mutex_lock(&players_mutex); // =================== INÍCIO: REGIÃO CRÍTICA GLOBAL ===================
        // =================== INÍCIO: SINCRONIZAÇÃO ENTRE THREADS (turno) ===================
        while (current_player_turn != player->id && !game_over) {
            pthread_cond_wait(&turn_cond, &players_mutex);
        }
        // =================== FIM: SINCRONIZAÇÃO ENTRE THREADS ===================
        if (game_over) {
            pthread_mutex_unlock(&players_mutex); // =================== FIM: REGIÃO CRÍTICA GLOBAL ===================
            break;
        }
        pthread_mutex_unlock(&players_mutex); // =================== FIM: REGIÃO CRÍTICA GLOBAL ===================

        // Agora é a vez deste jogador, então ele espera por um comando
        n = recv(player->socket, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) {
            printf("DEBUG: Cliente %s desconectou durante o jogo.\n", player->name);
            pthread_mutex_lock(&players_mutex);
            if (!game_over) { // Garante que a lógica de fim de jogo execute apenas uma vez
                game_over = 1;
                int other_player_id = (player->id == 0) ? 1 : 0;
                if (players[other_player_id].socket != 0) {
                    send_to_player(players[other_player_id].socket, "O adversario desconectou. Jogo encerrado.");
                }
                pthread_cond_broadcast(&turn_cond); // Acorda a outra thread para que ela possa sair
            }
            pthread_mutex_unlock(&players_mutex);
            break; // Sai do loop
        }
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\n")] = 0;

        // Como já garantimos que é o turno do jogador, só precisamos verificar o comando
        if (strncmp(buffer, CMD_FIRE, strlen(CMD_FIRE)) == 0) {
            handle_fire_command(player, buffer);
        } else {
            send_to_player(player->socket, "Comando invalido. E sua vez de atirar com FIRE.");
            printf("DEBUG: Jogador %s enviou comando invalido durante o turno: '%s'\n", player->name, buffer);
        }
    }

    // Se o jogo terminou e este socket ainda está aberto, envia CMD_END
    if (player->socket != 0) {
        send_to_player(player->socket, CMD_END);
        close(player->socket);
    }
    printf("DEBUG: Cliente %s desconectou e thread encerrada.\n", player->name);
    pthread_exit(NULL); // Encerrar a thread corretamente
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t tid[MAX_PLAYERS];
    int player_idx; // Índice para o jogador que está se conectando

    // Inicializa players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        init_player_state(&players[i]); // Usa a nova função de inicialização
        players[i].id = i;
        players[i].socket = 0; // 0 significa socket nao conectado/inicializado
        // =================== INÍCIO: REGIÃO DE PARALELISMO ===================
        // Cada jogador possui seu próprio mutex para proteger seu estado individual
        pthread_mutex_init(&players[i].lock, NULL); // Inicializa o mutex para cada jogador
        // =================== FIM: REGIÃO DE PARALELISMO ===================
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, MAX_PLAYERS);

    printf("Servidor de Batalha Naval iniciado na porta %d...\n", PORT);

    while (1) { // Loop infinito para aceitar conexões (agora mais robusto)
        // Reinicia o estado do jogo se ele terminou
        if (game_over) {
            printf("DEBUG: Jogo anterior terminou. Resetando estado do servidor.\n");
            pthread_mutex_lock(&players_mutex);
            game_over = 0;
            game_started = 0;
            current_player_turn = -1;
            num_connected_players = 0; // Resetar contagem de jogadores conectados
            for (int i = 0; i < MAX_PLAYERS; i++) {
                // Não inicializa socket aqui, pois ele será atribuído novamente no accept
                // Apenas reseta o estado do player para uma nova partida
                init_player_state(&players[i]);
            }
            pthread_mutex_unlock(&players_mutex);
        }

        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("accept");
            continue;
        }

        // =================== INÍCIO: REGIÃO CRÍTICA GLOBAL ===================
        pthread_mutex_lock(&players_mutex); 
        if (num_connected_players < MAX_PLAYERS) {
            player_idx = num_connected_players; // Pega o próximo slot disponível
            players[player_idx].socket = new_socket;
            // =================== INÍCIO: REGIÃO DE PARALELISMO ===================
            // Cria uma thread para cada jogador conectado. Cada thread executa a função handle_client.
            pthread_create(&tid[player_idx], NULL, handle_client, (void *)&players[player_idx]);
            pthread_detach(tid[player_idx]); // Desatacha a thread para não precisar de pthread_join
            // =================== FIM: REGIÃO DE PARALELISMO ===================
            num_connected_players++;
            printf("DEBUG: Nova conexao aceita. Slot %d. Total conectado: %d\n", player_idx, num_connected_players);
            
        } else {
            send(new_socket, "Jogo cheio. Tente mais tarde.\n", strlen("Jogo cheio. Tente mais tarde.\n"), 0);
            close(new_socket);
            printf("DEBUG: Conexao rejeitada: Jogo cheio (socket %d).\n", new_socket);
        }
        pthread_mutex_unlock(&players_mutex); // =================== FIM: REGIÃO CRÍTICA GLOBAL ===================
    }

    // Este loop nunca será alcançado em um servidor infinito.
    // pthread_join(tid[i], NULL); está agora no handle_client via pthread_detach.

    close(server_fd);
    printf("Servidor encerrado.\n");

    // Destroi mutexes (isso só aconteceria se o servidor fosse desligado, o que é raro para um servidor)
    for (int i = 0; i < MAX_PLAYERS; i++) {
        pthread_mutex_destroy(&players[i].lock);
    }
    pthread_mutex_destroy(&players_mutex);
    pthread_cond_destroy(&all_players_ready_cond);
    pthread_cond_destroy(&turn_cond);

    return 0;
}