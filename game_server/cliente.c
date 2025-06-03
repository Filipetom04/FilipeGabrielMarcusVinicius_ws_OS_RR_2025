#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/select.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    bool minha_vez = false;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Erro ao criar socket\n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Endereço inválido\n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Falha na conexão\n");
        return -1;
    }

    printf("Conectado ao servidor!\n");

    fd_set readfds;
    int max_sd;
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        max_sd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        int activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);
        
        if (activity < 0) {
            printf("Erro no select\n");
            break;
        }
        
        // Mensagem do servidor
        if (FD_ISSET(sock, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int valread = read(sock, buffer, BUFFER_SIZE);
            if (valread <= 0) {
                printf("Servidor desconectado\n");
                break;
            }
            
            printf("%s", buffer);
            
            if (strstr(buffer, "escolha")) {
                minha_vez = true;
                printf("Digite 1 para atacar ou 2 para defender: ");
                fflush(stdout);
            }
            
            if (strstr(buffer, "CAMPEAO") || strstr(buffer, "Torneio finalizado")) {
                break;
            }
        }
        
        // Input do usuário
        if (FD_ISSET(STDIN_FILENO, &readfds) && minha_vez) {
            memset(buffer, 0, BUFFER_SIZE);
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
                send(sock, buffer, strlen(buffer), 0);
                minha_vez = false;
            }
        }
    }

    printf("Fim da conexão.\n");
    close(sock);
    return 0;
}