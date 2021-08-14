#include "htcpcpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>

#include "wrappers.h"

// not entirely sure all these headers are needed, but the project this is based on had them so ¯\_(ツ)_/¯

// ----------------------------

// status codes that the server can return and corresponsing struct definition

static const Code codes[] = {
    { 102, "Processing" },
    { 200, "OK" },
    { 301, "Moved permanently" },
    { 406, "Not acceptable" },
    { 410, "Gone" },
    { 418, "I'm a teapot" },
    { 500, "Internal server error" },
    { 0xFFFF, "Terminator" },
};

static void delay(unsigned int millis)
{
    clock_t start_time = clock();
    
    while (clock() < start_time + millis);
}

void load_file(char * filename, loaded_file * dest)
{
    off_t fsize, rv;
    static const char files_dir[] = "www";
    struct stat stbuf;
    int fd;
    char * buf;
    char filename_qualified[strlen(files_dir) + strlen(filename) + 2];
    
    sprintf(filename_qualified, "%s/%s", files_dir, filename);
    fd = open(filename_qualified, O_RDONLY);
    if (fd == -1)
    {
        goto err;
    }
    
    if ((fstat(fd, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode)))
    {
        close(fd);
        goto err;
    }
    
    fsize = stbuf.st_size;
    
    buf = malloc(fsize);
    
    if (buf == NULL)
    {
        close(fd);
        goto err;
    }
    
    rv = read(fd, buf, fsize);
    if (rv != fsize)
    {
        close(fd);
        free(buf);
        goto err;
    }
    
    dest->filedata = buf;
    dest->filesize = fsize;
    return;
    
err:
    dest->filedata = NULL;
    dest->filesize = 0;
    return;
}

static UsedFile files[] = {
    { "index.html", "/index.html", "text/html", {}, TEXT, 418 }, // main page, returns status 418, file on disk is index.html
    { "frens.html", "/frens.html", "text/html", {}, TEXT, 200 }, // frens page, returns status 200, file on disk is frens.html
    { "images/fren0.png", "/images/fren0.png", "image/png", {}, BIN, 200 }, // images
    { "images/fren1.png", "/images/fren1.png", "image/png", {}, BIN, 200 }, // all
    { "images/fren2.png", "/images/fren2.png", "image/png", {}, BIN, 200 }, // return
    { "images/fren3.png", "/images/fren3.png", "image/png", {}, BIN, 200 }, // 200
    { "images/me.png", "/images/me.png", "image/png", {}, BIN, 418 }, // except this one! it's a teapot, so, well, it's a teapot
    { "/favicon.png", "/favicon.ico", NULL, {}, REDIR, 301 }, // redirect favicon.ico to favicon.png
    { "favicon.png", "/favicon.png", "image/png", {}, BIN, 200 }, // favicon, returns status 200, file on disk is favicon.png
    { "styling.css", "/styling.css", "text/css", {}, TEXT, 200 }, // css styling file
    { "Terminator", NULL, NULL, {}, TERMINATOR, 0xFFFF }, // structure terminator
};

// now for the HTCPCP code! very little of this is tested, since our server is a teapot

static unsigned int time_to_brew = 1800;
static unsigned short port = 80;

// stuff for threads that I copied from another implementation

static unsigned char closeThread = 0;
static pthread_t HWThread;
static int sockfd;

static const size_t max_threads = 10;

static void sig_handler(int signo)
{
    closeThread = 1;
    close(sockfd);
    pthread_join(HWThread, NULL);
    exit(0);
}

static pot potinfo;

// brew a cup of coffee

static unsigned char brew(void)
{
    if (potinfo.ready == 1)
    {
        potinfo.ready = 0;
        potinfo.is_brewing = 1;
        potinfo.lastbrew = time(NULL);
        return 0;
    }
    return 1;
}

// self-explanatory, really

static unsigned char say_when(void)
{
    if (potinfo.is_pouring_milk == 1)
    {
        potinfo.is_pouring_milk = 0;
        return 0;
    }
    return 1;
}

// whoops!

__attribute__ ((noreturn)) void error(const char * msg)
{
    perror(msg);
    exit(1);
}

static char get_section(char ** string, char ** dest)
{
    size_t len;
	
	for (len = 0; (*string)[len] != ' ' && (*string)[len] != '\t' && (*string)[len] != '\n' && (*string)[len] != '\r' && (*string)[len] != '\0' && (*string)[len] != ':'; len++);
	
	*dest = strndup_wrapper(*string, len);
	
    *string += len;
    
	while (**string == ' ' || **string == '\t' || **string == '\n' || **string == '\r' || **string == ':')
	{
		(*string)++;
	}
    
	return **string;
}

