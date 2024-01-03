#include "api.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "common/io.h"

#define MAX_BUFFER_SIZE 40   // theoretically largest command is 80 chars + 4 for opcode

int req_fd = -1;
int resp_fd = -1;
char *req_pipe;
char *resp_pipe;
unsigned int id;

enum OPCODES {
  SETUP = 1,
  QUIT = 2,
  CREATE = 3,
  RESERVE = 4,
  SHOW = 5,
  LIST = 6,
};

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  //TODO: create request and response pipes
  int tx = open(server_pipe_path, O_WRONLY);
  if (tx == -1) {
    fprintf(stderr, "Failed to open server pipe\n");
    return 1;
  }
  
  printf("sending setup request %s %s\n", req_pipe_path, resp_pipe_path);
  int code = 1;
  if (write(tx, &code, sizeof(int)) != sizeof(int)) {
    fprintf(stderr, "Failed to send setup request\n");
    return 1;
  }
  char buffer[MAX_BUFFER_SIZE] = {0};
  printf("sending request pipe path\n");
  strcpy(buffer, req_pipe_path);
  if (write(tx, buffer, MAX_BUFFER_SIZE) == -1) {
    fprintf(stderr, "Failed to send request pipe path\n");
    return 1;
  }

  memset(buffer, 0, MAX_BUFFER_SIZE);
  printf("sending response pipe path\n");
  strcpy(buffer, resp_pipe_path);
  if (write(tx, buffer, MAX_BUFFER_SIZE) == -1) {
    fprintf(stderr, "Failed to send response pipe path\n");
    return 1;
  }

  close(tx);
  printf("closed server pipe\n");
  mkfifo(req_pipe_path, 0640);
  mkfifo(resp_pipe_path, 0640);
  if (req_fd != -1 || resp_fd != -1) {
    fprintf(stderr, "Request or response pipe already open\n");
    return 1;
  }
  req_fd = open(req_pipe_path, O_WRONLY);
  resp_fd = open(resp_pipe_path, O_RDONLY);
  if (req_fd == -1 || resp_fd == -1) {
    fprintf(stderr, "Failed to open request or response pipe\n");
    return 1;
  }
  printf("opened request and response pipes\n");
  
  req_pipe = malloc(strlen(req_pipe_path) + 1);
  resp_pipe = malloc(strlen(resp_pipe_path) + 1);
  strcpy(req_pipe, req_pipe_path);
  strcpy(resp_pipe, resp_pipe_path);
  read(resp_fd, &id, sizeof(unsigned int));
  return 0;
}

int ems_quit(void) { 
  printf("sending quit request\n");
  int code = QUIT;
  write(req_fd, &code, sizeof(int));
  close(req_fd);
  close(resp_fd);
  unlink(req_pipe);
  unlink(resp_pipe);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  printf("sending create request\n");
  int code = CREATE;
  write(req_fd, &code, sizeof(int));
  write(req_fd, &event_id, sizeof(unsigned int));
  write(req_fd, &num_rows, sizeof(size_t));
  write(req_fd, &num_cols, sizeof(size_t));
  read(resp_fd, &code, sizeof(int));
  if (code == 0) {
    printf("create successful\n");
    return 0;
  } else {
    printf("create failed\n");
    return 1;
  }
  return 1;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  printf("sending reserve request\n");
  int code = RESERVE;
  write(req_fd, &code, sizeof(int));
  write(req_fd, &event_id, sizeof(unsigned int));
  write(req_fd, &num_seats, sizeof(size_t));
  write(req_fd, xs, sizeof(size_t) * num_seats);
  write(req_fd, ys, sizeof(size_t) * num_seats);
  //Checks for response
  read(resp_fd, &code, sizeof(int));
  if (code == 0) {
    printf("reserve successful\n");
    return 0;
  } else {
    printf("reserve failed\n");
    return 1;
  }
  return 1;
}

