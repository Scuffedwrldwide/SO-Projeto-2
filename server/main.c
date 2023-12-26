#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include "session.h"

#define MAX_BUFFER_SIZE 81 // theoretically largest command is 80 chars + 1 for opcode
int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  char* endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);
    
    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }
    state_access_delay_us = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  //TODO: Intialize server, create worker threads
  mkfifo(argv[1], 0666);
  // Create array of pointers to sessions
  struct Session** sessions = malloc(sizeof(void *) * 1); // CHANGE THIS TO MAX_SESSIONS
  if (!sessions) {
    fprintf(stderr, "Failed to allocate memory for sessions\n");
    return 1;
  }

  while (1) {
    //TODO: Read from pipe
    int register_fd = open(argv[1], O_RDONLY);
    char buffer[MAX_BUFFER_SIZE];
    printf("Waiting for connection request\n");
    ssize_t bytesRead = read(register_fd, buffer, MAX_BUFFER_SIZE);
    // Checking for connection request OPCODE
    if (bytesRead > 0 && strcmp(buffer, "1") == 0) {
      printf("Connection request received\n");
      printf("Client pipe path: %s\n", buffer + 1);
      printf("Server pipe path: %s\n", buffer + 41);
      close(register_fd);
      continue;
    }
    //TODO: Write new client to the producer-consumer buffer

  }

  //TODO: Close Server
  unlink(argv[1]);

  ems_terminate();
}