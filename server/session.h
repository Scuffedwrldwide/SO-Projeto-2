#ifndef SERVER_SESSION_H
#define SERVER_SESSION_H

#include <pthread.h>
#include <stddef.h>

#define MAX_SESSIONS 8

typedef struct {
  unsigned int id;
  char* requests;
  char* responses;

} Session;

typedef struct {
  Session* sessions[MAX_SESSIONS];
  int size;
  int front;
  int rear;
  int shutdown;
  pthread_mutex_t mutex;
  pthread_cond_t full;
  pthread_cond_t empty;
} SessionQueue;

// Creates a new session structure, representing a client/server relationship
// @param session_id Session identifier
// @param requests Name of the requests pipe
// @param responses Name of the responses pipe
// @return Pointer to the newly created session structure
// @warning The created structure should be destroyed with destroy_session()
Session* create_session(unsigned int session_id, char* requests, char* responses);

// Destroys a session structure, freeing all resources
// @param session Pointer to the session structure to be destroyed
void destroy_session(Session* session);

// Creates a new session queue, used to store sessions
// @return Pointer to the newly created session queue
// @warning The created structure should be destroyed with destroy_session_queue()
SessionQueue* create_session_queue();

// Destroys a session queue, freeing all resources
// @param queue Pointer to the session queue to be destroyed
void destroy_session_queue(SessionQueue* queue);

// Places a session into the session queue
// @param queue Pointer to the session queue
// @param session Pointer to the session to be enqueued
int enqueue_session(SessionQueue* queue, Session* session);

// Removes a session from the session queue
// @param queue Pointer to the session queue
// @return Pointer to the dequeued session
// @warning This function may fail, returning a NULL pointer
Session* dequeue_session(SessionQueue* queue);

#endif  // SERVER_SESSION_H