static void parseRequest(char ** method, char ** path, char ** protocol, char * instring)
{
    char * tmp;
    char ** ptrs[] = { method, path, protocol };
    char rv;
    
    for (unsigned char j = 0; j < 3; j++)
    {
        *ptrs[j] = NULL;
    }
    
    for (unsigned char j = 0; j < 3; j++)
    {
        rv = get_section(&instring, &tmp);
        *ptrs[j] = tmp;
        
        if (rv == '\0')
        {
            break;
        }
    }
}

// builds a response header given a status and a message
// e.g. HTTP/1.1 418 I'm a teapot

static char * buildResponse(unsigned short status, char * message)
{
    char * response;
    static const char format[] = "HTTP/1.1 %d %s\n%s";
    static const Code errorcode = { 500, "Internal server error" };
    size_t msgLen;
    const Code * cur_code = codes;
    
    while (cur_code->status != status && cur_code->status != 0xFFFF)
        cur_code++;
    
    if (cur_code->status == 0xFFFF)
        cur_code = &errorcode;
    
    msgLen = snprintf(NULL, 0, format, cur_code->status, cur_code->message, message) + 1;
    
    response = malloc_wrapper(msgLen);
    
    snprintf(response, msgLen, format, cur_code->status, cur_code->message, message);
    
    return response;
}

// this is the main function that parses incoming requests and decides what to do
// parameters are the request string and a pointer to the length of the output string,
// and it returns a pointer to the output string, because this is the way I did it

static char * handleHeaders(char * request, size_t * length)
{
    char * method;
    char * path;
    char * protocol;
    char * response;
    
    parseRequest(&method, &path, &protocol, request); // read header
    
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
    
    if (strcmp("/teapot", path) == 0 || strcmp(method, "GET") != 0)
    {
        if (strcmp(method, "GET") == 0 || strcmp(method, "BREW") == 0 || strcmp(method, "POST") == 0)
        {
            if (potinfo.is_teapot == 1)
            {
                response = buildResponse(418, ""); // if the coffee pot is permanently a teapot, return 418
            }
            else // otherwise, try and brew coffee
            {
                if (brew() == 0)
                {
                    response = buildResponse(200, "");
                }
                else
                { // 102 Processing if the coffee pot is already brewing coffee
                    response = buildResponse(102, "Content-Length: 16\nConnection: close\nContent-Type: text/plain; charset=utf-8\n\nAlready brewing!");
                }
            }
        }
        else if (strcmp(method, "PROPFIND") == 0)
        {
            if (potinfo.ready == 1)
            {
                response = buildResponse(200, "Content-type: message/coffeepot\n\nPot ready to brew");
            }
            else
            {
                response = buildResponse(200, "Content-type: message/coffeepot\n\nPot not ready to brew");
            }
        }
        else if (strcmp(method, "WHEN") == 0)
        {
            if (say_when() == 0)
            {
                response = buildResponse(200, "");
            }
            else
            { // 102 Processing if the coffee pot is already adding milk
                response = buildResponse(102, "Content-Length: 25\nConnection: close\nContent-Type: text/plain; charset=utf-8\n\nNot currently adding milk");
            }
        }
        else
        { // 406 Not acceptable if the method isn't recognised
            response = buildResponse(406, "");
        }
        
    }
    else
    {
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
                size_t len;
                
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
                        
                        len = strlen(buf); // binary so strcat can't be used
                        memcpy(buf + strlen(buf), cur_file->file.filedata, cur_file->file.filesize);
                        
                        free(msg);
                        // server tries to autodetect length if it's not set, but it uses strlen which won't work for data that isn't null-terminated
                        *length = len + cur_file->file.filesize;
                        
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
                        // server tries to autodetect length if it's not set, but it uses strlen which won't work for data that isn't null-terminated
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
    }
    
    free(method);
    free(path); // this is why the separate handler for failures earlier was needed - don't want to free a null pointer!
    free(protocol);
    
    if (*length == 0) // autodetect length if not set
        *length = strlen(response);
    
    return response;
}

// this is the function that *would* handle the majority of the coffee pot hardware if I had implemented that!
// for now, it's just dummy stuff that messes around with variables
// feel free to add your own code in here to interface with your coffee pot

