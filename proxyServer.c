#include <stdio.h>
#include <netinet/in.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <pthread.h>
#include "threadpool.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#define BUFF_MAX_len 1024
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

int dispatch_client_side(void* arg);
int read_from_socket(int socket_fd, char *msg_received, size_t msg_size) ;
void send_error_response(int socket_fd, char *protocol, char *tb_now, char *code);
int check_filter( char *host, FILE *filter_file);
FILE *global_filter_file; // Global variable to store the file pointer

int main(int argc, char* argv[]) {
    if (argc != 5){
        fprintf(stderr, "Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        exit(1);
    }
    int port = atoi(argv[1]);
    int pool_size = atoi(argv[2]);
    int request_counter = atoi(argv[3]);
    char *filter_file = argv[4];
    if (port < BUFF_MAX_len || port > 65535 || pool_size <= 0|| pool_size > MAXT_IN_POOL || request_counter <= 0 || filter_file == NULL){
        fprintf(stderr, "Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        exit(1);
    }
    
    global_filter_file = fopen(filter_file, "r");
    if (global_filter_file == NULL) {
        perror("error: opening filter file");
		exit(1);
    }
    threadpool* pool = create_threadpool(pool_size);
    if (!pool) {
        fclose(global_filter_file);
        perror("error: creating thread pool");
		exit(1);
    }

    int listen_socket, new_socket, counter = 0;
    int* client_socket;
    struct sockaddr_in server, client_addr;
    memset(&server, 0, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket < 0){
        perror("error: creating socket");
        fclose(global_filter_file);
        destroy_threadpool(pool);
        exit(1);
    }
    if (bind(listen_socket, (struct sockaddr*)&server, sizeof(struct sockaddr_in)) < 0){
        perror("error: bind");
        close(listen_socket);
        fclose(global_filter_file);
        destroy_threadpool(pool);
        exit(1);
    }
    if (listen(listen_socket, 5) < 0){
        perror("error: listen");
        close(listen_socket);
        fclose(global_filter_file);
        destroy_threadpool(pool);
        exit(1);
    }
    socklen_t socklen = sizeof(struct sockaddr_in);
    while (counter < request_counter){
        memset(&client_addr, 0, sizeof(struct sockaddr_in)); // Initialize client_addr to zero
        new_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &socklen);
        if (new_socket < 0) { 
            perror("error accepting new connection");
            continue; // Continue listening for new connections
        }
        client_socket = (int*)calloc(1, sizeof(int));
        if (!client_socket) {
            perror("allocating memory");
            close(new_socket);
            continue; // Continue listening for new connections
        }
        *client_socket = new_socket;
        dispatch(pool, dispatch_client_side, (void*)client_socket);
        counter++;
    }
    destroy_threadpool(pool);
    close(listen_socket);

    fclose(global_filter_file);

    return 0;
}

int dispatch_client_side(void *arg) {
    if (arg == NULL) {
        fprintf(stderr, "Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        return -1;
    }
    int client_socket_fd = *((int *)arg);
    free(arg);
    char timebuf[128];
    char request[BUFF_MAX_len*2] = {0};
    time_t now = time(NULL);
    strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S %Z", gmtime(&now));
    if (read_from_socket(client_socket_fd, request, sizeof(request)) != 0) {
	    perror("read");
		close(client_socket_fd);
		return -1;
    }
    char method[1024], path[1024], protocol[1024], host_temp[1024];
    memset(method, 0, sizeof(method));
    memset(path, 0, sizeof(path));
    memset(protocol, 0, sizeof(protocol));
    memset(host_temp, 0, sizeof(host_temp));
    if (sscanf(request, "%s %s %s\r\nHost: %s", method, path, protocol, host_temp)!= 4){
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "400 Bad Request");
        close(client_socket_fd);
        return -1;
    }
    protocol[8]='\0';
    if (strcmp(protocol, "HTTP/1.0") != 0 && strcmp(protocol, "HTTP/1.1") != 0) {
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "400 Bad Request");
        close(client_socket_fd);
        return -1;
    }
    if (strcmp(method, "GET") != 0) {
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "501 Not Supported");
        close(client_socket_fd);
        return -1;
    }
    int host_length = strlen(host_temp);
    char *host = (char *)malloc(host_length + 1);
    if (host == NULL) {
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "500 Internal Server Error");
        close(client_socket_fd);
        return -1;
    }
    strcpy(host, host_temp);
    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0) {
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "500 Internal Server Error");
        close(client_socket_fd);
        free(host);
        return -1;
    }
    if (check_filter(host, global_filter_file)== 0) {
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "403 Forbidden");
        close(client_socket_fd);
        close(server_socket_fd);
        free(host); // Free previously allocated memory
        return -1;
    }
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "404 Not Found");
        close(client_socket_fd);
        close(server_socket_fd);
        free(host); // Free previously allocated memory
        return -1;
    }


    // Construct the modified request
    char modified_request[BUFF_MAX_len*4];
    sprintf(modified_request, "%s %s %s\r\nHost: %s\r\nConnection: close\r\n\r\n", method, path,protocol, host);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy((char *)&server_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    server_addr.sin_port = htons(80); // Assuming HTTP server
    // Connect to the server
    if (connect(server_socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "500 Internal Server Error");
        free(host);
        close(client_socket_fd);
        close(server_socket_fd);
        return -1;
    }
    if (write(server_socket_fd, modified_request, strlen(modified_request)) < 0) {
        send_error_response(client_socket_fd, "HTTP/1.1", timebuf, "500 Internal Server Error");
        free(host);
        close(client_socket_fd);
        close(server_socket_fd);
        return -1;
    }
    char response_buffer[BUFF_MAX_len] = {0};
    ssize_t bytesRead;
    ssize_t totalbytes = 0; // Initialize totalbytes
    while (1) {
        bzero(response_buffer, sizeof(response_buffer));
        bytesRead = read(server_socket_fd, response_buffer, sizeof(response_buffer) - 1);
        if (bytesRead < 0) { // failed receiving data
            free(host);
            close(client_socket_fd);
            close(server_socket_fd);
            return -1;
        } 
        else if (bytesRead > 0) { // received data
            //printf("%.*s", (int)bytesRead, response_buffer);
            totalbytes += bytesRead;
            ssize_t bytes_Write = write(client_socket_fd, response_buffer, bytesRead);
            if (bytes_Write == -1) {
                free(host);
                close(client_socket_fd);
                close(server_socket_fd);
                return -1;
            }
        } 
        else { // finish reading
            break;
        }
    }
    free(host);
    close(client_socket_fd);
    close(server_socket_fd);
    return 0;
}

