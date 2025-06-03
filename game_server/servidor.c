#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/select.h>

#define PORT 8080
#define MAX_PLAYERS 100
#define NUM_WORKER_THREADS 4
#define HP_INICIAL 10
#define TEMPO_ESCOLHA 15
#define TEMPO_MAXIMO_CONEXAO 20

typedef struct {
    int id;
    int socket;
    int hp;
    bool conectado;
    bool em_batalha;
    int escolha;
    bool escolheu;
    pthread_mutex_t lock;
} Jogador;

typedef struct evento {
    int tipo;
    Jogador *origem;
    Jogador *alvo;
    int valor;
    TAILQ_ENTRY(evento) entries;
} Evento;

typedef struct {
    Jogador *j1;
    Jogador *j2;
    int *vencedor;
} Confronto;

typedef struct {
    pthread_t thread;
    int carga;
    int socket_pool[MAX_PLAYERS];
    pthread_mutex_t lock;
} WorkerThread;

typedef struct {
    Jogador **participantes;
    int num_participantes;
    int rodada_atual;
    pthread_mutex_t lock;
} Torneio;

TAILQ_HEAD(event_queue, evento) event_queue = TAILQ_HEAD_INITIALIZER(event_queue);
pthread_mutex_t event_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

Jogador jogadores[MAX_PLAYERS];
WorkerThread workers[NUM_WORKER_THREADS];
Torneio torneio;
int jogadores_conectados = 0;
pthread_t jogador_threads[MAX_PLAYERS];
bool tempo_esgotado = false;
bool servidor_rodando = true;

void enviar_mensagem(Jogador *j, const char *msg) {
    if (j->conectado && j->socket != -1) {
        if (send(j->socket, msg, strlen(msg), 0) < 0) {
            perror("Erro ao enviar mensagem");
            close(j->socket);
            j->socket = -1;
            j->conectado = false;
        }
    }
}

void enviar_mensagem_formatada(Jogador *j, const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    enviar_mensagem(j, buffer);
}

void *handle_player(void *arg) {
    Jogador *jog = (Jogador *)arg;
    char buffer[1024];

    while (jog->conectado && servidor_rodando) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(jog->socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            if (bytes < 0) perror("Erro no recv");
            printf("Jogador %d desconectado.\n", jog->id);
            pthread_mutex_lock(&jog->lock);
            jog->conectado = false;
            if (jog->socket != -1) {
                close(jog->socket);
                jog->socket = -1;
            }
            pthread_mutex_unlock(&jog->lock);
            break;
        }

        pthread_mutex_lock(&jog->lock);
        if (jog->em_batalha) {
            int escolha = atoi(buffer);
            if (escolha == 1 || escolha == 2) {
                if (!jog->escolheu) {
                    jog->escolha = escolha;
                    jog->escolheu = true;
                    enviar_mensagem(jog, "Escolha registrada!\n");
                    
                    Evento *e = malloc(sizeof(Evento));
                    if (e == NULL) {
                        perror("Erro ao alocar evento");
                        pthread_mutex_unlock(&jog->lock);
                        continue;
                    }
                    e->tipo = escolha;
                    e->origem = jog;
                    
                    pthread_mutex_lock(&event_queue_mutex);
                    TAILQ_INSERT_TAIL(&event_queue, e, entries);
                    pthread_mutex_unlock(&event_queue_mutex);
                }
            } else {
                enviar_mensagem(jog, "Opcao invalida! Use 1 (Atacar) ou 2 (Defender)\n");
            }
        }
        pthread_mutex_unlock(&jog->lock);
    }
    return NULL;
}