static void * hardwareHandler(void * arg)
{
    while (1)
    {
        if (closeThread == 1)
        {
            break;
        }
        if (potinfo.ready == 0 && potinfo.is_brewing == 1)
        {
            if (time(NULL) - potinfo.lastbrew >= time_to_brew)
            {
                potinfo.is_brewing = 0;
            }
        }
        
        if (potinfo.is_brewing == 0 && potinfo.is_teapot == 0)
        {
            potinfo.ready = 1;
        }
        
        if (potinfo.is_brewing == 1 && potinfo.is_teapot == 1)
        {
            error("Glitch in the matrix"); // teapots can't brew coffee
            break;
        }
        delay(10);
    }
    pthread_exit(NULL);
}

// handler thread function

static void * handle_request(void * param)
{
    char * buffer;
    int n;
    size_t buflen;
    size_t readlen;
    char * bufstart;
    size_t return_length;
    char * return_header;
    StructPack * data = param;
    
    buflen = 256;
    readlen = 0;
    
    buffer = malloc_wrapper(buflen);
    
    // fun memory stuff to handle requests larger than 256 bytes!
    // dunno why I felt the need to implement this, it's complicated and goes entirely unused
    // I guess to make it easier to expand this in the future? eh
    // this (and the other similar code in the readHeaders function) is implemented kind of like a vector
    // it reads in bytes until the buffer is full, then doubles the size of the buffer
    // seems to work, but this is computer science, so don't trust this to be secure
    
    while (1)
    {
        bufstart = buffer + readlen;
        
        memset(bufstart, 0, buflen - readlen);
        
        n = read(data->fd, bufstart, buflen - readlen);
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
        
        buffer = realloc_wrapper_ignore(buffer, buflen);
        if (buffer == NULL)
            break;
    }
    
    if (buffer == NULL)
    {
        //error("ERROR out of memory on accept");
        goto end;
    }
    
    buffer = realloc_wrapper_shrink(buffer, strlen(buffer) + 1);
    
    // process the incoming request
    
    return_length = 0;
    return_header = handleHeaders(buffer, &return_length);
    
    // send a response
    
    n = write(data->fd, return_header, return_length);
    free(return_header);
    free(buffer);
    
end:
    close(data->fd);
    
    data->inUse = 0;
    
    pthread_exit(NULL);
    
    return NULL;
}

// program entrypoint

int main(int argc, char * argv[])
{
    struct sockaddr_in serv_addr;
    UsedFile * cur_file;
    StructPack * threads;
    
    printf("HTTP/1.1 server starting\n");
    
    // initialise the pot
    
    potinfo.ready = 0;
    potinfo.lastbrew = time(NULL);
    potinfo.is_brewing = 0;
    potinfo.is_pouring_milk = 0;
    potinfo.is_teapot = 1;
    potinfo.milk_available = 0;
    
    // load all the files
    
    cur_file = files;
    
    while (cur_file->type != TERMINATOR)
    {
        if (cur_file->filepath != NULL && cur_file->type != REDIR)
        {
            load_file(cur_file->filepath, &cur_file->file);
            if (cur_file->file.filesize == 0)
            {
                char msgbuf[25 + strlen(cur_file->filepath)];
                sprintf(msgbuf, "ERROR opening file %s\n", cur_file->filepath);
                error(msgbuf);
            }
        }
        cur_file++;
    }
    
    // socket and thread code is mostly copied from another implementation, see GitHub page
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        error("ERROR opening socket");
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    
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
    
    // comment out this line to stop it running as a daemon
    //daemon(1, 0);
    pthread_create(&HWThread, NULL, hardwareHandler, NULL);
    
    threads = calloc_wrapper(max_threads, sizeof(StructPack));
    
    for(;;)
    {
        static socklen_t clilen = sizeof(struct sockaddr_in);
        size_t cur_thread = 0;
        
        while (threads[cur_thread].inUse != 0)
        {
            cur_thread++;
            cur_thread %= max_threads;
        }
        
        listen(sockfd, 5);
        threads[cur_thread].fd = accept(sockfd, (struct sockaddr *)&threads[cur_thread].cli_addr, &clilen);
        
        if (threads[cur_thread].fd < 0)
        {
            //error("ERROR on accept");
            continue;
        }
        
        threads[cur_thread].inUse = 1;
        
        pthread_create(&threads[cur_thread].thread, NULL, handle_request, &threads[cur_thread]);
    }
    
    close(sockfd);
    return 0;
}
