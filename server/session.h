#ifndef SERVER_SESSION_H
#define SERVER_SESSION_H

#include <stddef.h>

struct Session {
  unsigned int id;
  char* requests;
  char* responses;

};

struct Session* create_session(unsigned int session_id, char* requests, char* responses);
void destroy_session(struct Session* session);

#endif // SERVER_SESSION_H