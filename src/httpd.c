#include "httpd.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

// not entirely sure all these headers are needed, but the project this is based on had them so ¯\_(ツ)_/¯

// ----------------------------

char * files_dir = "www";

unsigned short port = 80;

char * protocol = "HTTP/1.1";

const char version[] = "1.0.0";

// status codes that the server can return and corresponsing struct definition

typedef struct
{
    unsigned short status;
    char message[32];
} Code;

const Code codes[] = {
    { 102, "Processing" },
    { 200, "OK" },
    { 301, "Moved permanently" },
    { 406, "Not acceptable" },
    { 410, "Gone" },
    { 418, "I'm a teapot" },
    { 500, "Internal server error" },
    { 0xFFFF, "Terminator" },
};

void load_file(char * filename, loaded_file * dest, unsigned char qualify, char * method)
{
    FILE * file;
    size_t fsize, rv;
    char * buf;
    char filename_qualified[strlen(files_dir) + strlen(filename) + 2];
    
    sprintf(filename_qualified, "%s/%s", files_dir, filename);
    file = fopen(qualify ? filename_qualified : filename, method);
    
    if (file == NULL)
    {
        dest->filedata = NULL;
        dest->filesize = 0;
        return;
    }
    
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    rewind(file);
    
    buf = malloc(fsize);
    
    if (buf == NULL)
    {
        dest->filedata = NULL;
        dest->filesize = 0;
        fclose(file);
        return;
    }
    
    rv = fread(buf, sizeof(char), fsize, file);
    if (rv != fsize)
    {
        free(buf);
        dest->filedata = NULL;
        dest->filesize = 0;
    }
    else
    {
        dest->filedata = buf;
        dest->filesize = fsize;
    }
    
    fclose(file);
    
    return;
}

UsedFile * files;

int sockfd;

void sig_handler(int signo)
{
    close(sockfd);
    exit(0);
}

// whoops!

void error(const char * msg)
{
    perror(msg);
    exit(1);
}

// function to read the method, path and protocol from an HTTP request, e.g.
// GET /index.html HTTP/1.1
// stops at newlines because this server ignores everything except that one line!

void readHeader(char ** method, char ** path, char ** protocol, char * instring)
{
    char * tmp;
    size_t bufsize;
    size_t i;
    size_t index = 0;
    char ** ptrs[] = { method, path, protocol };
    
    for (unsigned char j = 0; j < 3; j++)
    {
        bufsize = 256;
        tmp = malloc(bufsize); // dynamic memory allocation is fun
        
        if (tmp == NULL)
        {
            *ptrs[j] = NULL;
            continue;
        }
        
        i = 0;
        
        while (1)
        {
            for (; instring[index] != ' ' && instring[index] != '\n' && instring[index] != '\0' && i < bufsize; i++, index++)
            {
                tmp[i] = instring[index];
            }
            tmp[i] = '\0';
            
            if (i < bufsize)
            {
                break;
            }
            
            bufsize *= 2;
            tmp = realloc(tmp, bufsize); // I *think* this implementation doesn't have any memory leaks
            
            if (tmp == NULL)
                break;
        }
        
        if (tmp != NULL) // pointers are checked when the function returns so this is safer than it looks
            tmp = realloc(tmp, strlen(tmp) + 1);
        
        *ptrs[j] = tmp;
        
        index++;
    }
    
}

// builds a response header given a status and a message
// e.g. HTTP/1.1 418 I'm a teapot

char * buildResponse(unsigned short status, char * message)
{
    char * response;
    static const char format[] = "%s %d %s\n%s";
    static const Code errorcode = { 500, "Internal server error" };
    size_t msgLen;
    const Code * cur_code = codes;
    
    while (cur_code->status != status && cur_code->status != 0xFFFF)
        cur_code++;
    
    if (cur_code->status == 0xFFFF)
        cur_code = &errorcode;
    
    msgLen = snprintf(NULL, 0, format, protocol, cur_code->status, cur_code->message, message) + 1;
    
    response = malloc(msgLen);
    if (response == NULL)
    {
        error("Out of memory");
    }
    
    snprintf(response, msgLen, format, protocol, cur_code->status, cur_code->message, message);
    
    return response;
}

// this is the main function that parses incoming requests and decides what to do
// parameters are the request string and a pointer to the length of the output string,
// and it returns a pointer to the output string, because this is the way I did it

