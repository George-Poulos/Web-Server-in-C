#include <fnmatch.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <dirent.h>

#define BACKLOG (10)

int portNumber;
char root[4096];
char current[4096];

void serve_request(int);

char * response_str = "HTTP/1.0 %s\r\n"
"Content-type: %s; charset=UTF-8\r\n\r\n";


char * index_hdr = "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\"><html>"
"<title>Directory listing for %s</title>"
"<body>"
"<h2>Directory listing for %s</h2><hr><ul>";

char * index_body = "<li><a href=\"%s\">%s</a>";

char * index_ftr = "</ul><hr></body></html>";

char* parseRequest(char* request) {
    char *buffer = malloc(sizeof(char)*257);
    memset(buffer, 0, 257);
    
    if(fnmatch("GET * HTTP/1.*",  request, 0)) return 0;
    
    sscanf(request, "GET %s HTTP/1.", buffer);
    
    return buffer;
}


char* getContentType(char* filename) {
    char copyOfString[256];
    strcpy(copyOfString, filename);
    char* tempCopyOfString = strtok(copyOfString, ".");
    char extensionName[256];
    strcpy(extensionName, tempCopyOfString);
    
    while (tempCopyOfString != NULL) {
        strcpy(extensionName, tempCopyOfString);
        tempCopyOfString = strtok(NULL, ".");
    }
    
    if (strcmp(extensionName, "html") == 0) {
        return "text/html";
    } else if (strcmp(extensionName, "txt") == 0)  {
        return "text/plain";
    } else if (strcmp(extensionName, "ico") == 0) {
        return "image/x-icon";
    }  else if (strcmp(extensionName, "gif") == 0 ){
        return "image/gif";
    } else if (strcmp(extensionName, "jpeg") == 0 || strcmp(extensionName, "jpg") == 0 ) {
        return "image/jpeg";
    } else if (strcmp(extensionName, "png") == 0) {
        return "image/png";
    } else if (strcmp(extensionName, "pdf") == 0) {
        return "application/pdf";
    }else {
        return "application/octet-stream";
    }
    
}


char* fileResponse(char* filename, int status_code) {
    char* response = (char*) malloc(sizeof(char)*512);
    memset(response, 0, 512);
    char* statusCode;
    switch (status_code){
        case 200 :
            statusCode = "200 OK";
            break;
        case 400 :
            statusCode = "404 Not Found";
            break;
        default:
            statusCode = "500 Internal Server Error";
    }
    char temp[512];
    snprintf(temp, 512, response_str, statusCode, getContentType(filename));
    strcpy(response, temp);
    return response;
}

void sendFile(int file_fd, int client_fd) {
    size_t readBytes;
    char clientSendBuf[4096];
    while(1){
        readBytes = read(file_fd ,clientSendBuf,4096);
        if(readBytes == 0)
            break;
        if (readBytes == -1) {
            printf("error %s\n", strerror(errno));
            break;
        }
        size_t sent = send(client_fd,clientSendBuf,readBytes,0);
        while (sent < readBytes) {
            sent += send(client_fd, clientSendBuf + sent, readBytes - sent, 0);
        }
    }
}

void sendString(char* stringToSend, int client_fd) {
    long lengthOfString = strlen(stringToSend);
    long bytesSent = send(client_fd, stringToSend, lengthOfString, 0);
    while (bytesSent < lengthOfString) {
        bytesSent = bytesSent + send(client_fd, stringToSend + bytesSent, lengthOfString - bytesSent, 0);
    }
}

void dir_listing(char* dirpath, int client_fd, char* relative_path) {
    DIR *dir;
    char listing[8192];
    char temp[4096];
    snprintf(temp, 4096, index_hdr, dirpath, dirpath);
    strcpy(listing, temp);
    struct dirent *und_file;
    dir = opendir (dirpath);
    if (dir != NULL)
    {
        while ((und_file = readdir(dir))) {
            char row[4096];
            char filepath[4096];
            snprintf(filepath, 4096, "%s/%s/", relative_path, und_file->d_name);
            snprintf(row, 4096, index_body, filepath, und_file->d_name);
            strcat(listing, row);
        }
            closedir (dir);
    } else
        perror ("Can't Open Directory");
    
    char row[4096];
    snprintf(row, 4096, index_ftr, portNumber);
    strcat(listing, row);
    sendString(listing, client_fd);
}


int is_directory(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0)
        return 0;
    return S_ISDIR(statbuf.st_mode);
}

void status200File(char newFilePath[8192], char * requested_file, int client_fd){
    sendString(fileResponse(".html", 200), client_fd);
    dir_listing(newFilePath, client_fd, requested_file);
    printf("Response 200 : %d\n", client_fd);
    free(requested_file);
}
void status500(char newFilePath[8192], char * requested_file, int client_fd, int read_fd){
    sendString(fileResponse(requested_file, 500), client_fd);
    printf("can't open index.html, error: %s\n", strerror(errno));
    close(read_fd);
    free(requested_file);
}