void *worker_thread(void *arg) {
    WorkerThread *worker = (WorkerThread *)arg;
    
    while (servidor_rodando) {
        pthread_mutex_lock(&event_queue_mutex);
        if (!TAILQ_EMPTY(&event_queue)) {
            Evento *e = TAILQ_FIRST(&event_queue);
            TAILQ_REMOVE(&event_queue, e, entries);
            pthread_mutex_unlock(&event_queue_mutex);
            
            free(e);
        } else {
            pthread_mutex_unlock(&event_queue_mutex);
        }
        
        pthread_mutex_lock(&worker->lock);
        for (int i = 0; i < worker->carga && servidor_rodando; i++) {
            if (worker->socket_pool[i] != -1) {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(worker->socket_pool[i], &readfds);
                
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 10000;
                
                int result = select(worker->socket_pool[i]+1, &readfds, NULL, NULL, &tv);
                if (result < 0) {
                    if (errno != EINTR && errno != EBADF) {
                        perror("Erro no select");
                    }
                    continue;
                }
                
                if (result > 0 && FD_ISSET(worker->socket_pool[i], &readfds)) {
                    // Processar dados do jogador
                }
            }
        }
        pthread_mutex_unlock(&worker->lock);
        
        usleep(1000);
    }
    
    return NULL;
}

void *timer_conexao(void *arg) {
    sleep(TEMPO_MAXIMO_CONEXAO);
    tempo_esgotado = true;
    printf("Tempo limite de conexao atingido (%d segundos).\n", TEMPO_MAXIMO_CONEXAO);
    return NULL;
}

