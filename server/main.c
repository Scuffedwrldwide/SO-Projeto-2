#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include "session.h"

int session_worker(Session* session);
void list_all_info();

SessionQueue* queue = NULL;
unsigned int active_sessions = 0;
volatile sig_atomic_t server_running = 1;
volatile sig_atomic_t list_all = 0;  // Flag to trigger list all events

// Handler for SIGINT
void sigint_handler(int sign) {
  if (sign != SIGINT) {
    fprintf(stderr, "Received unexpected signal %d\n", sign);
    return;
  }
  server_running = 0;
  pthread_mutex_lock(&queue->mutex);
  queue->shutdown = 1;
  pthread_mutex_unlock(&queue->mutex);
  pthread_cond_broadcast(&queue->empty);
  fprintf(stderr, "\nReceived SIGINT. Terminating...\n");
}
// Handler for SIGUSR1
void sigusr1_handler(int sign) {
  if (sign != SIGUSR1) {
    fprintf(stderr, "Received unexpected signal %d\n", sign);
    return;
  }
  printf("\nReceived SIGUSR1. Listing all events...\n");
  list_all = 1;
}

void* session_thread(void* arg) {
  (void)arg;
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL);
  while (server_running) {
    Session* session = dequeue_session(queue);
    if (!session) {
      if (server_running == 1) {
        fprintf(stderr, "Failed to dequeue session\n");
      }
      break;
    }
    if (session_worker(session) != 0) {
      fprintf(stderr, "Session Error\n");
    }
    fprintf(stderr, "Session %d terminated.\n", session->id);
    destroy_session(session);
    active_sessions--;
  }
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

  mkfifo(argv[1], 0640);  // Create named pipe for connection requests
  if (errno == EEXIST) {
    fprintf(stderr, "Named pipe already exists.\n");
  } else if (errno != 0) {
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
    int create = pthread_create(&worker_threads[i], NULL, session_thread, NULL);
    if (create != 0) {
      fprintf(stderr, "Failed to create worker thread\n");
      return 1;
    }
  }

  signal(SIGINT, sigint_handler);
  signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE
  signal(SIGUSR1, sigusr1_handler);

  int register_fd;
  while (server_running) {
    if (list_all) list_all_info();

    while (server_running) {
      register_fd = open(argv[1], O_RDWR);
      if (register_fd == -1) {
        if (errno == EINTR) {
          printf("Interrupted by signal\n");
          if (list_all) list_all_info();
          continue;  // Retry or terminate via signal
        }
        fprintf(stderr, "Failed to open named pipe\n");
        return 1;
      }
      break;
    }
    // Process connection request
    int code = 0;
    char req_pipe_path[MAX_BUFFER_SIZE] = {0};
    char resp_pipe_path[MAX_BUFFER_SIZE] = {0};
    printf("Waiting for connection request...\n");
    ssize_t bytesRead = read(register_fd, &code, sizeof(int));
    // Checking for connection request OPCODE
    if (bytesRead == 0) {
      // Read from pipe a second time before closure, so nothing to do but wait
      continue;
    }
    if (bytesRead == -1) {
      if (errno == EINTR) {  // since reading call is blocking, USR1 might interrupt
        continue;
      }
      fprintf(stderr, "Failed to read connection request\n");
      break;
    }
    printf("Connection request received with code %d. %d connections already active\n", code, active_sessions);
    if (code != 1) {
      fprintf(stderr, "Invalid connection request\n");
      continue;
    }
    bytesRead = read(register_fd, req_pipe_path, MAX_BUFFER_SIZE);
    if (bytesRead == -1) {
      fprintf(stderr, "Failed to read request pipe path\n");
      break;
    }
    printf("Request pipe path: %s\n", req_pipe_path);
    bytesRead = read(register_fd, resp_pipe_path, MAX_BUFFER_SIZE);
    if (bytesRead == -1) {
      fprintf(stderr, "Failed to read response pipe path\n");
      break;
    }
    printf("Response pipe path: %s\n", resp_pipe_path);
    // Creates session
    Session* session = create_session(active_sessions, req_pipe_path, resp_pipe_path);
    active_sessions++;
    if (!session) {
      fprintf(stderr, "Failed to create session\n");
      break;
    }
    printf("Session %d created", session->id);
    if (active_sessions > MAX_SESSIONS) {
      printf(". Waiting for earlier sessions to finish...");
    }
    printf("\n\n");
    // Queues session
    if (enqueue_session(queue, session) != 0) {
      fprintf(stderr, "Failed to enqueue session\n");
      destroy_session(session);
      break;
    }

    close(register_fd);
  }
  close(register_fd);
  printf("\nServer terminating.\n");

  // Wait for all worker threads to terminate
  for (int i = 0; i < MAX_SESSIONS; i++) {
    pthread_join(worker_threads[i], NULL);
  }
  destroy_session_queue(queue);
  unlink(argv[1]);
  ems_terminate();
  return 0;
}

