# **Servidor de Jogos Multi-Thread para Matches (Torneio de Batalha por Turnos)**  

Este projeto implementa um **servidor de jogos multi-thread para match** que simula um torneio de batalhas por turnos entre jogadores conectados. O servidor gerencia conexões simultâneas, processa ações em paralelo e controla o estado do jogo com sincronização thread-safe. As execuções foram feitas em terminais nas seguintes distribuições Linux: Ubuntu 22.04.5 LTS e Arch Linux, kernel 6.14.6-arch1-1.

---

## **Características**  
✅ **Servidor Multi-Thread**  
- Processamento paralelo de jogadores usando threads dedicadas e worker threads.  
- Balanceamento de carga entre workers para distribuir conexões.  

✅ **Controle de Recursos Compartilhados**  
- Mutexes (`pthread_mutex_t`) protegem dados como:  
  - Lista de jogadores.  
  - Fila de eventos (`event_queue`).  
  - Estado do torneio.  

✅ **Sistema de Batalha por Turnos**  
- Jogadores escolhem entre **Atacar (1)** ou **Defender (2)**.  
- Dano calculado com chance de crítico (20% para 4 de dano, senão 2).  
- Defesa reduz dano para 1.  

✅ **Torneio com Múltiplas Rodadas**  
- Jogadores são pareados em confrontos até restar um **campeão**.  
- BYE (avance automático) se número de jogadores for ímpar.  

✅ **Timeout de Conexão**  
- Tempo limite (`TEMPO_MAXIMO_CONEXAO = 20s`) para aguardar jogadores.  

✅ **Comunicação Cliente-Servidor via TCP**  
- Clientes enviam escolhas (1/2) e recebem feedback em tempo real.  

---

## **Como Executar**  

### **Compilação**  
```bash
# Servidor
gcc servidor.c -o servidor -lpthread

# Cliente
gcc cliente.c -o cliente
```

### **Uso**  
1. **Inicie o servidor**:  
   ```bash
   ./servidor
   ```
   - Aguarda conexões por **20 segundos** antes de iniciar o torneio.  

2. **Conecte clientes** (em terminais separados):  
   ```bash
   ./cliente
   ```
   - O servidor suporta até **100 jogadores** (`MAX_PLAYERS`), porém os testes foram feitos com até no máximo 6 jogadores.  

3. **Fluxo do Jogo**:  
   - Cada jogador recebe instruções por turno.  
   - O torneio continua até que apenas um jogador reste.  

---

## **Arquitetura**  

### **Threads Principais**  
| Thread               | Função                                                                 |
|----------------------|-----------------------------------------------------------------------|
| **Worker Threads**   | Gerenciam pools de sockets para leitura assíncrona.                  |
| **Player Threads**   | Lidam com ações individuais dos jogadores (`handle_player`).         |
| **Torneio Thread**   | Orquestra as rodadas e batalhas (`executar_torneio`).                |
| **Timer Thread**     | Encerra a fase de conexões após `TEMPO_MAXIMO_CONEXAO`.              |

### **Estruturas de Dados**  
- **`Jogador`**: Armazena HP, socket, estado (em batalha/conectado).  
- **`Evento`**: Ações dos jogadores (ataque/defesa) em uma fila thread-safe.  
- **`Torneio`**: Gerencia participantes, rodadas e vencedores.  

---

## **Exemplo de Saída**  
### **Servidor**  
```plaintext
Jogador 1 conectado.  
Jogador 2 conectado.  
Iniciando torneio com 2 jogadores...  
Rodada 1: Jogador 1 vs Jogador 2  
CAMPEAO DO TORNEIO: Jogador 1  
```

### **Cliente**  
```plaintext
Conectado ao servidor!  
Seu oponente é o jogador 2.  
Escolha: 1 (Atacar) ou 2 (Defender)  
Voce atacou causando 2 de dano! HP do oponente: 8  
PARABENS! Voce é o CAMPEAO DO TORNEIO!  
```

---

## **Possíveis Melhorias**  
- **Mais Mecânicas**: Itens, habilidades especiais.  
- **Balanceamento Dinâmico**: Ajustar dano com base no HP restante.  

---

## **Autores**  
- Filipe Gabriel Tomaz Brito
- Marcus Vinícius Maia dos Santos
