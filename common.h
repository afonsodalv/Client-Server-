//
// Created by francisco on 11/23/24.
//

#ifndef COMMON_H
#define COMMON_H

#include<stdio.h>
#include<stdlib.h>
#include<signal.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<pthread.h>
#include<time.h>

#define USERNAME_SIZE 30
#define MAX_USERS     10
#define MAX_TOPICS 20
#define TOPIC_LENGTH 20
#define MAX_MSG 5
#define MSG_LENGTH 300
#define MAX_SUBSCRIBERS 10
#define PIPE_NAME     "users"
#define PIPE_PRODUTOR "pipe_%d"

typedef struct user {
    pid_t pid;
    char name[USERNAME_SIZE];
} user;

 typedef enum{
 	STATUS,
 	LIST_TOPICS,
 	MESSAGES
}response_type;

typedef struct user_message{
		int duration;
		char topic[TOPIC_LENGTH];
		char msg[MSG_LENGTH];
		user user;
} user_message;

typedef struct server_message{
	char topics[TOPIC_LENGTH];
	char username[TOPIC_LENGTH];
	char msg[MSG_LENGTH];
}server_message;

typedef struct {
	int n_messages;
	response_type type;
	union response_payload {
		char status[MSG_LENGTH];
		char topics[MAX_TOPICS][MSG_LENGTH];
		server_message msg[MAX_MSG];
	} response_payload;
} response;

typedef struct {
	char name[20];
	int blocked;
	pid_t subscribers[MAX_SUBSCRIBERS];
	int subscriber_count;
	int msg_count;
	user_message msg[MAX_MSG];
} Topic;

typedef enum {
	LOGIN,
	TOPICS,
	MSG,
	SUBSCRIBE,
	UNSUBSCRIBE,
	EXIT
} request_type;

typedef struct request{
	pid_t pid;
	request_type type;
	union request_payload{
		char string[TOPIC_LENGTH];
		user_message msg;
	}request_payload;
}request;

#endif //COMMON_H