void *batalha(void *args) {
    Confronto *c = (Confronto *)args;
    Jogador *j1 = c->j1;
    Jogador *j2 = c->j2;
    int *vencedor = c->vencedor;
    
    srand(time(NULL) + j1->id + j2->id + (unsigned int)pthread_self());
    
    enviar_mensagem_formatada(j1, "Seu oponente e o jogador %d\n", j2->id);
    enviar_mensagem_formatada(j2, "Seu oponente e o jogador %d\n", j1->id);
    
    while (j1->hp > 0 && j2->hp > 0 && servidor_rodando) {
        // Turno do jogador 1
        pthread_mutex_lock(&j1->lock);
        j1->escolheu = false;
        pthread_mutex_unlock(&j1->lock);
        
        enviar_mensagem(j1, "E a sua vez, escolha: 1 (Atacar) ou 2 (Defender)\n");
        enviar_mensagem_formatada(j2, "E a vez do oponente (jogador %d)\n", j1->id);
        
        struct timeval start, now;
        gettimeofday(&start, NULL);
        
        while (servidor_rodando) {
            pthread_mutex_lock(&j1->lock);
            bool escolheu = j1->escolheu;
            pthread_mutex_unlock(&j1->lock);
            
            if (escolheu) break;
            
            gettimeofday(&now, NULL);
            if ((now.tv_sec - start.tv_sec) >= TEMPO_ESCOLHA) {
                enviar_mensagem(j1, "Tempo esgotado! Turno perdido.\n");
                break;
            }
            usleep(100000);
        }

        if (!servidor_rodando) break;

        // Processar escolha do jogador 1
        pthread_mutex_lock(&j1->lock);
        int escolha_j1 = j1->escolha;
        pthread_mutex_unlock(&j1->lock);
        
        if (escolha_j1 == 1) { // Ataque
            int dano = (rand() % 100) < 20 ? 4 : 2;
            pthread_mutex_lock(&j2->lock);
            if (j2->escolha == 2) { // Defesa
                dano = 1;
                enviar_mensagem_formatada(j2, "Voce defendeu! Dano reduzido para %d\n", dano);
            }
            j2->hp -= dano;
            pthread_mutex_unlock(&j2->lock);
            
            enviar_mensagem_formatada(j1, "Voce atacou causando %d de dano! HP do oponente: %d\n", dano, j2->hp);
            enviar_mensagem_formatada(j2, "Jogador %d atacou causando %d de dano! Seu HP: %d\n", j1->id, dano, j2->hp);
        }

        if (j2->hp <= 0) break;

        // Turno do jogador 2
        pthread_mutex_lock(&j2->lock);
        j2->escolheu = false;
        pthread_mutex_unlock(&j2->lock);
        
        enviar_mensagem(j2, "E a sua vez, escolha: 1 (Atacar) ou 2 (Defender)\n");
        enviar_mensagem_formatada(j1, "E a vez do oponente (jogador %d)\n", j2->id);
        
        gettimeofday(&start, NULL);
        
        while (servidor_rodando) {
            pthread_mutex_lock(&j2->lock);
            bool escolheu = j2->escolheu;
            pthread_mutex_unlock(&j2->lock);
            
            if (escolheu) break;
            
            gettimeofday(&now, NULL);
            if ((now.tv_sec - start.tv_sec) >= TEMPO_ESCOLHA) {
                enviar_mensagem(j2, "Tempo esgotado! Turno perdido.\n");
                break;
            }
            usleep(100000);
        }

        if (!servidor_rodando) break;

        // Processar escolha do jogador 2
        pthread_mutex_lock(&j2->lock);
        int escolha_j2 = j2->escolha;
        pthread_mutex_unlock(&j2->lock);
        
        if (escolha_j2 == 1) { // Ataque
            int dano = (rand() % 100) < 20 ? 4 : 2;
            pthread_mutex_lock(&j1->lock);
            if (j1->escolha == 2) { // Defesa
                dano = 1;
                enviar_mensagem_formatada(j1, "Voce defendeu! Dano reduzido para %d\n", dano);
            }
            j1->hp -= dano;
            pthread_mutex_unlock(&j1->lock);
            
            enviar_mensagem_formatada(j2, "Voce atacou causando %d de dano! HP do oponente: %d\n", dano, j1->hp);
            enviar_mensagem_formatada(j1, "Jogador %d atacou causando %d de dano! Seu HP: %d\n", j2->id, dano, j1->hp);
        }
    }
    
    if (servidor_rodando) {
        *vencedor = (j1->hp > 0) ? j1->id : j2->id;
        
        if (*vencedor == j1->id) {
            enviar_mensagem(j1, "Voce VENCEU esta rodada!\n");
            enviar_mensagem(j2, "Voce PERDEU esta rodada!\n");
        } else {
            enviar_mensagem(j2, "Voce VENCEU esta rodada!\n");
            enviar_mensagem(j1, "Voce PERDEU esta rodada!\n");
        }
    }

    // Resetar HP para próxima rodada
    pthread_mutex_lock(&j1->lock);
    j1->hp = HP_INICIAL;
    j1->em_batalha = false;
    pthread_mutex_unlock(&j1->lock);
    
    pthread_mutex_lock(&j2->lock);
    j2->hp = HP_INICIAL;
    j2->em_batalha = false;
    pthread_mutex_unlock(&j2->lock);
    
    free(c);
    return NULL;
}

void inicializar_torneio() {
    torneio.participantes = malloc(jogadores_conectados * sizeof(Jogador*));
    int participantes_atual = 0;
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        pthread_mutex_lock(&jogadores[i].lock);
        if (jogadores[i].conectado) {
            torneio.participantes[participantes_atual] = &jogadores[i];
            torneio.participantes[participantes_atual]->hp = HP_INICIAL;
            torneio.participantes[participantes_atual]->em_batalha = false;
            participantes_atual++;
        }
        pthread_mutex_unlock(&jogadores[i].lock);
    }
    
    torneio.num_participantes = participantes_atual;
    torneio.rodada_atual = 0;
    pthread_mutex_init(&torneio.lock, NULL);
}

