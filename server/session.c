#include "session.h"

#include <stdlib.h>
#include <string.h>

struct Session* create_session(unsigned int session_id, char* requests, char* responses) {
  struct Session* session = (struct Session*)malloc(sizeof(struct Session));
  if (!session) return NULL;
  session->requests = malloc(40*sizeof(char) + 1);
  session->responses = malloc(40*sizeof(char) + 1);
  if (!session->requests || !session->responses) { // FIXME if alloc fails, might be freeing NULL
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

void destroy_session(struct Session* session) {
  if (!session) return;

  free(session->requests);
  free(session->responses);
  free(session);
}
