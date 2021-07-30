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
#include <pthread.h>

// not entirely sure all these headers are needed, but the project this is based on had them so ¯\_(ツ)_/¯

// ----------------------------

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

void delay(unsigned int millis)
{
    clock_t start_time = clock();
    
    while (clock() < start_time + millis);
}

// struct for making file handling easier and function for loading files safely (I think this is using GNU extensions,
// so if you compile this with something else then you're on your own)

typedef struct
{
    char * filedata;
    size_t filesize;
} loaded_file;

void load_file(char * filename, loaded_file * dest, char * method)
{
    FILE * file;
    static const char files_dir[] = "www";
    size_t fsize, rv;
    char * buf;
    char filename_qualified[strlen(files_dir) + strlen(filename) + 2];
    
    sprintf(filename_qualified, "%s/%s", files_dir, filename);
    file = fopen(filename_qualified, method);
    
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

// data type and enum definitions for all the files that the site will handle

typedef enum
{
    TERMINATOR = 0,
    BIN,
    TEXT,
    NOPE,
    REDIR,
} ContentType;

typedef struct
{
    char * filepath;
    char * serverpath;
    char * mime;
    loaded_file file;
    ContentType type;
    unsigned short status;
} UsedFile;

UsedFile files[] = {
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

unsigned int time_to_brew = 1800;
unsigned short port = 80;

// stuff for threads that I copied from another implementation

unsigned char closeThread = 0;
pthread_t HWThread;
int sockfd;

void sig_handler(int signo)
{
    closeThread = 1;
    close(sockfd);
    pthread_join(HWThread, NULL);
    exit(0);
}

// struct for storing info about the current coffee pot; this could probably be made const but eh

typedef struct
{
    unsigned char ready : 1;
    unsigned char is_brewing : 1;
    unsigned char is_pouring_milk : 1;
    unsigned char is_teapot : 1;
    unsigned char milk_available : 1;
    time_t lastbrew;
} pot;

pot potinfo;

// brew a cup of coffee

unsigned char brew(void)
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

unsigned char say_when(void)
{
    if (potinfo.is_pouring_milk == 1)
    {
        potinfo.is_pouring_milk = 0;
        return 0;
    }
    return 1;
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
    static const char format[] = "HTTP/1.1 %d %s\n%s";
    static const Code errorcode = { 500, "Internal server error" };
    size_t msgLen;
    const Code * cur_code = codes;
    
    while (cur_code->status != status && cur_code->status != 0xFFFF)
        cur_code++;
    
    if (cur_code->status == 0xFFFF)
        cur_code = &errorcode;
    
    msgLen = snprintf(NULL, 0, format, cur_code->status, cur_code->message, message) + 1;
    
    response = malloc(msgLen);
    if (response == NULL)
    {
        error("Out of memory");
    }
    
    snprintf(response, msgLen, format, cur_code->status, cur_code->message, message);
    
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

void * hardwareHandler(void * arg)
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
            load_file(cur_file->filepath, &cur_file->file, (cur_file->type == TEXT) ? "r" : "rb");
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
    
    // comment out this line to stop it running as a daemon
    //daemon(1, 0);
    pthread_create(&HWThread, NULL, hardwareHandler, NULL);
    
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
        
        /*if (n < 0) 
        {
            error("ERROR writing to socket");
        }*/
        close(newsockfd);
    }
    
    close(sockfd);
    return 0;
}
