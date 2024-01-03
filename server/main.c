#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include "session.h"

#define MAX_BUFFER_SIZE 40   // theoretically largest command is 80 chars + 4 for opcode
int session_worker(Session* session);
void list_all_info();
SessionQueue* queue = NULL;

unsigned int active_sessions = 0;
volatile sig_atomic_t server_running = 1;
volatile sig_atomic_t list_all = 0; // Flag to trigger list all events

// Handler for SIGINT
void sigint_handler(int sign) {
  server_running = 0;
  pthread_mutex_lock(&queue->mutex);
  queue->shutdown = 1;
  pthread_mutex_unlock(&queue->mutex);
  pthread_cond_broadcast(&queue->empty);
  fprintf(stderr, "Received SIGINT. Terminating...\n");
}

void sigusr1_handler(int sign) {
  printf("Received SIGUSR1. Listing all events...\n");
  list_all = 1;
}

void *session_thread() {
  //SessionQueue* queue = (SessionQueue*)arg;
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL);
  while (server_running) {
    Session* session = dequeue_session(queue);
    if (!session) {
      fprintf(stderr, "Failed to dequeue session\n");
      break;
    }
    if (session_worker(session) != 0) {
      fprintf(stderr, "Session Error\n");
      destroy_session(session);
      active_sessions--;
      break;
    }
    fprintf(stderr, "Session %d terminated\n", session->id);
    destroy_session(session);
    active_sessions--;
  }
  printf("Thread terminating\n");
  return NULL;
}

int main(int argc, char* argv[]) {
  printf("Server started with PID %d\n", getpid());
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
  mkfifo(argv[1], 0666); // Create named pipe for conection requests
  if (errno == EEXIST) {
    fprintf(stderr, "Named pipe already exists.\n");  
  } 
  else if (errno != 0) {
    perror("Error creating named pipe");
    return 1;
  }
  // Create array of pointers to sessions
  pthread_t worker_threads[MAX_SESSIONS];
  queue = create_session_queue();
  if (!queue) {
    fprintf(stderr, "Failed to create session queue\n");
    return 1;
  }
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (pthread_create(&worker_threads[i], NULL, session_thread, NULL) != 0) {
      fprintf(stderr, "Failed to create worker thread\n");
      return 1;
    }
  }

  signal(SIGINT, sigint_handler); 
  signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE (good idea???)
  signal(SIGUSR1, sigusr1_handler);

  int register_fd;
  while (server_running) {
    if (list_all) list_all_info();

    while (server_running) {
      register_fd = open(argv[1], O_RDONLY);
      if (register_fd == -1) {
        if (errno == EINTR) {
          printf("Interrupted by signal\n");
          if (list_all) list_all_info();
          continue; // Retry or terminate via signal
        }
        fprintf(stderr, "Failed to open named pipe\n");
        return 1;
      }
      break;
    }
    int code;
    char req_pipe_path[MAX_BUFFER_SIZE];
    char resp_pipe_path[MAX_BUFFER_SIZE];

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
    printf("Connection request received\n");
    bytesRead = read(register_fd, req_pipe_path, MAX_BUFFER_SIZE);
    if (bytesRead == -1) {
      fprintf(stderr, "Failed to read request pipe path\n");
      break;
    }
    printf("Request pipe path received\n");
    printf("Request pipe path: %s\n", req_pipe_path);
    bytesRead = read(register_fd, resp_pipe_path, MAX_BUFFER_SIZE);
    if (bytesRead == -1) { 
      fprintf(stderr, "Failed to read response pipe path\n");
      break;
    }
    printf("Response pipe path received\n");
    printf("Response pipe path: %s\n", resp_pipe_path);

    Session *session = create_session(active_sessions, req_pipe_path, resp_pipe_path);
    active_sessions++;
    if (!session) {
      fprintf(stderr, "Failed to create session\n");
      break;
    }
    printf("Session created\n");
    if (enqueue_session(queue, session) != 0) {
      fprintf(stderr, "Failed to enqueue session\n");
      destroy_session(session);
      break;
    }
    close(register_fd);
  }
  close(register_fd);
  printf("Server terminating\n");
  //TODO: Close Server
  for (int i = 0; i < MAX_SESSIONS; i++) {
    printf("Joining thread %d\n", i);
    pthread_join(worker_threads[i], NULL);
  }
  destroy_session_queue(queue);
  unlink(argv[1]);
  ems_terminate();
  return 0;
}

void list_all_info() {
  printf("Listing all events...\n");
  size_t num_events, num_rows, num_cols;
  unsigned int *event_ids, *data;
  int ret_val = ems_list_events(&num_events, &event_ids);
  if (ret_val == 0 && num_events > 0) {
    printf("Displaying all event information...\n");
    printf("Number of events: %zu\n", num_events);
    for (size_t i = 0; i < num_events; i++) {
      printf("Event ID: %u\n", event_ids[i]);
      ems_show(event_ids[i], &num_rows, &num_cols, &data);
      for (size_t j = 0; j < num_rows; j++) {
        for (size_t k = 0; k < num_cols; k++) {
          printf("%d", data[(j) * num_cols + (k)]);
          if(k < num_cols - 1) printf(" ");
        }
        printf("\n");
      }
      printf("\n");
    }
    free(event_ids);
  }
  signal(SIGUSR1, sigusr1_handler);
  list_all = 0;
}

