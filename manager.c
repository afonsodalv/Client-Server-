#include <stdio.h>
#include <pthread.h>
#include "common.h"

#ifndef MSG_FICH
#define MSG_FICH "default_messages_file.txt"
#endif

int running = 1;
user users[10];
int countUsers = 0;
Topic topics[MAX_TOPICS];
int topic_count = 0;
pthread_mutex_t mutex;
int shutdown_expiration_manager = 0;

void remove_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

void save_persistent_messages(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        perror("Erro na abertura do ficheiro para salvaguarda das mensagens");
        return;
    }

    for (int i = 0; i < topic_count; ++i) {
        for (int j = 0; j < topics[i].msg_count; ++j) {
            fprintf(f, "%s %s %d %s\n", topics[i].msg[j].topic,
                    topics[i].msg[j].user.name,
                    topics[i].msg[j].duration,
                    topics[i].msg[j].msg);
        }
    }

    fclose(f);
    printf("Mensagens guardadas no %s\n", filename);
}

void sendMessage(pid_t pid, response response) {
    int nbytes;
    char pipe_produtor[20];
    int fd_produtor;

    sprintf(pipe_produtor, PIPE_PRODUTOR, pid);
    fd_produtor = open(pipe_produtor, O_WRONLY);
    if (fd_produtor == -1) {
        perror("erro na abertura do named pipe");
        return;
    }

    printf("Resposta serv: %s\n", response.response_payload.status);
    nbytes = write(fd_produtor, &response, sizeof(response));
    if (nbytes == -1) {
        perror("erro na escrita no pipe no produtor");
    }

    close(fd_produtor);
}

char *getUsername(pid_t pid) {
    int i = 0;
    for (; i < countUsers; ++i) {
        if (pid == users[i].pid) break;
    }
    return users[i].name;
}

void avisaSaidaUser(pid_t user_removido) {
    char *username_removido = getUsername(user_removido);
    response res;
    for (int i = 0; i < countUsers; ++i) {
        if (users[i].pid != user_removido) {
            res.type = STATUS;
            sprintf(res.response_payload.status, "%s removido\n", username_removido);
            sendMessage(users[i].pid, res);
        }
    }
}

int check_topic(const char *topic_name) {
    int topic_index = -1;

    // Verificar se o tópico já existe
    for (int i = 0; i < topic_count; i++) {
        if (strcmp(topics[i].name, topic_name) == 0) {
            topic_index = i;
            break;
        }
    }

    return topic_index;
}

void handle_signal(int sig) {
    if (sig == SIGINT) {
        printf("SIGINT received, terminating...\n");
        running = 0;
    }
}

void list_users() {
    for (int i = 0; i < countUsers; ++i) {
        printf("Utilizador: %s, PID: %d\n", users[i].name, users[i].pid);
    }
    printf("\n");
}

void list_topics() {
    if (topic_count == 0) {
        printf("Ainda não foram criados tópicos");
        return;
    }
    for (int i = 0; i < topic_count; ++i) {
        printf("Topico %d: %s\tNum mensagens persistentes: %d\n", i, topics[i].name, topics[i].msg_count);
    }
}

