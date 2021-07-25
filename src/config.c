#include "config.h"
#include "httpd.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const Variable variables[] = {
    { STRING, &files_dir, "folder" },
    { USHORT, &port, "port" },
    { STRING, &protocol, "protocol" },
    { END, NULL, "Terminator" },
};

void assignVariable(char * name, char * value)
{
    const Variable * cur_var = variables;
    
    while (cur_var->type != END)
    {
        if (strcmp(name, cur_var->name))
        {
            switch (cur_var->type)
            {
                case USHORT:
                {
                    *(unsigned short *)cur_var->ptr = strtoul(value, NULL, 0);
                    break;
                }
                case STRING:
                {
                    char * newValue = strdup(value);
                    if (newValue == NULL)
                    {
                        error("Out of memory");
                    }
                    
                    *(char **)cur_var->ptr = newValue;
                    break;
                }
                case END:
                {
                    error("You screwed up the variable table. Not sure how, since you somehow managed to break the termination condition.");
                    break;
                }
            }
        }
        cur_var++;
    }
}

UsedFile * load_config_file(void)
{
    loaded_file conf;
    ConfigState state = VARIABLE;
    char configBuf[256];
    char variableName[256];
    size_t index = 0;
    
    UsedFile * rv;
    size_t num_loaded_files = 0;
    size_t max_loaded_files = 1;
    unsigned char doLoop = 1;
    
    load_file("server.conf", &conf, 0, "r");
    
    if (conf.filesize == 0)
    {
        error("Could not load file server.conf");
        return NULL;
    }
    
    rv = malloc(max_loaded_files * sizeof(UsedFile));
    
    if (rv == NULL)
    {
        free(conf.filedata);
        error("Out of memory");
        return NULL;
    }
    
    while (doLoop == 1 && index < conf.filesize)
    {
        for (unsigned short i = 0; i < 256; i++)
        {
            switch (state)
            {
                case VARIABLE:
                {
                    switch (conf.filedata[index])
                    {
                        case ';':
                        {
                            state = COMMENT;
                            break;
                        }
                        
                        case '=':
                        {
                            configBuf[i] = '\0';
                            
                            strcpy(variableName, configBuf);
                            
                            state = VALUE;
                            i = 256;
                            break;
                        }
                        
                        case ' ': // state was actually FILETYPE not VARIABLE
                        {
                            if (num_loaded_files >= max_loaded_files)
                            {
                                max_loaded_files *= 2;
                                rv = realloc(rv, max_loaded_files * sizeof(UsedFile));
                                
                                if (rv == NULL)
                                {
                                    free(conf.filedata);
                                    error("Out of memory");
                                    return NULL;
                                }
                            }
                        
                            if (strncmp(configBuf, "BIN", i) == 0)
                                rv[num_loaded_files].type = BIN;
                            else if (strncmp(configBuf, "TEXT", i) == 0)
                                rv[num_loaded_files].type = TEXT;
                            else if (strncmp(configBuf, "NOPE", i) == 0)
                                rv[num_loaded_files].type = NOPE;
                            else if (strncmp(configBuf, "REDIR", i) == 0)
                                rv[num_loaded_files].type = REDIR;
                            
                            rv[num_loaded_files].serverpath = NULL;
                            rv[num_loaded_files].filepath = NULL;
                            rv[num_loaded_files].mime = NULL;
                            rv[num_loaded_files].status = 0;
                            
                            num_loaded_files++;
                            
                            state = LOCALPATH;
                            i = 256;
                            break;
                        }
                        
                        case '\n':
                        case '\r':
                        {
                            state = VARIABLE;
                            i = 256;
                            break;
                        }
                        
                        case '\0':
                        {
                            doLoop = 0;
                            i = 256;
                            break;
                        }
                        
                        default:
                        {
                            configBuf[i] = conf.filedata[index];
                        }
                    }
                    
                    index++;
                    
                    break;
                }
                
                case VALUE:
                {
                    switch (conf.filedata[index])
                    {
                        case ';':
                        {
                            state = COMMENT;
                            break;
                        }
                        
                        case '=':
                        {
                            free(conf.filedata);
                            error("Unexpected = in variable assignment");
                            return NULL;
                        }
                        
                        case ' ':
                        case '\n':
                        case '\r':
                        {
                            configBuf[i] = '\0';
                            assignVariable(variableName, configBuf);
                            
                            state = VARIABLE;
                            i = 256;
                            break;
                        }
                        
                        case '\0':
                        {
                            configBuf[i] = '\0';
                            assignVariable(variableName, configBuf);
                            
                            doLoop = 0;
                            i = 256;
                            break;
                        }
                        
                        default:
                        {
                            configBuf[i] = conf.filedata[index];
                        }
                    }
                    
                    index++;
                    
                    break;
                }
                
                case LOCALPATH:
                {
                    switch (conf.filedata[index])
                    {
                        case ';':
                        {
                            state = COMMENT;
                            break;
                        }
                        
                        case '=':
                        {
                            free(conf.filedata);
                            error("Unexpected = in file listing");
                            return NULL;
                        }
                        
                        case ' ':
                        {
                            char * localpath;
                            
                            configBuf[i] = '\0';
                            
                            localpath = strdup(configBuf);
                            if (localpath == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].filepath = localpath;
                            
                            state = SERVERPATH;
                            i = 256;
                            break;
                        }
                        
                        case '\n':
                        case '\r':
                        {
                            char * localpath;
                            
                            configBuf[i] = '\0';
                            
                            localpath = strdup(configBuf);
                            if (localpath == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].filepath = localpath;
                            
                            state = VARIABLE;
                            i = 256;
                            break;
                        }
                        
                        case '\0':
                        {
                            char * localpath;
                            
                            configBuf[i] = '\0';
                            
                            localpath = strdup(configBuf);
                            if (localpath == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].filepath = localpath;
                            
                            doLoop = 0;
                            i = 256;
                            break;
                        }
                        
                        default:
                        {
                            configBuf[i] = conf.filedata[index];
                        }
                    }
                    
                    index++;
                    
                    break;
                }
                
                case SERVERPATH:
                {
                    switch (conf.filedata[index])
                    {
                        case ';':
                        {
                            state = COMMENT;
                            break;
                        }
                        
                        case '=':
                        {
                            free(conf.filedata);
                            error("Unexpected = in file listing");
                            return NULL;
                        }
                        
                        case ' ':
                        {
                            char * serverpath;
                            
                            configBuf[i] = '\0';
                            
                            serverpath = strdup(configBuf);
                            if (serverpath == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].serverpath = serverpath;
                            
                            state = MIME;
                            i = 256;
                            break;
                        }
                        
                        case '\n':
                        case '\r':
                        {
                            char * serverpath;
                            
                            configBuf[i] = '\0';
                            
                            serverpath = strdup(configBuf);
                            if (serverpath == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].serverpath = serverpath;
                            
                            state = VARIABLE;
                            i = 256;
                            break;
                        }
                        
                        case '\0':
                        {
                            char * serverpath;
                            
                            configBuf[i] = '\0';
                            
                            serverpath = strdup(configBuf);
                            if (serverpath == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].serverpath = serverpath;
                            
                            doLoop = 0;
                            i = 256;
                            break;
                        }
                        
                        default:
                        {
                            configBuf[i] = conf.filedata[index];
                        }
                    }
                    
                    index++;
                    
                    break;
                }
                
                case MIME:
                {
                    switch (conf.filedata[index])
                    {
                        case ';':
                        {
                            state = COMMENT;
                            break;
                        }
                        
                        case '=':
                        {
                            free(conf.filedata);
                            error("Unexpected = in file listing");
                            return NULL;
                        }
                        
                        case ' ':
                        {
                            char * mime;
                            
                            configBuf[i] = '\0';
                            
                            mime = strdup(configBuf);
                            if (mime == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].mime = mime;
                            
                            state = STATUS;
                            i = 256;
                            break;
                        }
                        
                        case '\n':
                        case '\r':
                        {
                            char * mime;
                            
                            configBuf[i] = '\0';
                            
                            mime = strdup(configBuf);
                            if (mime == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].mime = mime;
                            
                            state = VARIABLE;
                            i = 256;
                            break;
                        }
                        
                        case '\0':
                        {
                            char * mime;
                            
                            configBuf[i] = '\0';
                            
                            mime = strdup(configBuf);
                            if (mime == NULL)
                            {
                                free(conf.filedata);
                                error("Out of memory");
                                return NULL;
                            }
                            
                            rv[num_loaded_files - 1].mime = mime;
                            
                            doLoop = 0;
                            i = 256;
                            break;
                        }
                        
                        default:
                        {
                            configBuf[i] = conf.filedata[index];
                        }
                    }
                    
                    index++;
                    
                    break;
                }
                
                case STATUS:
                {
                    switch (conf.filedata[index])
                    {
                        case ';':
                        {
                            state = COMMENT;
                            break;
                        }
                        
                        case '=':
                        {
                            free(conf.filedata);
                            error("Unexpected = in file listing");
                            return NULL;
                        }
                        
                        case ' ':
                        case '\n':
                        case '\r':
                        {
                            configBuf[i] = '\0';
                            
                            rv[num_loaded_files - 1].status = strtoul(configBuf, NULL, 0);
                            
                            state = VARIABLE;
                            i = 256;
                            break;
                        }
                        
                        case '\0':
                        {
                            configBuf[i] = '\0';
                            
                            rv[num_loaded_files - 1].status = strtoul(configBuf, NULL, 0);
                            
                            doLoop = 0;
                            i = 256;
                            break;
                        }
                        
                        default:
                        {
                            configBuf[i] = conf.filedata[index];
                        }
                    }
                    
                    index++;
                    
                    break;
                }
                
                case COMMENT:
                {
                    while (conf.filedata[index] != '\r' && conf.filedata[index] != '\n' && index < conf.filesize)
                        index++;
                    
                    state = VARIABLE;
                    i = 256;
                    break;
                }
            }
        }
    }
    
    for (size_t i = 0; i < num_loaded_files; i++)
    {
        #if DEBUG
        printf("Using file %s\n", rv[i].filepath);
        #endif
        
        if (rv[i].serverpath == NULL)
        {
            char * serverpath = malloc(strlen(rv[i].filepath) + 2);
            if (serverpath == NULL)
            {
                error("Out of memory");
            }
            
            sprintf(serverpath, "/%s", rv[i].filepath);
            rv[i].serverpath = serverpath;
        }
        
        if (rv[i].mime == NULL && (rv[i].type == TEXT || rv[i].type == BIN))
        {
            char * mime = strdup((rv[i].type == TEXT) ? "text/html" : "application/octet-stream");
            if (mime == NULL)
            {
                error("Out of memory");
            }
            
            rv[i].mime = mime;
        }
        
        if (rv[i].status == 0)
        {
            switch (rv[i].type)
            {
                case TEXT:
                case BIN:           { rv[i].status = 200; break; }
                case REDIR:         { rv[i].status = 301; break; }
                case NOPE:          { rv[i].status = 410; break; }
                case TERMINATOR:    { error("There is something *seriously* wrong with your config file"); break; }
            }
        }
    }
    
    if (num_loaded_files >= max_loaded_files)
    {
        max_loaded_files *= 2;
        rv = realloc(rv, max_loaded_files * sizeof(UsedFile));
        
        if (rv == NULL)
        {
            free(conf.filedata);
            error("Out of memory");
            return NULL;
        }
    }

    rv[num_loaded_files].type = TERMINATOR;
    rv[num_loaded_files].serverpath = NULL;
    rv[num_loaded_files].filepath = "Terminator";
    rv[num_loaded_files].mime = NULL;
    rv[num_loaded_files].status = 0;
    
    free(conf.filedata);
    
    return rv;
}
