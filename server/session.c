#include "session.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

Session* create_session(unsigned int session_id, char* requests, char* responses) {
  Session* session = (Session*)malloc(sizeof(Session));
  if (!session) return NULL;
  session->requests = malloc(40 * sizeof(char) + 1);
  session->responses = malloc(40 * sizeof(char) + 1);
  if (!session->requests || !session->responses) {  // FIXME if alloc fails, might be freeing NULL
    free(session->requests);
    free(session->responses);
    free(session);
    return NULL;
  }
  strcpy(session->requests, requests);
  strcpy(session->responses, responses);
  session->id = session_id;
  return session;
}

void destroy_session(Session* session) {
  if (!session) return;

  if (session->requests) {
    free(session->requests);
  }
  if (session->responses) {
    free(session->responses);
  }
  if (session) {
    free(session);
  }
}

SessionQueue* create_session_queue() {
  SessionQueue* queue = (SessionQueue*)malloc(sizeof(SessionQueue));
  if (!queue) return NULL;
  queue->size = 0;
  queue->front = 0;
  queue->rear = -1;
  queue->shutdown = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    queue->sessions[i] = NULL;
  }
  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->full, NULL);
  pthread_cond_init(&queue->empty, NULL);
  return queue;
}

void destroy_session_queue(SessionQueue* queue) {
  if (!queue) return;
  pthread_mutex_destroy(&queue->mutex);
  pthread_cond_destroy(&queue->full);
  pthread_cond_destroy(&queue->empty);
  free(queue);
}

int enqueue_session(SessionQueue* queue, Session* session) {
  if (!queue || !session) return 1;
  pthread_mutex_lock(&queue->mutex);
  while (queue->size >= MAX_SESSIONS) {
    pthread_cond_wait(&queue->full, &queue->mutex);
    if (queue->shutdown) {
      pthread_mutex_unlock(&queue->mutex);
      return 1;
    }
  }
  queue->rear = (queue->rear + 1) % MAX_SESSIONS;
  queue->sessions[queue->rear] = session;
  queue->size++;
  pthread_cond_signal(&queue->empty);
  pthread_mutex_unlock(&queue->mutex);
  return 0;
}

Session* dequeue_session(SessionQueue* queue) {
  if (!queue) return NULL;
  pthread_mutex_lock(&queue->mutex);
  while (queue->size <= 0) {
    pthread_cond_wait(&queue->empty, &queue->mutex);
    // Threads might be waiting on empty when shutdown is called
    if (queue->shutdown) {
      pthread_mutex_unlock(&queue->mutex);
      return NULL;
    }
  }
  Session* session = queue->sessions[queue->front];
  queue->front = (queue->front + 1) % MAX_SESSIONS;
  queue->size--;
  pthread_cond_signal(&queue->full);
  pthread_mutex_unlock(&queue->mutex);

  return session;
}