int session_worker(Session* session) {

  while (server_running) {
        // Check if both named pipes exist
        if (access(session->requests, F_OK) == 0 && access(session->responses, F_OK) == 0) {
            printf("Both named pipes exist. Server is ready.\n");
            break;  
        } else {
            printf("One or both named pipes do not exist. Server is not ready.\n");
            sleep(1);
        }
    }
  printf("thread %d is running\n", session->id);
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
        unsigned int event_id;
        size_t num_rows, num_columns;
        int ret_val;
        if (read(requests, &event_id, sizeof(unsigned int)) != sizeof(unsigned int)) {
          fprintf(stderr, "Failed to read event id\n");
          return 1;
        }
        if (read(requests, &num_rows, sizeof(size_t)) != sizeof(size_t)) {
          fprintf(stderr, "Failed to read num rows\n");
          return 1;
        }
        if (read(requests, &num_columns, sizeof(size_t)) != sizeof(size_t)) {
          fprintf(stderr, "Failed to read num columns\n");
          return 1;
        }
        ret_val = ems_create(event_id, num_rows, num_columns);
        if (write(responses, &ret_val, sizeof(int)) != sizeof(int)) {
          fprintf(stderr, "Failed to write response\n");
          return 1;
        }
        break;
      }
      case 4: {
        printf("Reserve request received\n");
        unsigned int event_id;
        size_t num_seats;
        size_t* xs;
        size_t* ys;
        int ret_val;
        if (read(requests, &event_id, sizeof(unsigned int)) != sizeof(unsigned int)) {
          fprintf(stderr, "Failed to read event id\n");
          return 1;
        }
        if (read(requests, &num_seats, sizeof(size_t)) != sizeof(size_t)) {
          fprintf(stderr, "Failed to read num seats\n");
          return 1;
        }
        xs = malloc(sizeof(size_t) * num_seats);
        if (!xs) {
          fprintf(stderr, "Failed to allocate memory for xs\n");
          return 1;
        }
        ys = malloc(sizeof(size_t) * num_seats);
        if (!ys) {
          fprintf(stderr, "Failed to allocate memory for ys\n");
          return 1;
        }
        if (read(requests, xs, sizeof(size_t) * num_seats) != (ssize_t)sizeof(size_t) * num_seats) {
          fprintf(stderr, "Failed to read xs\n");
          free(xs);
          free(ys);
          return 1;
        }
        if (read(requests, ys, sizeof(size_t) * num_seats) != (ssize_t)sizeof(size_t) * num_seats) {
          fprintf(stderr, "Failed to read ys\n");
          free(xs);
          free(ys);
          return 1;
        }
        ret_val = ems_reserve(event_id, num_seats, xs, ys);
        if (write(responses, &ret_val, sizeof(int)) != sizeof(int)) {
          fprintf(stderr, "Failed to write response\n");
          free(xs);
          free(ys);
          return 1;
        }
        free(xs);
        free(ys);
        break;
      }
      case 5: {
        printf("Show request received\n");
        unsigned int event_id;
        int ret_val;
        size_t num_rows, num_columns;
        unsigned int* seats;
        if (read(requests, &event_id, sizeof(unsigned int)) != sizeof(unsigned int)) {
          fprintf(stderr, "Failed to read event id\n");
          return 1;
        }
        ret_val = ems_show(event_id, &num_rows, &num_columns, &seats);
        write(responses, &ret_val, sizeof(int));
        if (ret_val == 0) {
          if (write(responses, &num_rows, sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Failed to write num rows\n");
            return 1;
          }
          if (write(responses, &num_columns, sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Failed to write num columns\n");
            return 1;
          }
          if (write(responses, seats, sizeof(unsigned int) * num_rows * num_columns) != sizeof(unsigned int) * num_rows * num_columns) {
            fprintf(stderr, "Failed to write seats\n");
            return 1;
          }
        }
        break;
      }
      case 6: {
        printf("List request received\n");
        int ret_val;
        size_t num_events;
        unsigned int* event_ids;
        ret_val = ems_list_events(&num_events, &event_ids); //This fucntion allocates memory for event_ids
        write(responses, &ret_val, sizeof(int));
        if (ret_val == 0) { //If it returns 1 or num_events == 0, then there was no allocation
          if (write(responses, &num_events, sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Failed to write num events\n");
            if (event_ids) { free(event_ids);
              }
            return 1;
          }
          if (write(responses, event_ids, sizeof(unsigned int) * num_events) != sizeof(unsigned int) * num_events) {
            fprintf(stderr, "Failed to write event ids\n");
            if (event_ids) { free(event_ids);
              }
            return 1;
          }
          if (event_ids) { free(event_ids);
            }
        }
        break;
      }
    }
  }
  return 0;
}