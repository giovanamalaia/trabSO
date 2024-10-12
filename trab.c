#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <time.h>

#define NUM_PROCESSES 3
#define MAX_ITERATIONS 30

pid_t processes[NUM_PROCESSES];
int *pc;                   // Usando memória compartilhada
int *blocked;              // Usando memória compartilhada
int *finished;             // Usando memória compartilhada
int *blocked_on_device;    // Usando memória compartilhada para dispositivo bloqueado
int current_process = -1;
int fd[2];                 // Pipe para comunicação com o controlador
int io_device_queue[2][NUM_PROCESSES]; // Fila de dispositivos D1 e D2
int time_slice = 3;        // Time slice de 3 segundos

void handle_time_slice(int sig);
void handle_io_interrupt(int sig);
void handle_sigstp(int sig); // Manipulador do SIGTSTP
void start_processes();
void start_kernel();
void inter_controller_sim();
void setup_shared_memory();
void print_process_info();

void setup_shared_memory() {
    int shmid_pc = shmget(IPC_PRIVATE, NUM_PROCESSES * sizeof(int), IPC_CREAT | 0666);
    pc = shmat(shmid_pc, NULL, 0);

    int shmid_blocked = shmget(IPC_PRIVATE, NUM_PROCESSES * sizeof(int), IPC_CREAT | 0666);
    blocked = shmat(shmid_blocked, NULL, 0);

    int shmid_finished = shmget(IPC_PRIVATE, NUM_PROCESSES * sizeof(int), IPC_CREAT | 0666);
    finished = shmat(shmid_finished, NULL, 0);

    int shmid_blocked_on_device = shmget(IPC_PRIVATE, NUM_PROCESSES * sizeof(int), IPC_CREAT | 0666);
    blocked_on_device = shmat(shmid_blocked_on_device, NULL, 0);

    for (int i = 0; i < NUM_PROCESSES; i++) {
        pc[i] = 0;
        blocked[i] = 0;
        finished[i] = 0;
        blocked_on_device[i] = 0;
    }
}

void print_process_info() {
    printf("\nEstado dos processos:\n");
    for (int i = 0; i < NUM_PROCESSES; i++) {
        printf("Processo %d:\n", i + 1);
        printf("  PC: %d\n", pc[i]);

        if (finished[i]) {
            printf("  Estado: Terminado\n");
        } else if (blocked[i]) {
            printf("  Estado: Bloqueado no dispositivo D%d\n", blocked_on_device[i]);
        } else if (i == current_process) {
            printf("  Estado: Executando\n");
        } else {
            printf("  Estado: Pronto para execução\n");
        }
        printf("\n");
    }
}

void handle_time_slice(int sig) {
    if (current_process != -1 && !finished[current_process]) {
        kill(processes[current_process], SIGSTOP);
        printf("Processo %d interrompido.\n", current_process + 1);
    }

    // Escolhe o próximo processo não bloqueado ou terminado
    do {
        current_process = (current_process + 1) % NUM_PROCESSES;
    } while (blocked[current_process] || finished[current_process]);

    if (!finished[current_process]) {
        kill(processes[current_process], SIGCONT);
        printf("Processo %d retomado.\n", current_process + 1);
    }

    // Reinicia o alarme
    alarm(time_slice);
}

void handle_io_interrupt(int sig) {
    int device = (sig == SIGUSR1) ? 1 : 2;  // SIGUSR1 = D1, SIGUSR2 = D2
    printf("Gerando interrupção para D%d (SIGUSR%d)\n", device, sig == SIGUSR1 ? 1 : 2);

    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (blocked[i] && blocked_on_device[i] == device) {
            blocked[i] = 0;  // Desbloqueia o processo
            blocked_on_device[i] = 0;
            printf("Processo %d liberado do dispositivo D%d.\n", i + 1, device);
            break;  // Liberar apenas um processo por interrupção
        }
    }
}

void handle_sigstp(int sig) {
    printf("\nSinal SIGTSTP recebido (CTRL + Z). Pausando processos e exibindo informações:\n");

    // Pausar todos os processos ativos
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (!finished[i]) {
            kill(processes[i], SIGSTOP);
        }
    }

    // Exibir informações dos processos
    print_process_info();

    printf("\nExecução finalizada após SIGTSTP.\n");
    exit(0);  // Finaliza o programa após exibir as informações
}

void start_processes() {
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            while (pc[i] < MAX_ITERATIONS) {
                kill(getpid(), SIGSTOP); // Pausa até ser retomado

                while (1) {
                    if (pc[i] >= MAX_ITERATIONS) break;  // Termina se atingir o máximo
                    pc[i]++;
                    printf("Processo %d executando, PC: %d\n", i + 1, pc[i]);
                    sleep(1); // Simula trabalho do processo

                    // Simula I/O com 15% de chance
                    if (rand() % 100 < 15) {
                        int device = rand() % 2 + 1; // D1 ou D2
                        printf("Processo %d solicitou I/O no dispositivo D%d.\n", i + 1, device);
                        blocked[i] = 1;
                        blocked_on_device[i] = device;
                        printf("Processo %d foi bloqueado no dispositivo D%d.\n", i + 1, device);

                        // Enviar o sinal para forçar a troca imediata de processo
                        kill(getppid(), SIGALRM);  // Avisa o KernelSim que o processo foi bloqueado
                        kill(getpid(), SIGSTOP);    // Bloqueia o processo até o I/O terminar
                    }
                }
            }
            printf("Processo %d terminou.\n", i + 1);
            finished[i] = 1;
            exit(0);
        } else {
            processes[i] = pid;
            pc[i] = 0;
        }
    }
}

void start_kernel() {
    signal(SIGALRM, handle_time_slice); // Usa signal() para tratar SIGALRM
    signal(SIGTSTP, handle_sigstp);     // Manipulador do SIGTSTP para o CTRL + Z
    signal(SIGUSR1, handle_io_interrupt); // Sinal de interrupção para D1
    signal(SIGUSR2, handle_io_interrupt); // Sinal de interrupção para D2

    alarm(time_slice);  // Configura o alarme inicial para time_slice segundos

    current_process = 0;
    kill(processes[current_process], SIGCONT);
    printf("Processo %d iniciado.\n", current_process + 1);

    while (1) {
        pause(); // Espera por sinais
    }
}

void inter_controller_sim() {
    while (1) {
        sleep(2);  // Gera interrupções periódicas
        if (rand() % 2 == 0) {
            kill(getppid(), SIGUSR1);  // Gera interrupção para D1
        } else {
            kill(getppid(), SIGUSR2);  // Gera interrupção para D2
        }
    }
}

int main() {
    printf("Iniciando KernelSim...\n");

    setup_shared_memory(); // Configura a memória compartilhada

    if (pipe(fd) < 0) {
        perror("Erro ao criar o pipe");
        exit(1);
    }

    // Inicia o InterController Sim como um processo separado
    if (fork() == 0) {
        inter_controller_sim();
        exit(0);
    }

    start_processes();
    start_kernel();

    for (int i = 0; i < NUM_PROCESSES; i++) {
        wait(NULL);
        finished[i] = 1;
    }

    return 0;
}
