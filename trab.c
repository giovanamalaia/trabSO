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
int blocked_on_device[NUM_PROCESSES]; // (0 = não bloqueado, 1 = D1, 2 = D2)
int current_process = -1;         // processo atual
int fd[2];                        // pipe para comunicação de interrupções de I/O
int io_device_queue[2][NUM_PROCESSES]; // Fila de bloqueios de dispositivos D1 e D2

void handle_time_slice(int sig);
void handle_io_interrupt(int sig);
void start_processes();
void start_kernel();
void inter_controller_sim();
void enqueue_device_queue(int device, int process);
int dequeue_device_queue(int device);

void start_processes(){
    for (int i = 0; i < NUM_PROCESSES; i++){
        pid_t pid = fork();
        if (pid == 0){
            while (pc[i] < MAX_ITERATIONS){
                // Processos ficam aguardando por SIGCONT até serem retomados pelo KernelSim
                kill(getpid(), SIGSTOP); // O processo fica pausado até o KernelSim retomar com SIGCONT

                // Quando o processo é retomado, ele executa
                pc[i]++;
                printf("Processo %d executando, PC: %d\n", i, pc[i]);
                sleep(1); 

                // Simulação de I/O (com 15% de chance)
                if (rand() % 100 < 15){
                    int device = rand() % 2; // 0 = D1, 1 = D2
                    printf("Processo %d solicitou I/O no dispositivo D%d.\n", i, device + 1);
                    blocked_on_device[i] = device + 1; // Marca qual dispositivo está sendo usado
                    enqueue_device_queue(device, i); // Adiciona na fila do dispositivo
                    printf("Processo %d foi bloqueado no dispositivo D%d.\n", i, device + 1);
                    blocked[i] = 1; 
                    kill(getpid(), SIGSTOP); // Bloqueia o processo até que o I/O termine
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
    signal(SIGUSR2, handle_io_interrupt); // IRQ2 para dispositivo D2
    
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
        sleep(1);        // IRQ0 (TimeSlice), envia a cada 1 segundo
        kill(getppid(), SIGALRM); // Envia sinal de time slice para o KernelSim

        // Simula interrupção de I/O (IRQ1 e IRQ2) com probabilidades independentes
        if (rand() % 100 < 10){ 
            kill(getppid(), SIGUSR1); // IRQ1 - I/O finalizado no dispositivo D1
        }
        if (rand() % 100 < 5){ 
            kill(getppid(), SIGUSR2); // IRQ2 - I/O finalizado no dispositivo D2
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
    int device;
    if (sig == SIGUSR1) {
        device = 1; // D1
    } else if (sig == SIGUSR2) {
        device = 2; // D2
    } else {
        return; // Interrupção desconhecida
    }

    // Desbloqueia o primeiro processo da fila de I/O para o dispositivo correto
    int blocked_process = dequeue_device_queue(device - 1); 
    if (blocked_process >= 0) {
        printf("Processo %d liberado do I/O no dispositivo D%d.\n", blocked_process, device);
        blocked[blocked_process] = 0;              // Desbloqueia o processo
        blocked_on_device[blocked_process] = 0;    // Reseta o estado do dispositivo
        kill(processes[blocked_process], SIGCONT); // Continua o processo desbloqueado
    }
}

void enqueue_device_queue(int device, int process) {
    // Adiciona o processo na fila do dispositivo (D1 = 0, D2 = 1)
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (io_device_queue[device][i] == 0) {
            io_device_queue[device][i] = process + 1; // Salva o processo (1-indexado)
            break;
        }
    }
}

int dequeue_device_queue(int device) {
    // Remove o primeiro processo da fila do dispositivo (D1 = 0, D2 = 1)
    int process = -1;
    if (io_device_queue[device][0] > 0) {
        process = io_device_queue[device][0] - 1; // Pega o processo (0-indexado)
        for (int i = 0; i < NUM_PROCESSES - 1; i++) {
            io_device_queue[device][i] = io_device_queue[device][i + 1]; // Reorganiza a fila
        }
        io_device_queue[device][NUM_PROCESSES - 1] = 0; // Libera a última posição da fila
    }
    return process;
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
