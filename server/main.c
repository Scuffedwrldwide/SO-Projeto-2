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

#define MAX_BUFFER_SIZE 40   // theoretically largest command is 80 chars + 4 for opcode
int session_worker(struct Session* session);

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
  unsigned int client_count = 0;

  while (1) {
    //TODO: Read from pipe
    int register_fd = open(argv[1], O_RDONLY);
    int code;
    char req_pipe_path[MAX_BUFFER_SIZE];
    char resp_pipe_path[MAX_BUFFER_SIZE];
    int test = open("test", O_WRONLY);

    printf("Waiting for connection request\n");
    ssize_t bytesRead = read(register_fd, &code, sizeof(int));
    // Checking for connection request OPCODE
    if (bytesRead == -1) {
      fprintf(stderr, "Failed to read connection request\n");
      break;
    }
    printf("Connection request received with code %d\n", code);
    if (code != 1) {
      fprintf(stderr, "Invalid connection request\n");
      break;
    }
    write(test, &code, sizeof(int));
    printf("Connection request received\n");
    bytesRead = read(register_fd, req_pipe_path, MAX_BUFFER_SIZE);
    if (bytesRead == -1) {
      fprintf(stderr, "Failed to read request pipe path\n");
      break;
    }
    write(test, req_pipe_path, MAX_BUFFER_SIZE);
    printf("Request pipe path received\n");
    printf("Request pipe path: %s\n", req_pipe_path);
    bytesRead = read(register_fd, resp_pipe_path, MAX_BUFFER_SIZE);
    if (bytesRead == -1) { 
      fprintf(stderr, "Failed to read response pipe path\n");
      break;
    }
    write(test, resp_pipe_path, MAX_BUFFER_SIZE);
    printf("Response pipe path received\n");
    printf("Response pipe path: %s\n", resp_pipe_path);
    close(test);

    struct Session *session = create_session(client_count, req_pipe_path, resp_pipe_path);
    if (!session) {
      fprintf(stderr, "Failed to create session\n");
      break;
    }
    printf("Session created\n");
    sessions[client_count++] = session;
    if (session_worker(session) != 0) {
      fprintf(stderr, "Session Error\n");
      break;
    }
    close(register_fd);
    printf("Session ended\n");
    //TODO: Write new client to the producer-consumer buffer
    
  }

  //TODO: Close Server
  unlink(argv[1]);

  ems_terminate();
}

int session_worker(struct Session* session) {
  while (1) {
        // Check if both named pipes exist
        if (access(session->requests, F_OK) == 0 && access(session->responses, F_OK) == 0) {
            printf("Both named pipes exist. Server is ready.\n");
            break;  // Exit the loop if both pipes are found
        } else {
            printf("One or both named pipes do not exist. Server is not ready.\n");
            // Sleep for a short time and retry
            sleep(1);
        }
    }

  int requests = open(session->requests, O_RDONLY);
  if (requests == -1) {
    fprintf(stderr, "Failed to open request pipe\n");
    return 1;
  }
  int responses = open(session->responses, O_WRONLY);
  if (responses == -1) {
    fprintf(stderr, "Failed to open response pipe\n");
    close(requests);
    return 1;
  }

  write(responses, &session->id, sizeof(unsigned int));
  while (1) {
    //char buffer[MAX_BUFFER_SIZE] = {0};
    int opcode;
    ssize_t bytesRead = read(requests, &opcode, sizeof(int));
    if (bytesRead == -1) {
      fprintf(stderr, "Failed to read opcode\n");
      return 1;
    }
    printf("Opcode received: %d\n", opcode);

    switch (opcode) {
      case 2: {
        printf("Quit request received\n");
        close(requests);
        close(responses);
        return 0;
      }
      case 3: {
        printf("Create request received\n");
        break;
      }
      case 4: {
        printf("Reserve request received\n");
        break;
      }
      case 5: {
        printf("Show request received\n");
        break;
      }
      case 6: {
        printf("List request received\n");
        break;
      }
    }
  }

}