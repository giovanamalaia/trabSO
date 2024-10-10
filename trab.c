#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>

#define NUM_PROCESSOS 3
#define TIME_SLICE 3  

int fila_d1[NUM_PROCESSOS], fila_d2[NUM_PROCESSOS];
int processos[NUM_PROCESSOS];  // PIDs dos processos
int atual = 0;  // Índice do processo que está executando


// Função chamada pelo KernelSim para alternar processos
void troca_de_contexto() {
    // Pausa o processo atual se estiver em execução
    kill(processos[atual], SIGSTOP);

    // Encontra o próximo processo não bloqueado
    int iniciador = atual;
    do {
        atual = (atual + 1) % NUM_PROCESSOS;

        // Verifica se o processo está bloqueado
        int bloqueado = 0;
        for (int i = 0; i < NUM_PROCESSOS; i++) {
            if (fila_d1[i] == processos[atual] || fila_d2[i] == processos[atual]) {
                bloqueado = 1;
                break;
            }
        }

        // Se não estiver bloqueado, sai do loop
        if (!bloqueado) break;

        // Se voltamos ao início, não há processos prontos
        if (atual == iniciador) {
            printf("Nenhum processo pronto para executar.\n");
            return;
        }
    } while (1);

    // Retoma a execução do novo processo
    kill(processos[atual], SIGCONT);
}

// Manipulador para SIGUSR1 (notificação de syscall)
void sigusr1_handler(int signum) {
    troca_de_contexto();  // Troca de contexto imediatamente ao receber o sinal
}

// Função para adicionar um processo à fila de bloqueio
void adicionar_fila(int fila[], int pid) {
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        if (fila[i] == 0) {
            fila[i] = pid;
            break;
        }
    }
}

// Função para remover um processo da fila de bloqueio
int remover_fila(int fila[]) {
    int pid = fila[0];
    for (int i = 0; i < NUM_PROCESSOS - 1; i++) {
        fila[i] = fila[i + 1];  // Desloca os processos na fila
    }
    fila[NUM_PROCESSOS - 1] = 0;
    return pid;
}

// Simulação de syscall
void syscall_e_s(int id) {
    printf("Processo %d fazendo syscall...\n", id);

    int dispositivo = rand() % 2;  // Escolhe aleatoriamente D1 ou D2
    if (dispositivo == 0) {
        printf("Processo %d bloqueado esperando D1\n", id);
        adicionar_fila(fila_d1, getpid());
    } else {
        printf("Processo %d bloqueado esperando D2\n", id);
        adicionar_fila(fila_d2, getpid());
    }

    // Notifica o KernelSim que o processo foi bloqueado
    kill(getppid(), SIGUSR1);

    // Suspende o processo
    raise(SIGSTOP);
}



// Função para gerar interrupções e desbloquear processos
void gerador_interrupcoes() {
    while (1) {
        sleep(2);  // Gera uma interrupção a cada 2 segundos

        int irq = rand() % 2;  // Aleatoriamente escolhe IRQ1 (D1) ou IRQ2 (D2)
        if (irq == 0 && fila_d1[0] != 0) {
            int pid = remover_fila(fila_d1);
            printf("Desbloqueando processo %d da fila D1\n", pid);
            kill(pid, SIGCONT);  // Desbloqueia o processo
        } else if (irq == 1 && fila_d2[0] != 0) {
            int pid = remover_fila(fila_d2);
            printf("Desbloqueando processo %d da fila D2\n", pid);
            kill(pid, SIGCONT);  // Desbloqueia o processo
        }
    }
}

// Função do processo de aplicação
void processo_aplicacao(int id) {
    int PC = 0;
    while (PC < 5) {
        printf("Processo %d executando, PC = %d\n", id, PC);
        PC++;

        // Aleatoriamente, o processo faz uma syscall
        if (rand()%100 < 20) {
            syscall_e_s(id);
        }

        sleep(1);  // Simula trabalho
    }
    printf("Processo %d terminou\n", id);
    exit(0);
}

int main() {
    srand(time(NULL));  // Inicializa o gerador de números aleatórios

    // Configura o manipulador de sinal para SIGUSR1
    signal(SIGUSR1, sigusr1_handler);

    // Inicializa as filas de bloqueio
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        fila_d1[i] = 0;
        fila_d2[i] = 0;
    }

    // Cria processos de aplicação
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        if ((processos[i] = fork()) == 0) {
            // Todos os processos devem ser iniciados e logo em seguida pausados, exceto o primeiro
            if (i > 0) raise(SIGSTOP);  // Pausa todos os processos, exceto o primeiro
            processo_aplicacao(i + 1);  // Inicia o processo de aplicação
        }
    }

    // Cria o processo gerador de interrupções
    if (fork() == 0) {
        gerador_interrupcoes();  // Roda o gerador de interrupções
    }

    // KernelSim controla o escalonamento
    while (1) {
        sleep(TIME_SLICE);  // Simula a fatia de tempo
        troca_de_contexto();  // Alterna processos
    }

    return 0;
}
