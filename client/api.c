#include "api.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

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
  mkfifo(req_pipe_path, 0666);
  mkfifo(resp_pipe_path, 0666);
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
  //THIS IS DUMB AS
  req_pipe = malloc(strlen(req_pipe_path) + 1);
  resp_pipe = malloc(strlen(resp_pipe_path) + 1);
  strcpy(req_pipe, req_pipe_path);
  strcpy(resp_pipe, resp_pipe_path);
  read(resp_fd, &id, sizeof(unsigned int));
  return 0;
}

int ems_quit(void) { 
  //TODO: close pipes
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
  //TODO: send create request to the server (through the request pipe) and wait for the response (through the response pipe)
  printf("sending create request\n");
  int code = CREATE;
  write(req_fd, &code, sizeof(int));
  return 1;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  //TODO: send reserve request to the server (through the request pipe) and wait for the response (through the response pipe)
  printf("sending reserve request\n");
  int code = RESERVE;
  write(req_fd, &code, sizeof(int));
  return 1;
}

int ems_show(int out_fd, unsigned int event_id) {
  //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
  printf("sending show request\n");
  int code = SHOW;
  write(req_fd, &code, sizeof(int));
  return 1;
}

int ems_list_events(int out_fd) {
  //TODO: send list request to the server (through the request pipe) and wait for the response (through the response pipe)
  printf("sending list request\n");
  int code = LIST;
  write(req_fd, &code, sizeof(int));
  return 1;
}
