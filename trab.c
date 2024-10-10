#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define NUM_PROCESSES 3
#define TIME_SLICE 4
#define MAX_ITERATIONS 6

pid_t processes[NUM_PROCESSES];   // ids dos processos
int pc[NUM_PROCESSES];            // contador de execução de cada processo
int blocked[NUM_PROCESSES] = {0}; // (0 = desbloqueado, 1 = bloqueado)
int finished[NUM_PROCESSES] = {0}; // (0 = não terminado, 1 = terminado)
int current_process = -1;         // processo atual
int fd[2];                        // pipe para comunicação de interrupções de I/O

void handle_time_slice(int sig);
void handle_io_interrupt(int sig);
void start_processes();
void start_kernel();
void inter_controller_sim();

void start_processes(){
    for (int i = 0; i < NUM_PROCESSES; i++){
        pid_t pid = fork();
        if (pid == 0){
            //srand(getpid());
            while (pc[i] < MAX_ITERATIONS){
                // Processos ficam aguardando por SIGCONT até serem retomados pelo KernelSim
                kill(getpid(), SIGSTOP); // O processo fica pausado até o KernelSim retomar com SIGCONT

                // Quando o processo é retomado, ele executa
                pc[i]++;
                printf("Processo %d executando, PC: %d\n", i, pc[i]);
                sleep(1); 

                if (rand() % 100 < 15){
                    int device = rand() % 2; // D1 ou D2
                    printf("Processo %d solicitou I/O no dispositivo D%d.\n", i, device + 1);
                    write(fd[1], &i, sizeof(i)); 
                    printf("Processo %d foi bloqueado.\n", i);
                    blocked[i] = 1; 
                    kill(getpid(), SIGSTOP);
                }
            }
            printf("Processo %d atingiu o número máximo de iterações e será terminado.\n", i);
            exit(0); 
        }
        else{
            processes[i] = pid; 
            pc[i] = 0;          
        }
    }
}

void start_kernel(){
    signal(SIGALRM, handle_time_slice); 
    signal(SIGUSR1, handle_io_interrupt); 
    
    alarm(TIME_SLICE); // Inicia o alarme para o primeiro time slice

    if (fork() == 0){ 
        inter_controller_sim();
        exit(0);
    }

    // Iniciar o primeiro processo manualmente
    current_process = 0;
    kill(processes[current_process], SIGCONT); // Retoma o primeiro processo
    printf("Processo %d retomado.\n", current_process);

    while (1){
        pause(); // Espera por sinais
    }
}

void inter_controller_sim(){
    while (1){
        sleep(TIME_SLICE);        // IRQ0 (TimeSlice)
        kill(getppid(), SIGALRM); // Envia sinal de time slice para o KernelSim

        // Simula interrupção de I/O (IRQ1 ou IRQ2) com probabilidades
        if (rand() % 100 < 10){ 
            kill(getppid(), SIGUSR1); // IRQ1 ou IRQ2 
        }
    }
}

void handle_time_slice(int sig){
    if (current_process != -1 && !finished[current_process]) {
        kill(processes[current_process], SIGSTOP); // Pausa o processo atual
        printf("Processo %d interrompido.\n", current_process);
    }

    // Escolhe o próximo processo para rodar
    do {
        current_process = (current_process + 1) % NUM_PROCESSES;
    } while (blocked[current_process] || finished[current_process]); // Pula processos bloqueados ou terminados

    if (!finished[current_process]) {
        kill(processes[current_process], SIGCONT); // Continua o próximo processo
        printf("Processo %d retomado.\n", current_process);
    }

    alarm(TIME_SLICE); // Reinicia o time slice
}

void handle_io_interrupt(int sig){
    int blocked_process;
    read(fd[0], &blocked_process, sizeof(blocked_process)); // Lê processo bloqueado
    printf("Processo %d liberado do I/O.\n", blocked_process);
    blocked[blocked_process] = 0;              // Desbloqueia o processo
    kill(processes[blocked_process], SIGCONT); // Continua o processo desbloqueado
}

int main(){
    printf("Iniciando KernelSim...\n");

    if (pipe(fd) < 0){ 
        perror("Erro ao criar o pipe");
        exit(1);
    }

    start_processes(); 

    start_kernel(); 

    for (int i = 0; i < NUM_PROCESSES; i++){
        wait(NULL); 
        finished[i] = 1;
    }

    return 0;
}
