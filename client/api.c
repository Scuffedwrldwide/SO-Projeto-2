#include "api.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/constants.h"
#include "common/io.h"

int req_fd = -1;
int resp_fd = -1;
char const* req_pipe;
char const* resp_pipe;
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
  int tx = open(server_pipe_path, O_WRONLY);
  if (tx == -1) {
    fprintf(stderr, "Failed to open server pipe\n");
    return 1;
  }

  if (mkfifo(resp_pipe_path, 0640)) {
    fprintf(stderr, "Failed to create response pipes");
  }
  if (mkfifo(req_pipe_path, 0640)) {
    fprintf(stderr, "Failed to create requests pipe");
  }

  printf("Sending setup request %s %s\n", req_pipe_path, resp_pipe_path);
  void* message = malloc(sizeof(int) + MAX_BUFFER_SIZE * 2);
  if (message == NULL) {
    perror("Memory allocation error");
    return 1;
  }
  memset(message, 0, sizeof(int) + MAX_BUFFER_SIZE * 2);

  *(int*)message = SETUP;
  strcpy(message + sizeof(int), req_pipe_path);
  strcpy(message + sizeof(int) + MAX_BUFFER_SIZE, resp_pipe_path);

  write(tx, message, sizeof(int) + MAX_BUFFER_SIZE * 2);

  free(message);
  close(tx);

  resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "Failed to open response pipe\n");
    return 1;
  }

  read(resp_fd, &id, sizeof(unsigned int));

  req_fd = open(req_pipe_path, O_WRONLY);
  if (resp_fd == -1) {
    fprintf(stderr, "Failed to open response pipe\n");
    return 1;
  }

  req_pipe = req_pipe_path;
  resp_pipe = resp_pipe_path;
  printf("Got id %u\n", id);
  return 0;
}

int ems_quit(void) {
  int code = QUIT;
  write(req_fd, &code, sizeof(int));
  close(req_fd);
  close(resp_fd);
  unlink(req_pipe);
  unlink(resp_pipe);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  int code = CREATE;
  write(req_fd, &code, sizeof(int));
  write(req_fd, &event_id, sizeof(unsigned int));
  write(req_fd, &num_rows, sizeof(size_t));
  write(req_fd, &num_cols, sizeof(size_t));
  read(resp_fd, &code, sizeof(int));
  if (code == 0) {
    return 0;
  } else {
    return 1;
  }
  return 1;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  int code = RESERVE;
  write(req_fd, &code, sizeof(int));
  write(req_fd, &event_id, sizeof(unsigned int));
  write(req_fd, &num_seats, sizeof(size_t));
  write(req_fd, xs, sizeof(size_t) * num_seats);
  write(req_fd, ys, sizeof(size_t) * num_seats);
  // Checks for response
  read(resp_fd, &code, sizeof(int));
  if (code == 0) {
    return 0;
  } else {
    return 1;
  }
  return 1;
}

int ems_show(int out_fd, unsigned int event_id) {
  int code = SHOW;
  size_t num_rows, num_cols;
  unsigned int* seats;
  write(req_fd, &code, sizeof(int));
  write(req_fd, &event_id, sizeof(unsigned int));
  read(resp_fd, &code, sizeof(int));
  if (code == 0) {
    if (read(resp_fd, &num_rows, sizeof(size_t)) == -1 || read(resp_fd, &num_cols, sizeof(size_t)) == -1) {
      perror("Error reading dimensions from the response pipe");
      return 1;
    }
    seats = malloc(sizeof(unsigned int) * num_rows * num_cols);
    if (seats == NULL) {
      perror("Memory allocation error");
      return 1;
    }

    // Safe read of seating data from pipe
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
  } else {
    return 1;
  }
  return 1;
}

int ems_list_events(int out_fd) {
  printf("Sending list request\n");
  int code = LIST;
  write(req_fd, &code, sizeof(int));
  read(resp_fd, &code, sizeof(int));
  if (code == 0) {
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
    return 1;
  }
  return 1;
}