void status200(char newFilePath[8192], char * requested_file, int client_fd, int read_fd){
    sendString(fileResponse("index.html", 200), client_fd);
    sendFile(read_fd, client_fd);
    printf("Response 200 : %d\n", client_fd);
    close(read_fd);
    free(requested_file);
}

void status200UserFile(char newFilePath[8192], char * requested_file, int client_fd, int read_fd){
    sendString(fileResponse(requested_file, 200), client_fd);
    sendFile(read_fd, client_fd);
    printf("Response 200 : %d\n", client_fd);
    close(read_fd);
    free(requested_file);
}

void status404UserFile(char newpath[8192], char * requested_file, int client_fd, int read_fd){
    sendString(fileResponse(requested_file, 404), client_fd);
    printf("can't locate 404.html at %s\n", newpath);
    free(requested_file);
    close(read_fd);
}
void status404File(char newpath[8192], int client_fd, int read_fd){
    sendString(fileResponse("404.html", 404), client_fd);
    sendFile(read_fd, client_fd);
    close(read_fd);
}

void serve_request(int client_fd){
    char client_buf[4096];
    int read_fd;
    char * requested_file;
    memset(client_buf,0,4096);
    int file_offset = 0;
    while(1){
        file_offset += recv(client_fd,&client_buf[file_offset],4096,0);
        if(strstr(client_buf,"\r\n\r\n"))
            break;
    }
    requested_file = parseRequest(client_buf);
    printf("%s\n", requested_file);
    char newFilePath[8192];
    snprintf(newFilePath, 8192, "%s/%s%s", current, root, requested_file);
    if (is_directory(newFilePath)) {
        char indexpath[8192];
        snprintf(indexpath, 8192, "%s/index.html", newFilePath);

        read_fd = open(indexpath,0 ,0);
        if (read_fd < 0) {
            struct stat buffer;
            if (stat(indexpath, &buffer) < 0 && errno == ENOENT) {
                status200File(newFilePath,requested_file,client_fd);
                return;
            }
            else {
                status500(newFilePath, requested_file, client_fd,read_fd);
                return;
            }
        }
        else {
            status200(newFilePath, requested_file, client_fd,read_fd);
            return;
        }
    }
    else {
        read_fd = open(newFilePath,0,0);
        if (read_fd < 0) {
            struct stat buffer;
            if (stat(newFilePath, &buffer) < 0 && errno == ENOENT) {
                char newpath[8192];
                snprintf(newpath, 8192, "%s/%s/404.html", current, root);
                close(read_fd);
                read_fd = open(newpath, 0, 0);
                if (read_fd < 0) {
                    status404UserFile(newpath, requested_file, client_fd, read_fd);
                    return;
                }
                status404File(newFilePath, client_fd, read_fd);
                free(requested_file);
                return;
            }
            else {
                status500(newFilePath, requested_file, client_fd, read_fd);
                return;
            }
        }
        else {
            status200UserFile(newFilePath, requested_file, client_fd, read_fd);
        }
    }
    
    return;
}

int main(int argc, char** argv) {
    if(argc < 3){
        printf("Missing argument!\n");
        exit(1);
    }
    
    portNumber = atoi(argv[1]);
    
    strcpy(root, argv[2]);
    
    
    
    
    if (!getcwd(current, 4096)) {
        printf("failed to get working directory error: %s\n", strerror(errno));
        exit(1);
    }
    char dir[8192];
    snprintf(dir, 8192, "%s/%s", current, root);
    
    if(!is_directory(dir)){
        printf("Root Directory provided %s, can not be found!\n", root);
        exit(1);
    }
   
    printf("Web Server is now running, running on port : %d, \nroot directory : %s\n", portNumber, dir);
    /* Create a socket to which clients will connect. */
    int server_sock = socket(AF_INET6, SOCK_STREAM, 0);
    if(server_sock < 0) {
        perror("Creating socket failed\n");
        exit(1);
    }
    
    int reuse_true = 1;
    int returnVal = setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse_true,
                        sizeof(reuse_true));
    if (returnVal < 0) {
        perror("Socket option failed\n");
        exit(1);
    }
    
    struct sockaddr_in6 addr;
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(portNumber);
    addr.sin6_addr = in6addr_any;
    
    
    returnVal = bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    if(returnVal < 0) {
        perror("Can't bind to port\n");
        exit(1);
    }
    
    returnVal = listen(server_sock, BACKLOG);
    if(returnVal < 0) {
        perror("There was an error while listening to connections\n");
        exit(1);
    }
    
    while(1) {
        int sock;
        struct sockaddr_in clientAddress;
        unsigned int socketLength = sizeof(clientAddress);
        
        sock = accept(server_sock, (struct sockaddr*) &clientAddress, &socketLength);
        if(sock < 0) {
            perror("Error accepting connection");
            exit(1);
        }
        
        if (fork() == 0) {
            close(server_sock);
            serve_request(sock);
            close(sock);
            exit(0);
        }
        
        close(sock);
    }
    
    close(server_sock);
}

