#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

int serv = -1;
int client_desc = -1;

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

void handle_signal(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        
        if (client_desc >= 0) close(client_desc);
        if (serv >= 0) close(serv);
        
        unlink(DATA_FILE);
        closelog();
        exit(0);
    }
}

void daemonize() {
    pid_t P = fork();
    if (P < 0) {
        syslog(LOG_ERR, "daemonization failed");
        exit(-1);
    }
    if (P > 0) {
        exit(0);
    }
    if (setsid() < 0) {
        syslog(LOG_ERR, "failed to create session");
        exit(-1);
    }
    /*if (chdir("/") < 0) {
        syslog(LOG_ERR, "Change directory to root failed");
        exit(-1);
    }*/
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);
}

int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);

    int run_as_daemon = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        run_as_daemon = 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    serv = socket(AF_INET, SOCK_STREAM, 0);
    if (serv < 0) {
        syslog(LOG_ERR, "Socket creation failed");
        return -1;
    }

    int opt = 1;
    if (setsockopt(serv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "Socket config failed");
        close(serv);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(serv, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Socket failed to bind to port %d", PORT);
        close(serv);
        return -1;
    }

    if (run_as_daemon) {
        daemonize();
    }

    if (listen(serv, 10) < 0) {
        syslog(LOG_ERR, "Socket failed to listen");
        close(serv);
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        client_desc = accept(serv, (struct sockaddr *)&client_addr, &client_len);
        if (client_desc < 0) {
            syslog(LOG_ERR, "failed connection to incoming client node");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        char *packet_buff = NULL;
        size_t packetsz = 0;
        char read_buff[BUFFER_SIZE];
        ssize_t bytes_received;
        int newline = 0;

        while (!newline && (bytes_received = recv(client_desc, read_buff, BUFFER_SIZE, 0)) > 0) {
            char *new_ptr = realloc(packet_buff, packetsz + bytes_received);
            if (new_ptr == NULL) {
                syslog(LOG_ERR, "new_ptr failed");
                free(packet_buff);
                packet_buff = NULL;
                break;
            }
            packet_buff = new_ptr;
            memcpy(packet_buff + packetsz, read_buff, bytes_received);
            packetsz += bytes_received;

            for (size_t i = packetsz - bytes_received; i < packetsz; i++) {
                if (packet_buff[i] == '\n') {
                    newline = 1;
                    break;
                }
            }
        }

        if (newline && packet_buff != NULL) {
            FILE *write_file = fopen(DATA_FILE, "a");
            if (write_file != NULL) {
                fwrite(packet_buff, 1, packetsz, write_file);
                fclose(write_file);

                FILE *read_file = fopen(DATA_FILE, "r");
                if (read_file != NULL) {
                    char send_buffer[BUFFER_SIZE];
                    size_t bytes_read;
                    while ((bytes_read = fread(send_buffer, 1, BUFFER_SIZE, read_file)) > 0) {
                        send(client_desc, send_buffer, bytes_read, 0);
                    }
                    fclose(read_file);
                }
            }
        }

        free(packet_buff);
        close(client_desc);
        client_desc = -1;
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    close(serv);
    closelog();
    return 0;
}