int ems_show(int out_fd, unsigned int event_id) {
  printf("sending show request\n");
  int code = SHOW;
  size_t num_rows, num_cols;
  unsigned int* seats;
  write(req_fd, &code, sizeof(int));
  write(req_fd, &event_id, sizeof(unsigned int));
  read(resp_fd, &code, sizeof(int));
  if (code == 0) {
    if(read(resp_fd, &num_rows, sizeof(size_t)) ||
    read(resp_fd, &num_cols, sizeof(size_t))) {
      perror("Error reading dimensions from the response pipe");
      return 1;
    }
    seats = malloc(sizeof(unsigned int) * num_rows * num_cols);
    if (seats == NULL) {
      perror("Memory allocation error");
      return 1;
    }

    //Safe read of seating data from pipe
    size_t total_bytes = sizeof(unsigned int) * num_rows * num_cols;
    size_t bytes_read = 0;
    while (bytes_read < total_bytes) {
      ssize_t result = read(resp_fd, seats + bytes_read / sizeof(unsigned int), total_bytes - bytes_read);
      if (result < 0) {
        perror("Error reading seating arrangement from the response pipe");
        free(seats);
        return 1;
      } else if (result == 0) {
        fprintf(stderr, "Error: Unexpected end of file while reading seating arrangement\n");
        free(seats);
        return 1;
      } else {
        bytes_read += (size_t)result;
      }
    }
  
    for (size_t i = 1; i <= num_rows; i++) {
      for (size_t j = 1; j <= num_cols; j++) {
        char buffer[16];
        sprintf(buffer, "%u", seats[(i - 1) * num_cols + (j - 1)]);

        if (print_str(out_fd, buffer)) {
          perror("Error writing to file descriptor");
          free(seats);
          return 1;
        }

        if (j < num_cols) {
          if (print_str(out_fd, " ")) {
            perror("Error writing to file descriptor");
            free(seats);
            return 1;
          }
        }
      }

      if (print_str(out_fd, "\n")) {
        perror("Error writing to file descriptor");
        free(seats);
        return 1;
      }
    }
    free(seats);
    return 0;
  } 
  else {
    printf("show failed\n");
    return 1;
  }
  return 1;
}

int ems_list_events(int out_fd) {
  printf("sending list request\n");
  int code = LIST;
  write(req_fd, &code, sizeof(int));
  read(resp_fd, &code, sizeof(int));
  if (code == 0) {
    printf("list successful\n");
    size_t num_events = 0;
    unsigned int* event_ids = NULL;
    read(resp_fd, &num_events, sizeof(size_t));
    event_ids = malloc(sizeof(unsigned int) * num_events);
    if (event_ids == NULL) {
      fprintf(stderr, "Failed to allocate memory for event ids\n");
      return 1;
    }
    if (num_events == 0) {
      char buff[] = "No events\n";
      if (print_str(out_fd, buff)) {
        perror("Error writing no events to file descriptor\n");
        free(event_ids);
        return 1;
      }
      return 0;
    }
    printf("num_events: %lu\n", num_events);
    if (read(resp_fd, event_ids, sizeof(unsigned int) * num_events) != (ssize_t)(sizeof(unsigned int) * num_events)) {
      fprintf(stderr, "Failed to read event ids\n");
      free(event_ids);
      return 1;
    }
    for (size_t i = 0; i < num_events; i++) {
      char buff[] = "Event: ";
      if (print_str(out_fd, buff)) {
        perror("Error writing event literal to file descriptor\n");
        return 1;
      }


      if (print_uint(out_fd, event_ids[i])) {
        perror("Error writing event id to file descriptor\n");
        return 1;
      }
      write(out_fd, "\n", 1);

    }
    free(event_ids);
    return 0;
  } else {
    printf("list failed\n");
    return 1;
  }
  return 1;
  /* OLD CODE FROM SERVER
  if (current == NULL) {
    char buff[] = "No events\n";
    if (print_str(out_fd, buff)) {
      perror("Error writing to file descriptor");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }

    pthread_rwlock_unlock(&event_list->rwl);
    return 0;
  }

  while (1) {
    char buff[] = "Event: ";
    if (print_str(out_fd, buff)) {
      perror("Error writing to file descriptor");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }

    char id[16];
    sprintf(id, "%u\n", (current->event)->id);
    if (print_str(out_fd, id)) {
      perror("Error writing to file descriptor");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }

    if (current == to) {
      break;
    }

    current = current->next;
  }
  */
}