void *executar_torneio(void *arg) {
    while (torneio.num_participantes > 1 && servidor_rodando) {
        pthread_mutex_lock(&torneio.lock);
        
        printf("Rodada %d com %d participantes\n", 
               torneio.rodada_atual + 1, torneio.num_participantes);
        
        // Criar threads para todas as batalhas desta rodada
        pthread_t *batalha_threads = malloc((torneio.num_participantes / 2) * sizeof(pthread_t));
        int *vencedores = malloc(torneio.num_participantes * sizeof(int));
        int batalhas = 0;
        int num_vencedores = 0;
        
        for (int i = 0; i < torneio.num_participantes && servidor_rodando; i += 2) {
            if (i + 1 >= torneio.num_participantes) {
                vencedores[num_vencedores++] = torneio.participantes[i]->id;
                enviar_mensagem(torneio.participantes[i], "Voce recebeu um BYE e avancou para a proxima rodada!\n");
                continue;
            }
            
            Confronto *c = malloc(sizeof(Confronto));
            c->j1 = torneio.participantes[i];
            c->j2 = torneio.participantes[i+1];
            c->vencedor = &vencedores[num_vencedores];
            
            c->j1->hp = HP_INICIAL;
            c->j2->hp = HP_INICIAL;
            c->j1->em_batalha = true;
            c->j2->em_batalha = true;
            
            if (pthread_create(&batalha_threads[batalhas], NULL, batalha, c) != 0) {
                perror("Erro ao criar thread de batalha");
                free(c);
                continue;
            }
            batalhas++;
            num_vencedores++;
        }
        
        // Esperar todas as batalhas terminarem
        for (int i = 0; i < batalhas && servidor_rodando; i++) {
            pthread_join(batalha_threads[i], NULL);
        }
        
        if (!servidor_rodando) {
            free(batalha_threads);
            free(vencedores);
            pthread_mutex_unlock(&torneio.lock);
            break;
        }

        // Atualizar participantes para próxima rodada
        int novos_participantes = 0;
        for (int i = 0; i < num_vencedores && servidor_rodando; i++) {
            for (int j = 0; j < torneio.num_participantes; j++) {
                if (torneio.participantes[j]->id == vencedores[i]) {
                    torneio.participantes[novos_participantes++] = torneio.participantes[j];
                    break;
                }
            }
        }
        
        torneio.num_participantes = novos_participantes;
        torneio.rodada_atual++;
        
        free(batalha_threads);
        free(vencedores);
        
        pthread_mutex_unlock(&torneio.lock);
        
        // Pequena pausa entre rodadas
        if (servidor_rodando) {
            sleep(1);
        }
    }
    
    if (servidor_rodando && torneio.num_participantes == 1) {
        printf("CAMPEAO DO TORNEIO: Jogador %d\n", torneio.participantes[0]->id);
        enviar_mensagem(torneio.participantes[0], "PARABENS! Voce e o CAMPEAO DO TORNEIO!\n");
        
        sleep(1);
    }
    
    return NULL;
}

