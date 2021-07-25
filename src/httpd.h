#pragma once

#include <stddef.h>

typedef struct
{
    char * filedata;
    size_t filesize;
} loaded_file;

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

void load_file(char * filename, loaded_file * dest, unsigned char qualify, char * method);

void error(const char * msg);

extern char * files_dir;
extern unsigned short port;
extern char * protocol;