char * handleHeaders(char * request, size_t * length)
{
    char * method;
    char * path;
    char * protocol;
    char * response;
    
    readHeader(&method, &path, &protocol, request); // read header
    
    #if DEBUG
    printf("%s %s %s\n", method, path, protocol);
    #endif
    
    if (method == NULL || path == NULL || protocol == NULL) // if something goes wrong with reading the header,
    { //                                                       return 500 Internal server error
        if (method != NULL)
            free(method);
        if (path != NULL)
            free(path);
        if (protocol != NULL)
            free(protocol);
        
        response = buildResponse(500, "");
        *length = strlen(response);
        return response;
    }
    
    UsedFile * cur_file = files;
    
    while (cur_file->serverpath != NULL && strcmp(cur_file->serverpath, path) != 0)
        cur_file++;
    
    if (cur_file->serverpath == NULL) // all unhandled file requests redirect to index.html
    {
        response = buildResponse(301, "Location: /index.html\nContent-Length: 66\nConnection: close\nContent-Type: text/html; charset=utf-8\n\n<html>\n<body>\n\t<a href=\"/index.html\">index.html</a>\n</body>\n<html>");
    }
    else
    {
        #if DEBUG
        printf("cur file: path %s, file %s, mime %s, type %d, status %d\n", cur_file->serverpath, cur_file->filepath, cur_file->mime, cur_file->type, cur_file->status);
        #endif
        if (cur_file->type == TEXT) // process text data
        {
            static const char format[] = "Content-Length: %zu\nConnection: keep-alive\nContent-Type: %s; charset=utf-8\n\n";
            size_t headerLen;
            char * header;
            char * msg;
            char * buf;
            
            headerLen = snprintf(NULL, 0, format, cur_file->file.filesize, cur_file->mime) + 1;
            
            header = malloc(headerLen);
            
            if (header == NULL)
            {
                response = buildResponse(500, "");
            }
            else
            {
                snprintf(header, headerLen, format, cur_file->file.filesize, cur_file->mime);
                
                msg = buildResponse(cur_file->status, header);
                
                free(header);
                
                buf = malloc(strlen(msg) + cur_file->file.filesize + 1);
                
                if (msg == NULL || buf == NULL)
                {
                    if (msg != NULL)
                        free(msg);
                    if (buf != NULL)
                        free(buf);
                    
                    response = buildResponse(500, ""); // 500 Internal server error if memory allocation fails
                }
                else
                {
                    strcpy(buf, msg);
                    strcat(buf, cur_file->file.filedata); // text so strcat can be used
                    
                    free(msg);
                    
                    response = buf;
                }
            }
        }
        else if (cur_file->type == BIN) // process binary data
        {
            static const char format[] = "Connection: keep-alive\nContent-type: %s\nContent-length: %zu\n\n";
            size_t headerLen;
            char * header;
            char * msg;
            char * buf;
            size_t len;
            
            headerLen = snprintf(NULL, 0, format, cur_file->mime, cur_file->file.filesize) + 1;
            
            header = malloc(headerLen);
            
            if (header == NULL)
            {
                response = buildResponse(500, "");
            }
            else
            {
                snprintf(header, headerLen, format, cur_file->mime, cur_file->file.filesize);
                
                msg = buildResponse(cur_file->status, header);
                
                free(header);
                
                buf = malloc(strlen(msg) + cur_file->file.filesize + 1);
                
                if (msg == NULL || buf == NULL)
                {
                    if (msg != NULL)
                        free(msg);
                    if (buf != NULL)
                        free(buf);
                    
                    response = buildResponse(500, ""); // 500 Internal server error if memory allocation fails
                }
                else
                {
                    strcpy(buf, msg);
                    
                    len = strlen(buf); // binary so strcat can't be used
                    memcpy(buf + strlen(buf), cur_file->file.filedata, cur_file->file.filesize);
                    
                    free(msg);
                    // server tries to autodetect length if it's not set, but it uses strlen which won't work for images
                    *length = len + cur_file->file.filesize;
                    
                    response = buf;
                }
            }
        }
        else if (cur_file->type == NOPE) // explicitly not found instead of redirecting to index.html
        {
            response = buildResponse(cur_file->status, "Content-Length: 45\nConnection: close\nContent-Type: text/html; charset=utf-8\n\n<html>\n<body>\n\t<p>410 gone</p>\n</body>\n<html>");
        }
        else if (cur_file->type == REDIR) // handle redirects
        {
            static char redirHeader[] = "Location: %s\nContent-Length: %d\nConnection: close\nContent-Type: text/html; charset=utf-8\n\n%s";
            static char redirContent[] = "<html>\n<body>\n\t<a href=\"%s\">%s</a>\n</body>\n<html>";
            size_t contentLen;
            char * content;
            size_t msgSize;
            char * msg;
            // all of the snprintfs here are to try and be memory-safe without the server operator having to specify memory sizes
            // I think it worked?
            // if you want to understand it, look very carefully though all the format strings and the HTTP documentation
            contentLen = snprintf(NULL, 0, redirContent, cur_file->filepath, cur_file->filepath) + 1;

            content = malloc(contentLen);
            if (content == NULL)
            {
                 response = buildResponse(500, ""); // memory fun
            }
            else
            {
                snprintf(content, contentLen, redirContent, cur_file->filepath, cur_file->filepath);
                
                msgSize = snprintf(NULL, 0, redirHeader, cur_file->filepath, contentLen, content);
                
                msg = malloc(msgSize);
                if (msg == NULL)
                {
                    response = buildResponse(500, ""); // memory fun
                    free(content);
                }
                else
                {
                    snprintf(msg, msgSize, redirHeader, cur_file->filepath, contentLen, content);
                    
                    response = buildResponse(cur_file->status, msg);
                    free(msg);
                    free(content);
                }
            }
        }
        else
        {
            response = buildResponse(500, "");
        }
    }
    
    free(method);
    free(path); // this is why the separate handler for failures earlier was needed - don't want to free a null pointer!
    free(protocol);
    
    if (*length == 0) // autodetect length if not set
        *length = strlen(response);
    
    return response;
}

