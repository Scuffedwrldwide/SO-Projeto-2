#ifndef SERVER_SESSION_H
#define SERVER_SESSION_H

#include <stddef.h>
#include <pthread.h>

#define MAX_SESSIONS 2

typedef struct {
  unsigned int id;
  char* requests;
  char* responses;

} Session;

typedef struct {
  Session *sessions[MAX_SESSIONS];
  int size;
  int front;
  int rear;
  int shutdown;
  pthread_mutex_t mutex;
  pthread_cond_t full;
  pthread_cond_t empty;
} SessionQueue;

Session* create_session(unsigned int session_id, char* requests, char* responses);
void destroy_session(Session* session);
SessionQueue* create_session_queue();
void destroy_session_queue(SessionQueue* queue);
int enqueue_session(SessionQueue* queue, Session* session);
Session* dequeue_session(SessionQueue* queue);

#endif // SERVER_SESSION_H