int read_from_socket(int socket_fd, char *msg_received, size_t msg_size) {
    int bytes_read;
    size_t total_bytes_read = 0;
    char temp_data[BUFF_MAX_len] = { 0 };
    while (1) {
        bytes_read = read(socket_fd, temp_data, sizeof(temp_data) - 1);
        if (bytes_read < 0)
            return 1;
        else if (bytes_read > 0) {
            size_t copy_size = (msg_size - total_bytes_read) > bytes_read ? bytes_read : (msg_size - total_bytes_read);
            memcpy(msg_received + total_bytes_read, temp_data, copy_size);
            total_bytes_read += copy_size;
            msg_received[total_bytes_read] = '\0';  // Null-terminate the string
            if (strstr(temp_data, "\r\n\r\n") != NULL)
                break;
            if (total_bytes_read >= msg_size - 1)
                break;
            memset(temp_data, 0, sizeof(temp_data));
        } else
            break;
    }
    return 0;
}
void send_error_response(int socket_fd, char *protocol, char *tb_now, char *code){
    char html_code[BUFF_MAX_len] = {0};
    char headers[BUFF_MAX_len] = {0};
    char response[BUFF_MAX_len*2] = {0};

    sprintf(headers,"%s %s\r\nServer: webserver/1.0\r\nDate: %s\r\nContent-Type: text/html\r\n", protocol, code, tb_now);
	if(strcmp(code,"501 Not Supported")==0){
        sprintf(html_code,
            "<HTML><HEAD><TITLE>%s</TITLE></HEAD>\r\n<BODY><H4>%s</H4>\r\nMethod is %s.\r\n</BODY></HTML>\r\n",
            code, code, code+4);
    }
    else if(strcmp(code,"404 Not Found")==0){
        sprintf(html_code,
            "<HTML><HEAD><TITLE>%s</TITLE></HEAD>\r\n<BODY><H4>%s</H4>\r\nFile %s.\r\n</BODY></HTML>\r\n",
            code, code, code+4);
    }
    else if(strcmp(code,"403 Forbidden")==0){
        sprintf(html_code,
            "<HTML><HEAD><TITLE>%s</TITLE></HEAD>\r\n<BODY><H4>%s</H4>\r\nAccess denied.\r\n</BODY></HTML>\r\n",
            code, code);
    }
    else{
        sprintf(html_code,
            "<HTML><HEAD><TITLE>%s</TITLE></HEAD>\r\n<BODY><H4>%s</H4>\r\n%s.\r\n</BODY></HTML>\r\n",
            code, code, code+4);
    }
    sprintf(headers + strlen(headers),
	    "Content-Length: %lu\r\nConnection: close\r\n\r\n", strlen(html_code));
	
    //build the final response
    sprintf(response, "%s%s", headers, html_code);
    int bytes_written = 1;
    char *yet_to_send = response;
    int bytes_to_write=strlen(response);
    while (bytes_to_write > 0){
        bytes_written = write(socket_fd, yet_to_send, bytes_to_write);
        if (bytes_written < 0) {
            perror("write");
            return ;
        }
        bytes_to_write -= bytes_written;
        yet_to_send += bytes_written;
    }	
    // Do not close the socket here
}
int check_filter(char *host, FILE *filter_file) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // Use IPv4
    hints.ai_socktype = SOCK_STREAM;
    int match_found = 0;

    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        fprintf(stderr, "Error resolving host\n");
        return 1;
    }
    char host_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, host_ip, INET_ADDRSTRLEN);
    fseek(filter_file, 0, SEEK_SET); // Reset file pointer to the start of the file

    char line[256];
    while (fgets(line, sizeof(line), filter_file)) {
        line[strcspn(line, "\n")] = '\0';  // Remove newline character

        if (line[0] >= '0' && line[0] <= '9') {

            char *subnet = strchr(line, '/');
            if (subnet == NULL) {
                continue;
            }
            *subnet = '\0';  // Split the line into IP address and subnet
            int mask = atoi(subnet + 1);
            if (mask < 1 || mask > 32) {
                continue;
            }
            struct in_addr host_addr;
            if (inet_pton(AF_INET, host_ip, &host_addr) != 1) {
                continue;
            }
            struct in_addr filter_addr;
            if (inet_pton(AF_INET, line, &filter_addr) != 1) {
                continue;
            }
            struct in_addr mask_addr;
            mask_addr.s_addr = htonl((0xFFFFFFFF << (32 - mask)) & 0xFFFFFFFF);
            if ((host_addr.s_addr & mask_addr.s_addr) == (filter_addr.s_addr & mask_addr.s_addr)) {
                match_found = 1; // IP address matches filter entry
                break;
            }
        }
        else {
            if (strcmp(host, line) == 0) {
                match_found = 1; // Host name matches filter entry
                break;
            }
            if (strncmp(host, "www.", 4) == 0 && strcmp(host + 4, line) == 0) {
                match_found = 1; // Host name matches filter entry without 'www.'
                break;
            }
        }
    }
    freeaddrinfo(res);
    return match_found ? 0 : -1; // Return 0 if a match was found, -1 otherwise
}


