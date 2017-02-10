#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>

#include "include/listener.h"
#include "include/constants.h"
#include "include/utils.h"

// Static functions

char *allocate_buf();
struct sockaddr_in *initialize_remote_sockaddr();
void listener_error_exit(char *msg);
char *get_listener_msg(char *msg, int listener_number);
void listener_error_exit(char *msg);
void store_batch(Packet *pack, BatchStock *bs, listener_t type);
void do_first_run(ListenerPack *lp, char *buf, 
                  struct sockaddr_in *remote_addr);
void receive_packet(int sd, char *buf, struct sockaddr_in *remote_addr, 
                    listener_t type);
void wait_unitl_calc_reads(ListenerPack *lp);

// End of static functions region

void run_listener(ListenerPack *lp) {
  char *bufin = allocate_buf(); 
  struct sockaddr_in *remote = initialize_remote_sockaddr();
  do_first_run(lp, bufin, remote);
  while(1) {
    receive_packet(lp->sd, bufin, remote, lp->type); 
    Packet *received_pack = bytes_to_pack(bufin);
    wait_until_calc_reads(lp);
    store_batch(received_pack, lp->batch_stock, lp->type);
    pthread_mutex_unlock(lp->mu_set->batch_stock_mu);
  }
}

ListenerPack *initialize_listener_pack(listener_t type, int sd, 
                    ListenerMutexSet *mu_set, BatchStock *bs, FILE *log_file) {
  ListenerPack *lp = (ListenerPack *) malloc(sizeof(ListenerPack));
  lp->type = type;
  lp->sd = sd;
  lp->mu_set = mu_set;
  lp->batch_stock = bs;
  lp->log_file = log_file;
  return lp;
}

void free_listener_pack(ListenerPack *lp) {
  free(lp);
}

ListenerMutexSet *create_listener_mutex_set(pthread_mutex_t *batch_stock_mu,
                    pthread_mutex_t *calc_batch_mu, pthread_mutex_t *io_mu) {
  ListenerMutexSet *lms = (ListenerMutexSet *)malloc(sizeof(ListenerMutexSet));
  lms->batch_stock_mu = batch_stock_mu;
  lms->calc_batch_mu = calc_batch_mu;
  lms->io_mu = io_mu;
  return lms;
}

void free_listener_mutex_set(ListenerMutexSet *mu_set) {
  free(mu_set);
}

char *allocate_buf() {
  char *buf = (char*) malloc(sizeof(char) * MAXBUF);
  return buf;
}

struct sockaddr_in *initialize_remote_sockaddr() {
  struct sockaddr_in *remote_sockaddr = 
            (struct sockaddr_in *) malloc(sizeof(struct sockaddr_in));
  return remote_sockaddr;
}

void listener_error_exit(char *msg) {
  size_t msg_len = strlen(msg);
  char msg_local[msg_len + 1];
  strncpy(msg_local, msg, msg_len);
  msg_local[msg_len] = '\0';
  exit_error(msg_local);
}

char *get_listener_msg(char *msg, int listener_number) {
  char *prefix_listener = "Listener ";
  size_t prefix_listener_len = strlen(prefix_listener);
  size_t num_repr_len = 12;
  size_t msg_len = strlen(msg);
  size_t total_len = prefix_listener_len + num_repr_len + msg_len + 1;
  char *result = (char *) malloc (sizeof(char) * total_len); 
  snprintf(result, total_len - 1, "%s%d: %s", 
            prefix_listener, listener_number, msg); 
  return result;
}

void store_batch(Packet *pack, BatchStock *bs, listener_t type) {
  float *payload = get_floats(pack);
  size_t count = get_floats_count(pack);
  char *err_msg;
  switch (type) {
    case LISTENER_FIRST:
      add_first_stock_to_batch(bs, payload, count);
      break;
    case LISTENER_SECOND:
      add_second_stock_to_batch(bs, payload, count);
      break;
    default:
      err_msg = 
          get_listener_msg("Cannot store batch: packet type unknown", type);
      listener_error_exit(err_msg);
  }
  return; 
}

void receive_packet(int sd, char *buf, struct sockaddr_in *remote_addr, 
                    listener_t type) {
  int bytes_recv;
  socklen_t addr_len;
  bytes_recv = recvfrom(sd, buf, MAXBUF, 0, (struct sockaddr *) remote_addr,
                          &addr_len);
  if (bytes_recv < 0) {
      char *err_msg = get_listener_msg("Error receiving data", type);
      listener_error_exit(err_msg);
    }
}

void do_first_run(ListenerPack *lp, char *buf, 
                  struct sockaddr_in *remote_addr) {
  receive_packet(lp->sd, buf, remote_addr, lp->type);
  pthread_mutex_unlock(lp->mu_set->batch_stock_mu);
}

void wait_unitl_calc_reads(ListenerPack *lp) {
  pthread_mutex_lock(lp->mu_set->calc_batch_mu);
  pthread_mutex_lock(lp->mu_set->batch_stock_mu);
  pthread_cond_signal();
}