#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include "common.h"

int running = 1;
int send = 1;
pid_t pid;
request req;
user current_user;
user_message umsg;

void remove_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

int check_inputs(char *input, int num_args) {
    int conta_args = 1;

    for (int i = 0; i < strlen(input); ++i) {
        if (input[i] == ' ') conta_args++;
    }

    if (conta_args > num_args) {
        send = 0;
        return -1;
    }

    return 0;
}

void handle_signal(int sig) {
    if (sig == SIGTERM) {
        printf("SIGTERM\n");
        running = 0;
    } else if (sig == SIGINT) {
        printf("SIGINT\n");
        running = 0;
    } else if (sig == SIGUSR1) {
        printf("Manager terminou\n");
        running = 0;
    } else if (sig == SIGUSR2) {
        printf("%s(%d) removido pelo Manager\n", current_user.name, current_user.pid);
        running = 0;
    } else {
        printf("signal: %d\n", sig);
    }
}

void processa_comando(char *comando) {
    remove_newline(comando);
    memset(&req, 0, sizeof(req));

    if (strcmp(comando, "topics") == 0) {
        int comando_correto = check_inputs(comando, 1);
        if (comando_correto == 0) {
            req.pid = pid;
            req.type = TOPICS;
            strcpy(req.request_payload.string, "lista de topicos\n");
        } else {
            printf("Demasiados argumentos\n");
        }
    } else if (strncmp(comando, "subscribe ", 10) == 0) {
        int comando_correto = check_inputs(comando, 2);
        if (comando_correto == 0) {
            char topic_name[TOPIC_LENGTH];
            sscanf(comando + 10, "%s", topic_name);
            printf("Subscribing to: %s\n", topic_name);
            req.pid = pid;
            req.type = SUBSCRIBE;
            strcpy(req.request_payload.string, topic_name);
        } else {
            printf("Demasiados argumentos\n");
        }
    } else if (strncmp(comando, "unsubscribe ", 12) == 0) {
        int comando_correto = check_inputs(comando, 2);
        if (comando_correto == 0) {
            char topic_name[TOPIC_LENGTH];
            sscanf(comando + 12, "%s", topic_name);
            printf("Unsubscribing to: %s\n", topic_name);
            req.pid = pid;
            req.type = UNSUBSCRIBE;
            strcpy(req.request_payload.string, topic_name);
        } else {
            printf("Demasiados argumentos\n");
        }
    } else if (strncmp(comando, "msg ", 4) == 0) {
        char buffer[TOPIC_LENGTH + MSG_LENGTH + 50];
        strcpy(buffer, comando);

        char *token = strtok(buffer, " ");
        if (token == NULL) return;

        token = strtok(NULL, " ");
        if (token == NULL) return;
        strncpy(umsg.topic, token, TOPIC_LENGTH - 1);
        umsg.topic[TOPIC_LENGTH - 1] = '\0';

        token = strtok(NULL, " ");
        if (token == NULL) return;
        umsg.duration = atoi(token);

        token = strtok(NULL, "");
        if (token != NULL) {
            strncpy(umsg.msg, token, MSG_LENGTH - 1);
            umsg.msg[MSG_LENGTH - 1] = '\0';
        } else {
            umsg.msg[0] = '\0';
        }

        umsg.user = current_user;

        printf("%s\n", umsg.msg);

        req.pid = pid;
        req.type = MSG;
        req.request_payload.msg = umsg;
    } else if (strncmp(comando, "exit", 4) == 0) {
        int comando_correto = check_inputs(comando, 1);
        if (comando_correto == 0) {
            req.pid = pid;
            req.type = EXIT;
            strcpy(req.request_payload.string, "");
            running = 0;
        } else {
            printf("Demasiados argumentos\n");
        }
    } else {
        printf("Comando não reconhecido: %s\n", comando);
    }
}