void encerrar_conexoes() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        pthread_mutex_lock(&jogadores[i].lock);
        if (jogadores[i].socket != -1) {
            shutdown(jogadores[i].socket, SHUT_RDWR);
            close(jogadores[i].socket);
            jogadores[i].socket = -1;
        }
        jogadores[i].conectado = false;
        pthread_mutex_unlock(&jogadores[i].lock);
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    
    // Inicializar workers
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        workers[i].carga = 0;
        for (int j = 0; j < MAX_PLAYERS; j++) {
            workers[i].socket_pool[j] = -1;
        }
        pthread_mutex_init(&workers[i].lock, NULL);
        if (pthread_create(&workers[i].thread, NULL, worker_thread, &workers[i]) != 0) {
            perror("Erro ao criar worker thread");
            exit(EXIT_FAILURE);
        }
    }
    
    // Inicializar jogadores
    for (int i = 0; i < MAX_PLAYERS; i++) {
        jogadores[i].id = i + 1;
        jogadores[i].socket = -1;
        jogadores[i].conectado = false;
        jogadores[i].em_batalha = false;
        jogadores[i].hp = HP_INICIAL;
        pthread_mutex_init(&jogadores[i].lock, NULL);
    }
    
    // Criar socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }
    
    // Configurar socket
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Erro setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Erro no bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, MAX_PLAYERS) < 0) {
        perror("Erro no listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Servidor aguardando conexoes por %d segundos...\n", TEMPO_MAXIMO_CONEXAO);
    
    // Iniciar timer para conexões
    pthread_t timer_thread;
    if (pthread_create(&timer_thread, NULL, timer_conexao, NULL) != 0) {
        perror("Erro ao criar timer thread");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    while (!tempo_esgotado && servidor_rodando) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int activity = select(server_fd + 1, &readfds, NULL, NULL, &tv);
        
        if (activity < 0 && errno != EINTR) {
            if (errno != EBADF) {
                perror("Erro no select");
            }
            break;
        }
        
        if (activity > 0 && FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
                if (errno != EINTR) {
                    perror("Erro no accept");
                }
                continue;
            }
            
            if (jogadores_conectados >= MAX_PLAYERS) {
                printf("Maximo de jogadores atingido. Conexao recusada.\n");
                close(new_socket);
                continue;
            }
            
            // Encontrar worker com menor carga
            int menor_carga = 0;
            for (int i = 1; i < NUM_WORKER_THREADS; i++) {
                if (workers[i].carga < workers[menor_carga].carga) {
                    menor_carga = i;
                }
            }
            
            pthread_mutex_lock(&workers[menor_carga].lock);
            workers[menor_carga].socket_pool[workers[menor_carga].carga++] = new_socket;
            pthread_mutex_unlock(&workers[menor_carga].lock);
            
            // Configurar jogador
            int indice_jogador = -1;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                pthread_mutex_lock(&jogadores[i].lock);
                if (!jogadores[i].conectado) {
                    indice_jogador = i;
                    jogadores[i].socket = new_socket;
                    jogadores[i].hp = HP_INICIAL;
                    jogadores[i].conectado = true;
                    jogadores[i].em_batalha = false;
                    jogadores[i].escolheu = false;
                    jogadores_conectados++;
                    pthread_mutex_unlock(&jogadores[i].lock);
                    break;
                }
                pthread_mutex_unlock(&jogadores[i].lock);
            }
            
            if (indice_jogador == -1) {
                printf("Erro: Nenhum slot de jogador disponivel\n");
                close(new_socket);
                continue;
            }
            
            if (pthread_create(&jogador_threads[indice_jogador], NULL, handle_player, &jogadores[indice_jogador]) != 0) {
                perror("Erro ao criar thread do jogador");
                pthread_mutex_lock(&jogadores[indice_jogador].lock);
                jogadores[indice_jogador].conectado = false;
                close(new_socket);
                jogadores[indice_jogador].socket = -1;
                jogadores_conectados--;
                pthread_mutex_unlock(&jogadores[indice_jogador].lock);
                continue;
            }
            
            printf("Jogador %d conectado.\n", jogadores[indice_jogador].id);
        }
    }
    
    pthread_join(timer_thread, NULL);
    
    // Verificar se há jogadores suficientes
    if (jogadores_conectados < 2) {
        printf("Numero insuficiente de jogadores conectados (%d). Encerrando.\n", jogadores_conectados);
        servidor_rodando = false;
        encerrar_conexoes();
        close(server_fd);
        return 0;
    }
    
    printf("Iniciando torneio com %d jogadores...\n", jogadores_conectados);
    
    // Iniciar torneio
    inicializar_torneio();
    
    pthread_t torneio_thread;
    if (pthread_create(&torneio_thread, NULL, executar_torneio, NULL) != 0) {
        perror("Erro ao criar thread do torneio");
        servidor_rodando = false;
        encerrar_conexoes();
        close(server_fd);
        return 1;
    }
    
    pthread_join(torneio_thread, NULL);
    
    // Sinalizar encerramento
    servidor_rodando = false;
    
    // Esperar as worker threads terminarem
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        pthread_join(workers[i].thread, NULL);
    }
    
    // Encerrar todas as conexões
    encerrar_conexoes();
    
    printf("Torneio finalizado. Encerrando servidor.\n");
    close(server_fd);
    return 0;
}