void processa_comando(char *comando) {
    remove_newline(comando);
    union sigval value = {0};
    response response;
    char buffer[50];

    if (strcmp(comando, "users") == 0) {
        printf("Comando 'users' recebido\n");
        list_users();
    } else if (strncmp(comando, "remove ", 7) == 0) {
        int i = 0, found_user = 0;
        char username[USERNAME_SIZE];
        sscanf(comando + 7, "%s", username);
        for (; i < countUsers; ++i) {
            if (strcmp(users[i].name, username) == 0) {
                found_user = 1;
                avisaSaidaUser(users[i].pid);
                sigqueue(users[i].pid, SIGUSR2, value);
                users[i] = users[countUsers - 1];
                countUsers--;
            }
        }
        if (found_user == 0) printf("User %s não existe\n", username);
    } else if (strcmp(comando, "topics") == 0) {
        list_topics();
    } else if (strcmp(comando, "close") == 0) {
        running = 0;
    } else if (strncmp(comando, "lock", 4) == 0) {
        int i = 0;
        char topic_name[MAX_TOPICS];
        sscanf(comando + 4, "%s", topic_name);
        for (; i < topic_count; ++i) {
            if (strcmp(topic_name, topics[i].name) == 0) {
                if (topics[i].blocked == 1) {
                    printf("Topico ja se encontra desbloqueado\n");
                    break;
                }
                topics[i].blocked = 1;
                printf("Topico %s bloqueado\n", topic_name);
                response.type = STATUS;
                sprintf(buffer, "Topico %s bloqueado", topic_name);
                strcpy(response.response_payload.status, buffer);
                for (int j = 0; j < topics[i].subscriber_count; ++j) {
                    sendMessage(topics[i].subscribers[j], response);
                }
                break;
            }
        }
        if (i == topic_count) printf("Não existe nenhum topico com esse nome\n");
    } else if (strncmp(comando, "unlock", 6) == 0) {
        int i = 0;
        char topic_name[MAX_TOPICS];
        sscanf(comando + 6, "%s", topic_name);
        for (; i < topic_count; ++i) {
            if (strcmp(topic_name, topics[i].name) == 0) {
                if (topics[i].blocked == 0) {
                    printf("Topico ja se encontra desbloqueado\n");
                    break;
                }

                topics[i].blocked = 0;
                printf("Topico %s desbloqueado\n", topic_name);
                response.type = STATUS;
                sprintf(buffer, "Topico %s desbloqueado", topic_name);
                strcpy(response.response_payload.status, buffer);
                for (int j = 0; j < topics[i].subscriber_count; ++j) {
                    sendMessage(topics[i].subscribers[j], response);
                }
                break;
            }
        }

        if (i == topic_count) printf("Não existe nenhum topico com esse nome\n");
    } else if (strncmp(comando, "show ", 5) == 0) {
        char topic_name[MAX_TOPICS];
        sscanf(comando + 5, "%s", topic_name);
        int i = check_topic(topic_name);
        if (i == -1)
            printf("Topicao nao existe\n");
        else {
            for (int j = 0; j < topics[i].msg_count; ++j) {
                printf("%s %s %d %s\n", topics[i].name, topics[i].msg[j].user.name, topics[i].msg[j].duration,
                       topics[i].msg[j].msg);
            }
        }

    } else {
        printf("Comando não reconhecido: %s\n", comando);
    }
}

void *stdin_thread(void *arg) {
    char comando[256];

    while (running) {
        if (fgets(comando, sizeof(comando), stdin) != NULL) {
            processa_comando(comando);
        }
    }

    pthread_exit(NULL);
}

int create_topic(char *topic_name) {
    int topic_index = topic_count++;

    strncpy(topics[topic_index].name, topic_name, sizeof(topics[topic_index].name) - 1);
    topics[topic_index].name[sizeof(topics[topic_index].name) - 1] = '\0';
    topics[topic_index].blocked = 0; // Começa desbloqueado
    topics[topic_index].subscriber_count = 0;
    topics[topic_index].msg_count = 0;

    return topic_index;
}

void deleteTopic(int topic_index) {
    if (topics[topic_index].subscriber_count == 0 && topics[topic_index].msg_count == 0) {
        topics[topic_index] = topics[topic_count - 1];
        topic_count--;
        printf(" Topico eliminado por falta de subscritores\n");
    }
}

char *subscribe_topic(char *topic_name, pid_t user_pid) {
    int topic_index = check_topic(topic_name);
    int b_size = 100;

    char *buffer = malloc(b_size);

    if (buffer == NULL) {
        perror("Erro na alucacao de memoria\n");
        snprintf(buffer, b_size, "Erro na alucacao de memoria");
        return buffer;
    }
    // Se o tópico não existir, criar um novo
    if (topic_index == -1) {
        if (topic_count >= MAX_TOPICS) {
            printf("Erro: Limite de tópicos atingido\n");
            snprintf(buffer, b_size, "Erro: Limite de tópicos atingido");
            return buffer;
        }

        topic_index = create_topic(topic_name);
    }

    for (int i = 0; i < topics[topic_index].subscriber_count; i++) {
        if (topics[topic_index].subscribers[i] == user_pid) {
            printf("Utilizador %d já está subscrito ao tópico '%s'\n", user_pid, topic_name);
            snprintf(buffer, b_size, "Utilizador já subscrito");
            return buffer; // Já subscrito
        }
    }

    if (topics[topic_index].subscriber_count < MAX_SUBSCRIBERS) {
        topics[topic_index].subscribers[topics[topic_index].subscriber_count++] = user_pid;
        printf("Utilizador %d subscreveu o tópico '%s'\n", user_pid, topic_name);
        snprintf(buffer, b_size, "Subscricao bem sucedida");
        return buffer; // Sucesso
    } else {
        printf("Erro: Limite de subscritores atingido para o tópico '%s'\n", topic_name);
        snprintf(buffer, b_size, "Erro: Limite de subscritores atingido para o tópico");
        return buffer; // Limite de subscritores atingido
    }
}