void list_all_info() {
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
          printf("%d", data[(j)*num_cols + (k)]);
          if (k < num_cols - 1) printf(" ");
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
  int requests;
  int responses;
  responses = open(session->responses, O_WRONLY);
  if (responses == -1) {
    fprintf(stderr, "Failed to open response pipe\n");
    return 1;
  }
  write(responses, &session->id, sizeof(unsigned int));
  requests = open(session->requests, O_RDONLY);
  if (requests == -1) {
    fprintf(stderr, "Failed to open request pipe\n");
    return 1;
  }

  while (server_running) {
    // char buffer[MAX_BUFFER_SIZE] = {0};
    int opcode;
    ssize_t bytesRead = read(requests, &opcode, sizeof(int));
    if (bytesRead == -1 || opcode < 2) {
      fprintf(stderr, "Failed to read opcode (%d)\n", session->id);
      return 1;
    }

    switch (opcode) {
      case 2: {
        close(requests);
        close(responses);
        return 0;
      }
      case 3: {
        unsigned int event_id;
        size_t num_rows, num_columns;
        int ret_val;
        if (read(requests, &event_id, sizeof(unsigned int)) != sizeof(unsigned int)) {
          fprintf(stderr, "Failed to read event id (%d)\n", session->id);
          return 1;
        }
        if (read(requests, &num_rows, sizeof(size_t)) != sizeof(size_t)) {
          fprintf(stderr, "Failed to read num rows (%d)\n", session->id);
          return 1;
        }
        if (read(requests, &num_columns, sizeof(size_t)) != sizeof(size_t)) {
          fprintf(stderr, "Failed to read num columns (%d)\n", session->id);
          return 1;
        }
        ret_val = ems_create(event_id, num_rows, num_columns);
        if (write(responses, &ret_val, sizeof(int)) != sizeof(int)) {
          fprintf(stderr, "Failed to write response (%d)\n", session->id);
          return 1;
        }
        break;
      }
      case 4: {
        unsigned int event_id;
        size_t num_seats;
        size_t* xs;
        size_t* ys;
        int ret_val;
        if (read(requests, &event_id, sizeof(unsigned int)) != sizeof(unsigned int)) {
          fprintf(stderr, "Failed to read event id (%d)\n", session->id);
          close(requests);
          close(responses);
          return 1;
        }
        if (read(requests, &num_seats, sizeof(size_t)) != sizeof(size_t)) {
          fprintf(stderr, "Failed to read num seats (%d)\n", session->id);
          close(requests);
          close(responses);
          return 1;
        }
        xs = malloc(sizeof(size_t) * num_seats);
        if (!xs) {
          fprintf(stderr, "Failed to allocate memory for xs (%d)\n", session->id);
          close(requests);
          close(responses);
          return 1;
        }
        ys = malloc(sizeof(size_t) * num_seats);
        if (!ys) {
          fprintf(stderr, "Failed to allocate memory for ys (%d)\n", session->id);
          close(requests);
          close(responses);
          return 1;
        }
        if (read(requests, xs, sizeof(size_t) * num_seats) != (ssize_t)(sizeof(size_t) * num_seats)) {
          fprintf(stderr, "Failed to read xs (%d)\n", session->id);
          free(xs);
          free(ys);
          close(requests);
          close(responses);
          return 1;
        }
        if (read(requests, ys, sizeof(size_t) * num_seats) != (ssize_t)(sizeof(size_t) * num_seats)) {
          fprintf(stderr, "Failed to read ys (%d)\n", session->id);
          free(xs);
          free(ys);
          close(requests);
          close(responses);
          return 1;
        }
        ret_val = ems_reserve(event_id, num_seats, xs, ys);
        if (write(responses, &ret_val, sizeof(int)) != sizeof(int)) {
          fprintf(stderr, "Failed to write response (%d)\n", session->id);
          free(xs);
          free(ys);
          close(requests);
          close(responses);
          return 1;
        }
        free(xs);
        free(ys);
        break;
      }
      case 5: {
        unsigned int event_id;
        int ret_val;
        size_t num_rows, num_columns;
        unsigned int* seats;
        if (read(requests, &event_id, sizeof(unsigned int)) != sizeof(unsigned int)) {
          fprintf(stderr, "Failed to read event id (%d)\n", session->id);
          close(requests);
          close(responses);
          return 1;
        }
        ret_val = ems_show(event_id, &num_rows, &num_columns, &seats);
        write(responses, &ret_val, sizeof(int));
        if (ret_val == 0) {
          if (write(responses, &num_rows, sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Failed to write num rows (%d)\n", session->id);
            close(requests);
            close(responses);
            return 1;
          }
          if (write(responses, &num_columns, sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Failed to write num columns (%d)\n", session->id);
            close(requests);
            close(responses);
            return 1;
          }
          if (write(responses, seats, sizeof(unsigned int) * num_rows * num_columns) !=
              (ssize_t)(sizeof(unsigned int) * num_rows * num_columns)) {
            fprintf(stderr, "Failed to write seats (%d)\n", session->id);
            close(requests);
            close(responses);
            return 1;
          }
        }
        break;
      }
      case 6: {
        int ret_val;
        size_t num_events;
        unsigned int* event_ids;
        ret_val = ems_list_events(&num_events, &event_ids);  // This function allocates memory for event_ids
        write(responses, &ret_val, sizeof(int));
        if (ret_val == 0) {  // If it returns 1 or num_events == 0, then there was no allocation
          if (write(responses, &num_events, sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Failed to write num events (%d)\n", session->id);
            if (event_ids) {
              free(event_ids);
            }
            close(requests);
            close(responses);
            return 1;
          }
          if (write(responses, event_ids, sizeof(unsigned int) * num_events) !=
              (ssize_t)(sizeof(unsigned int) * num_events)) {
            fprintf(stderr, "Failed to write event ids (%d)\n", session->id);
            if (event_ids) {
              free(event_ids);
            }
            close(requests);
            close(responses);
            return 1;
          }
          if (event_ids) {
            free(event_ids);
          }
        }
        break;
      }
    }
  }
  // Only reachable if interrupted mid session
  if (requests != -1) close(requests);
  if (responses != -1) close(responses);
  return 0;
}