#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/queue.h>
#include <fcntl.h>
#include <sys/ioctl.h>



#include "../aesd-char-driver/aesd_ioctl.h"



#define PORT 9000
#define BUFFER_SIZE 1024



#define FILE_PATH "/dev/aesdchar"



bool run_program = true;
int socketfd;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;



struct thread_node {
 pthread_t thread;
 int clientfd;
 bool complete;
 SLIST_ENTRY(thread_node) entries;
};



SLIST_HEAD(thread_list, thread_node) head;



/* ---------------- SIGNAL ---------------- */
void end_signal(int signum)
{
 syslog(LOG_INFO, "Caught signal, exiting");
 run_program = false;
 shutdown(socketfd, SHUT_RDWR);
}



/* ---------------- SEEK PARSE ---------------- */
static bool parse_seek_command(const char *buf,
 unsigned int *cmd,
 unsigned int *offset)
{
 const char *prefix = "AESDCHAR_IOCSEEKTO:";



 if (strncmp(buf, prefix, strlen(prefix)) != 0)
 return false;



 char *newline = strchr(buf, '\n');
 if (newline)
 *newline = '\0';



 return sscanf(buf + strlen(prefix), "%u,%u", cmd, offset) == 2;
}




/* ---------------- THREAD ---------------- */
void* client_thread_func(void* arg)
{
 struct thread_node *node = (struct thread_node*)arg;
 int clientfd = node->clientfd;



 char buffer[BUFFER_SIZE];
 char *full_data = NULL;
 size_t total_size = 0;



 /* -------- RECEIVE DATA SAFELY -------- */
while (1) {
 ssize_t bytes = recv(clientfd, buffer, BUFFER_SIZE, 0);
 if (bytes <= 0)
 break;



 char *tmp = realloc(full_data, total_size + bytes + 1);
 if (!tmp) {
 free(full_data);
 close(clientfd);
 return NULL;
 }



 full_data = tmp;
 memcpy(full_data + total_size, buffer, bytes);
 total_size += bytes;



 full_data[total_size] = '\0';



 if (memchr(buffer, '\n', bytes))
 break;
}



 if (!full_data) {
 close(clientfd);
 node->complete = true;
 return NULL;
 }



 pthread_mutex_lock(&file_mutex);



 int fd = open(FILE_PATH, O_RDWR);
 if (fd < 0) {
 pthread_mutex_unlock(&file_mutex);
 free(full_data);
 close(clientfd);
 node->complete = true;
 return NULL;
 }



 unsigned int cmd = 0, offset = 0;



 /* -------- IOCTL COMMAND -------- */
 if (parse_seek_command(full_data, &cmd, &offset)) {



 struct aesd_seekto seekto;
 seekto.write_cmd = cmd;
 seekto.write_cmd_offset = offset;



 ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto);



 ssize_t bytes;
 while ((bytes = read(fd, buffer, BUFFER_SIZE)) > 0) {
 send(clientfd, buffer, bytes, 0);
 }



 } else {



 /* -------- NORMAL WRITE -------- */
 if (!parse_seek_command(full_data, &cmd, &offset)) {
 write(fd, full_data, total_size);
}



 close(fd);
 fd = open(FILE_PATH, O_RDONLY);



 if (fd >= 0) {
 ssize_t bytes;
 while ((bytes = read(fd, buffer, BUFFER_SIZE)) > 0) {
 send(clientfd, buffer, bytes, 0);
 }
 close(fd);
 }
 }



 close(fd);
 pthread_mutex_unlock(&file_mutex);



 free(full_data);
 close(clientfd);



 node->complete = true;
 return NULL;
}



/* ---------------- MAIN ---------------- */
int main(int argc, char *argv[])
{
 openlog("aesdsocket", 0, LOG_USER);
 SLIST_INIT(&head);



 struct sigaction sa;
 memset(&sa, 0, sizeof(sa));
 sa.sa_handler = end_signal;



 sigaction(SIGINT, &sa, NULL);
 sigaction(SIGTERM, &sa, NULL);



 socketfd = socket(AF_INET, SOCK_STREAM, 0);



 int opt = 1;
 setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));



 struct sockaddr_in addr;
 memset(&addr, 0, sizeof(addr));



 addr.sin_family = AF_INET;
 addr.sin_port = htons(PORT);
 addr.sin_addr.s_addr = INADDR_ANY;



 bind(socketfd, (struct sockaddr*)&addr, sizeof(addr));



 listen(socketfd, 5);



 while (run_program) {



 struct sockaddr client_addr;
 socklen_t len = sizeof(client_addr);



 int clientfd = accept(socketfd, &client_addr, &len);
 if (clientfd < 0)
 continue;



 syslog(LOG_INFO, "Accepted connection");



 struct thread_node *node = malloc(sizeof(*node));
 node->clientfd = clientfd;
 node->complete = false;



 SLIST_INSERT_HEAD(&head, node, entries);
 pthread_create(&node->thread, NULL, client_thread_func, node);



 struct thread_node *curr = SLIST_FIRST(&head);
 while (curr) {
 struct thread_node *next = SLIST_NEXT(curr, entries);



 if (curr->complete) {
 pthread_join(curr->thread, NULL);
 SLIST_REMOVE(&head, curr, thread_node, entries);
 free(curr);
 }



 curr = next;
 }
 }



 while (!SLIST_EMPTY(&head)) {
 struct thread_node *curr = SLIST_FIRST(&head);
 pthread_join(curr->thread, NULL);
 SLIST_REMOVE_HEAD(&head, entries);
 free(curr);
 }



 close(socketfd);
 closelog();
 return 0;
}