int main(int argc, char **argv) {
    char nome_user[USERNAME_SIZE];
    setbuf(stdout, NULL);

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <username>\n", argv[0]);
        exit(EXIT_FAILURE);
    } else if (strlen(argv[1]) > 20) {
        fprintf(stderr, "Username too long\n");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    if (access(PIPE_NAME, W_OK) != 0) {
        printf("O servidor nao se encontra em execucao, ou nao tem permissao\n");
        exit(EXIT_FAILURE);
    }

    int fd = open(PIPE_NAME, O_WRONLY);
    if (fd == -1) {
        perror("Erro na abertura do named pipe para escrita");
        exit(EXIT_FAILURE);
    }

    pid = getpid();

    char pipe_produtor[20];
    sprintf(pipe_produtor, PIPE_PRODUTOR, pid);

    if (mkfifo(pipe_produtor, 0666) == -1) {
        perror("Erro na criacao do named pipe");
        close(fd);
        exit(EXIT_FAILURE);
    }

    int fd_produtor = open(pipe_produtor, O_RDWR | O_NONBLOCK);
    if (fd_produtor == -1) {
        perror("Erro na abertura do pipe do produtor");
        close(fd);
        unlink(pipe_produtor);
        exit(EXIT_FAILURE);
    }

    current_user.pid = pid;
    strcpy(current_user.name, argv[1]);

    req.pid = pid;
    req.type = LOGIN;
    strcpy(req.request_payload.string, argv[1]);

    int nbytes = write(fd, &req, sizeof(req));
    if (nbytes == -1) {
        if (errno != EPIPE) {
            perror("Erro ao escrever a mensagem no pipe");
        }
        close(fd);
        close(fd_produtor);
        unlink(pipe_produtor);
        exit(EXIT_FAILURE);
    }
    fd_set read_fds;
    int max_fd = fd_produtor > STDIN_FILENO ? fd_produtor : STDIN_FILENO;
    response response;

    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(fd_produtor, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int select_ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (select_ret == -1) {
            if (errno != EINTR) {
                perror("Erro no select");
                break;
            }

            if (!running) {
                break;
            }
        } else if (select_ret > 0) {
            if (FD_ISSET(fd_produtor, &read_fds)) {
                nbytes = read(fd_produtor, &response, sizeof(response));
                if (nbytes == -1) {
                    perror("Erro na leitura do named pipe do produtor");
                    break;
                }
                switch (response.type) {
                    case STATUS:
                        printf("%s\n", response.response_payload.status);
                        break;
                    case LIST_TOPICS:
                        printf("Topicos:\n");
                        for (int i = 0; i < response.n_messages; ++i) {
                            printf("%s\n", response.response_payload.topics[i]);
                        }
                        break;
                    default:
                        int i = 0;
                        if (strcmp(response.response_payload.msg[i].username, "aceite") == 0) {

                            printf("%s\n%s\n", response.response_payload.msg[i].msg,
                                   response.response_payload.msg[i].topics);
                            ++i;
                        }

                        for (i; i < response.n_messages; ++i) {
                            printf("%s %s %s\n", response.response_payload.msg[i].topics,
                                   response.response_payload.msg[i].username, response.response_payload.msg[i].msg);
                        }
                        break;
                }
            }

            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                char comando[256];
                if (fgets(comando, sizeof(comando), stdin) != NULL) {
                    processa_comando(comando);

                    // Envia o comando processado imediatamente após ser lido
                    if (send) {
                        nbytes = write(fd, &req, sizeof(req));
                        if (nbytes == -1) {
                            if (errno != EPIPE) {
                                perror("Erro ao escrever a mensagem no pipe");
                            }
                            close(fd);
                            close(fd_produtor);
                            unlink(pipe_produtor);
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            }
        }
    }

    printf("Vou terminar\n");

    close(fd);
    close(fd_produtor);
    unlink(pipe_produtor);

    return 0;
}