char *unsubscribe_topic(char *topic_name, pid_t user_pid) {
    int topic_index = check_topic(topic_name);
    int b_size = 100;

    char *buffer = malloc(b_size);

    if (buffer == NULL) {
        perror("Erro na alucacao de memoria\n");
        snprintf(buffer, b_size, "Erro na alucacao de memoria");
        return buffer;
    }

    if (topic_index == -1) {
        printf("Erro: Tópico não existe\n");
        snprintf(buffer, b_size, "Erro: Tópico não existe");
        return buffer;
    }

    for (int i = 0; i < topics[topic_index].subscriber_count; i++) {
        if (topics[topic_index].subscribers[i] == user_pid) {
            for (int j = i; j < topics[topic_index].subscriber_count - 1; j++) {
                topics[topic_index].subscribers[j] = topics[topic_index].subscribers[j + 1];
            }
            topics[topic_index].subscriber_count--;

            snprintf(buffer, b_size, "Subscricao cancelada com sucesso.");

            deleteTopic(topic_index);

            return buffer;
        }
    }

    snprintf(buffer, b_size, "Nao esta inscrito nesse topico");
    return buffer;
}

void *expiration_manager(void *arg) {
    int removed_flag = 0;
    while (!shutdown_expiration_manager) {
        sleep(1);

        pthread_mutex_lock(&mutex);

        for (int i = 0; i < topic_count; i++) {
            Topic *topic = &topics[i];

            for (int j = 0; j < topic->msg_count;) {
                if (topic->msg[j].duration > 0) {
                    topic->msg[j].duration--;
                    ++j;
                } else {
                    topic->msg[j] = topic->msg[topic->msg_count--];
                    deleteTopic(check_topic(topic->name));
                    removed_flag = 1;
                }
            }
            if (removed_flag == 1) {
                printf("Removed expired messages from topic '%s'.\n", topic->name);
                removed_flag = 0;
            }
        }

        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

void readFile(char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return;
    }

    user_message aux;
    while (fscanf(file, "%19s %19s %d %299[^\n]", aux.topic, aux.user.name, &aux.duration, aux.msg) == 4) {
        int found = 0;
        printf("%s %s %d %s\n", aux.topic, aux.user.name, aux.duration, aux.msg);
        // Check if the topic exists
        for (int i = 0; i < topic_count; ++i) {
            if (strcmp(topics[i].name, aux.topic) == 0) {
                // Ensure message count does not exceed limit
                aux.user.pid = 0; // Assuming pid is part of user_message
                topics[i].msg[topics[i].msg_count] = aux;
                topics[i].msg_count++;

                found = 1;
                break;
            }
        }

        // If the topic doesn't exist, create it
        if (!found) {
            // Ensure topic count does not exceed limit
            int new_index = create_topic(aux.topic);
            if (new_index != -1) {
                aux.user.pid = 0; // Assuming pid is part of user_message
                topics[new_index].msg[0] = aux;
                topics[new_index].msg_count = 1;
            } else {
                printf("Error: Failed to create topic '%s'.\n", aux.topic);
            }
        }
    }

    fclose(file);
}

int main() {
    setbuf(stdout, NULL);

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    union sigval value = {0};
    char *filename = MSG_FICH;

    if (access(PIPE_NAME, F_OK) == 0) {
        printf("O manager ja se encontra em execucao\n");
        exit(0);
    }

    if (mkfifo(PIPE_NAME, 0666) == -1) {
        if (errno != EEXIST) {
            perror("erro na criacao do named pipe");
            exit(EXIT_FAILURE);
        }
    }

    int fd = open(PIPE_NAME, O_RDWR | O_NONBLOCK); // Non-blocking open
    if (fd == -1) {
        perror("erro na abertura do named pipe para leitura");
        unlink(PIPE_NAME);
        exit(EXIT_FAILURE);
    }
    readFile(filename);

    int nbytes;

    // Cria thread para receber/ler comandos no stdin
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, stdin_thread, NULL) != 0) {
        perror("Erro ao criar a thread para ler comandos no stdin");
        close(fd);
        unlink(PIPE_NAME);
        exit(EXIT_FAILURE);
    }

    pthread_mutex_init(&mutex, NULL);
    pthread_t manager_thread;
    if (pthread_create(&manager_thread, NULL, expiration_manager, NULL) != 0) {
        perror("Failed to create expiration manager thread");
        close(fd);
        unlink(PIPE_NAME);
        return EXIT_FAILURE;
    }

    request req;
    response response;

    while (running) {
        nbytes = read(fd, &req, sizeof(req));
        if (nbytes == -1) {
            if (errno == EINTR) {
                continue; // Signal interruption, keep looping
            } else if (errno == EAGAIN) {
                continue; // Non-blocking read, no data yet, continue looping
            } else {
                perror("ocorreu um erro na leitura do named pipe");
                close(fd);
                unlink(PIPE_NAME);
                exit(EXIT_FAILURE);
            }
        }

        switch (req.type) {
            case LOGIN:
                remove_newline(req.request_payload.string);
                printf("%s\n", req.request_payload.string);

                int duplicate_found = 0;

                for (int i = 0; i < MAX_USERS; ++i) {
                    if (strcmp(req.request_payload.string, users[i].name) == 0) {
                        duplicate_found = 1;
                        break;
                    }
                }

                if (duplicate_found) {
                    response.n_messages = 1;
                    strcpy(response.response_payload.topics[0], "Nome de utilizador ja existe");

                    sendMessage(req.pid, response);

                    if (sigqueue(req.pid, SIGTERM, value) == -1) {
                        // não usar kill
                        perror("erro ao enviar SIGTERM");
                    } else {
                        printf("Processo %d notificado para terminar devido a username duplicado\n", req.pid);
                    }
                } else {
                    response.n_messages = 1;
                    response.type = STATUS;
                    strcpy(response.response_payload.status, "Login bem sucedido");

                    users[countUsers].pid = req.pid;
                    strcpy(users[countUsers].name, req.request_payload.string);
                    ++countUsers;

                    sendMessage(req.pid, response);
                }
                break;
            case TOPICS:

                if (topic_count == 0) {
                    response.type = STATUS;
                    response.n_messages = 1;
                    strcpy(response.response_payload.status, "Não existem topicos");
                } else {
                    response.type = TOPICS;
                    response.n_messages = topic_count;

                    for (int i = 0; i < topic_count; ++i) {
                        char buffer[MSG_LENGTH];

                        if(topics[i].blocked == 0)
                            sprintf(buffer, "%s\nNumero de mensagens persistentes: %d\nEstado: Desbloqueado",topics[i].name, topics[i].msg_count);
                        else
                            sprintf(buffer, "%s\nNumero de mensagens persistentes: %d\nEstado: Blooqueado", topics[i].name, topics[i].msg_count);

                        strcpy(response.response_payload.topics[i], buffer);
                        printf("%s\n", response.response_payload.topics[i]);
                    }

                }

                sendMessage(req.pid, response);

                break;
            case MSG:
                pthread_mutex_lock(&mutex);
                int duration = req.request_payload.msg.duration;
                int i = check_topic(req.request_payload.msg.topic);
                int j;

                for (j = 0; j < topics[i].subscriber_count && topics->subscribers[j] != req.pid; ++j);

                if (i != -1 && topics[i].blocked == 0) {
                    if(j==topics[i].subscriber_count) {
                        response.type = STATUS;
                        strcpy(response.response_payload.status, "Necessario subscricao antes de enviar mensagem\n");
                        sendMessage(req.pid, response);
                    }

                    if (duration > 0) {
                        topics[i].msg[topics[i].msg_count++] = req.request_payload.msg;
                        printf("MESSAGA COUNT: %d\n", topics[i].msg_count);
                    }
                    response.n_messages = 1;
                    response.type = MESSAGES;
                    strcpy(response.response_payload.msg->topics, topics[i].name);
                    strcpy(response.response_payload.msg->msg, req.request_payload.msg.msg);
                    printf("Topic: %s\n", response.response_payload.msg->topics);
                    printf("Recebi esta mensaggem: %s\n", response.response_payload.msg->msg);
                    for (int j = 0; j < topics[i].subscriber_count; ++j) {
                        if (topics->subscribers[j] != req.pid) {
                            strcpy(response.response_payload.msg->username, getUsername(topics[i].subscribers[j]));
                            printf("Vou mandar para: %d\n", topics->subscribers[j]);
                            sendMessage(topics->subscribers[j], response);
                        }
                    }
                } else if (i != -1 && topics[i].blocked == 1) {
                    if(j==topics[i].subscriber_count) {
                        response.type = STATUS;
                        strcpy(response.response_payload.status, "Necessario subscricao antes de enviar mensagem\n");
                        sendMessage(req.pid, response);
                    }
                    response.type = STATUS;
                    strcpy(response.response_payload.status, "Topico bloqueado. Nao foi possivel enivar mensagem\n");
                    sendMessage(req.pid, response);
                } else {
                    if (topic_count < MAX_TOPICS) {
                        int topic_index = create_topic((req.request_payload.msg.topic));
                        topics[topic_index].subscribers[topics[topic_index].subscriber_count++] = req.pid;
                        topics[topic_index].msg[0] = req.request_payload.msg;
                        topics[topic_index].msg_count = 1;
                        response.type = MSG;
                        response.n_messages = 1;
                        strcpy(response.response_payload.msg[0].msg, "Topico criado com sucesso");
                        strcpy(response.response_payload.msg[0].username, "aceite");
                        strcpy(response.response_payload.msg[0].topics, req.request_payload.msg.topic);
                        sendMessage(req.pid, response);
                    } else {
                        response.type = STATUS;
                        strcpy(response.response_payload.status, "Foi atingido o limite maximo de topicos\n");
                        sendMessage(req.pid, response);
                    }
                }

                pthread_mutex_unlock(&mutex);
                break;
            case SUBSCRIBE:
                char helper[100];
                char *sub_temp_buffer = subscribe_topic(req.request_payload.string, req.pid);
                strcpy(helper, sub_temp_buffer);
                int index = check_topic(req.request_payload.string);

                if ((strcmp(helper, "Subscricao bem sucedida") != 0) || index == -1 || topics[index].msg_count == 0) {
                    response.type = STATUS;
                    strcpy(response.response_payload.status, helper);
                    printf("STATUS\n");
                } else {
                    response.type = MSG;
                    printf("MSG\n");
                    strcpy(response.response_payload.msg[0].msg, helper);
                    strcpy(response.response_payload.msg[0].username, "aceite");
                    strcpy(response.response_payload.msg[0].topics, req.request_payload.msg.topic);

                    for (int i = 1; i <= topics[index].msg_count; ++i) {
                        strcpy(response.response_payload.msg[i].username, topics[index].msg[i - 1].user.name);
                        strcpy(response.response_payload.msg[i].topics, topics[index].name);
                        strcpy(response.response_payload.msg[i].msg, topics[index].msg[i - 1].msg);
                    }
                    response.n_messages = topics[index].msg_count + 1;
                }

                sendMessage(req.pid, response);
                free(sub_temp_buffer);
                break;
            case UNSUBSCRIBE:
                response.type = STATUS;
                response.n_messages = 1;
                char *unsub_temp_buffer = unsubscribe_topic(req.request_payload.string, req.pid);
                strcpy(response.response_payload.status, unsub_temp_buffer);
                sendMessage(req.pid, response);
                free(unsub_temp_buffer);
                break;
            case EXIT:
                printf("O user com o pid %d encerrou sessao\n", req.pid);
                for (int i = 0; i < topic_count; ++i) {
                    for (int j = 0; j < topics[i].subscriber_count; ++j) {
                        if (topics[i].subscribers[j] == req.pid) {
                            topics[i].subscribers[j] = topics[i].subscribers[topics[i].subscriber_count - 1];
                            topics[i].subscriber_count--;
                            break;
                        }
                    }
                }
                break;
        }
    }

    shutdown_expiration_manager = 1;
    pthread_join(thread_id, NULL);
    pthread_join(manager_thread, NULL);

    for (int i = 0; i < countUsers; i++) {
        sigqueue(users[i].pid, SIGUSR1, value);
    }

    save_persistent_messages(filename);
    close(fd);
    unlink(PIPE_NAME);
    return 0;
}
