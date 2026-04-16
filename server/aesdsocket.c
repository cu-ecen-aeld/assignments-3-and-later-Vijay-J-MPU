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

 

#define PORT 9000
#define BUFFER_SIZE 1024

 

#define USE_AESD_CHAR_DEVICE 1

 

#ifdef USE_AESD_CHAR_DEVICE
#define FILE_PATH "/dev/aesdchar"
#else
#define FILE_PATH "/var/tmp/aesdsocketdata"
#include <time.h>
#endif

 

bool run_program = true;
int socketfd;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

 

// Thread node
struct thread_node {
    pthread_t thread;
    int clientfd;
    bool complete;
    SLIST_ENTRY(thread_node) entries;
};

 

SLIST_HEAD(thread_list, thread_node) head;

 

// Cleanup
void cleanup() {
    close(socketfd);
    
#ifndef USE_AESD_CHAR_DEVICE
    unlink(FILE_PATH);
#endif

    closelog();
}

 

// Signal handler
void end_signal(int signum) {
    syslog(LOG_INFO, "Caught signal, exiting");
    run_program = false;
    shutdown(socketfd, SHUT_RDWR);
}

 

#ifndef USE_AESD_CHAR_DEVICE
// Timer thread (ONLY for file mode)
void* timer_thread_func(void* arg) {
    while(run_program) {
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);

 

        char timestamp[100];
        strftime(timestamp, sizeof(timestamp),
                 "timestamp:%Y-%m-%d %H:%M:%S\n", &tm_now);

 

        pthread_mutex_lock(&file_mutex);

 

        int fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if(fd >= 0) {
            write(fd, timestamp, strlen(timestamp));
            close(fd);
        }

 

        pthread_mutex_unlock(&file_mutex);

 

        for(int i = 0; i < 10 && run_program; i++) {
            sleep(1);
        }
    }
    return NULL;
}
#endif

 

// Client thread
void* client_thread_func(void* arg) {
    struct thread_node *node = (struct thread_node*)arg;
    int clientfd = node->clientfd;

 

    char buffer[BUFFER_SIZE];
    char *full_data = NULL;
    size_t total_size = 0;

 

    // Receive until newline
    while(1) {
        ssize_t bytes_received = recv(clientfd, buffer, BUFFER_SIZE, 0);
        if(bytes_received <= 0) break;

 

        full_data = realloc(full_data, total_size + bytes_received);
        memcpy(full_data + total_size, buffer, bytes_received);
        total_size += bytes_received;

 

        if(memchr(buffer, '\n', bytes_received)) break;
    }

 

    pthread_mutex_lock(&file_mutex);

 

    int fd = open(FILE_PATH, O_RDWR);
    if(fd >= 0) {

 

        // Write received data
        write(fd, full_data, total_size);

 

        // Read entire content back
        lseek(fd, 0, SEEK_SET);

 

        ssize_t bytes;
        while((bytes = read(fd, buffer, BUFFER_SIZE)) > 0) {
            send(clientfd, buffer, bytes, 0);
        }

 

        close(fd);
    }

 

    pthread_mutex_unlock(&file_mutex);

 

    free(full_data);
    close(clientfd);

 

    node->complete = true;
    return NULL;
}

 

int main(int argc, char *argv[]) {
    openlog("aesdsocket", 0, LOG_USER);

 

    SLIST_INIT(&head);

 

    // Signal handling
    struct sigaction sig_handler;
    memset(&sig_handler,0,sizeof(sig_handler));
    sig_handler.sa_handler = end_signal;

 

    sigaction(SIGINT,&sig_handler,NULL);
    sigaction(SIGTERM,&sig_handler,NULL);

 

#ifndef USE_AESD_CHAR_DEVICE
    // Create file only in file mode
    int fd = open(FILE_PATH, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if(fd >= 0) close(fd);
#endif

 

    // Socket setup
    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0) return -1;

 

    int optval = 1;
    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

 

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

 

    if(bind(socketfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        cleanup();
        return -1;
    }

 

    // Daemon mode
    if(argc>1 && strcmp(argv[1],"-d")==0){
        pid_t pid = fork();
        if(pid < 0) return -1;
        if(pid > 0) exit(0);

 

        setsid();
        chdir("/");

 

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

 

    listen(socketfd, 5);

 

#ifndef USE_AESD_CHAR_DEVICE
    pthread_t timer_thread;
    pthread_create(&timer_thread, NULL, timer_thread_func, NULL);
#endif

 

    while(run_program){
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

 

        int clientfd = accept(socketfd, &client_addr, &client_addr_len);
        if(clientfd < 0) {
            if(!run_program) break;
            continue;
        }

 

        syslog(LOG_INFO, "Accepted connection");

 

        struct thread_node *node = malloc(sizeof(struct thread_node));
        node->clientfd = clientfd;
        node->complete = false;

 

        SLIST_INSERT_HEAD(&head, node, entries);
        pthread_create(&node->thread, NULL, client_thread_func, node);

 

        // Cleanup completed threads
        struct thread_node *curr = SLIST_FIRST(&head);
        while(curr != NULL) {
            struct thread_node *tmp = SLIST_NEXT(curr, entries);

 

            if(curr->complete) {
                pthread_join(curr->thread, NULL);
                SLIST_REMOVE(&head, curr, thread_node, entries);
                free(curr);
            }
            curr = tmp;
        }
    }

 

    // Join remaining threads
    while(!SLIST_EMPTY(&head)) {
        struct thread_node *curr = SLIST_FIRST(&head);
        pthread_join(curr->thread, NULL);
        SLIST_REMOVE_HEAD(&head, entries);
        free(curr);
    }

 

#ifndef USE_AESD_CHAR_DEVICE
    pthread_join(timer_thread, NULL);
#endif

 

    cleanup();
    return 0;
}