// program entrypoint

int main(int argc, char * argv[])
{
    int newsockfd;
    socklen_t clilen;
    char * buffer;
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    size_t buflen;
    size_t readlen;
    char * bufstart;
    UsedFile * cur_file;
    
    // load all the files
    
    files = load_config_file(); // this function is in config.c because it's a large boi and I didn't want 600 lines of finite state machine in the main C file
    
    if (files == NULL)
    {
        error("ERROR loading config file");
    }
    
    cur_file = files;
    
    while (cur_file->type != TERMINATOR)
    {
        if (cur_file->filepath != NULL && cur_file->type != REDIR)
        {
            load_file(cur_file->filepath, &cur_file->file, 1, (cur_file->type == TEXT) ? "r" : "rb");
            if (cur_file->file.filesize == 0)
            {
                char msgbuf[25 + strlen(cur_file->filepath)];
                sprintf(msgbuf, "ERROR opening file %s\n", cur_file->filepath);
                error(msgbuf);
            }
        }
        cur_file++;
    }
    
    printf("%s server version %s starting\n", protocol, version);
    
    // socket and thread code is mostly copied from another implementation, see GitHub page
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        error("ERROR opening socket");
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        error("ERROR on binding");
    }
    
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    // uncomment this line to make it run as a daemon
    // note: this makes debugging hard and is, I believe, not necessary to make it run as a service
    //daemon(1, 0);
    
    for(;;)
    {
        size_t return_length;
        char * return_header;
        
        listen(sockfd, 5);
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        
        if (newsockfd < 0)
        {
            error("ERROR on accept");
        }
        
        buflen = 256;
        readlen = 0;
        
        buffer = malloc(buflen);
        if (buffer == NULL)
        {
            error("ERROR out of memory on accept");
        }
        
        // fun memory stuff to handle requests larger than 256 bytes!
        // dunno why I felt the need to implement this, it's complicated and goes entirely unused
        // I guess to make it easier to expand this in the future? eh
        // this (and the other similar code in the readHeaders function) is implemented kind of like a vector
        // it reads in bytes until the buffer is full, then doubles the size of the buffer
        // seems to work, but this is computer science, so don't trust this to be secure
        
        while (1)
        {
            bufstart = buffer + readlen;
            
            bzero(bufstart, buflen - readlen);
            
            n = read(newsockfd, bufstart, buflen - readlen);
            if (n < 0)
            {
                //error("ERROR reading from socket");
                // don't error on read fail! just try again!
                free(buffer);
                buffer = NULL;
                break;
            }
            
            readlen = buflen;
            
            if (buffer[buflen - 1] == '\0')
                break;
            
            buflen *= 2;
            
            buffer = realloc(buffer, buflen);
            if (buffer == NULL)
                break;
        }
        
        if (buffer == NULL)
        {
            //error("ERROR out of memory on accept");
            continue;
        }
        
        buffer = realloc(buffer, strlen(buffer) + 1);
        
        // process the incoming request
        
        return_length = 0;
        return_header = handleHeaders(buffer, &return_length);
        
        // send a response
        
        n = write(newsockfd, return_header, return_length);
        free(return_header);
        free(buffer);
        
        if (n < 0) 
        {
            error("ERROR writing to socket");
        }
        close(newsockfd);
    }
    
    close(sockfd);
    return 0;
}
