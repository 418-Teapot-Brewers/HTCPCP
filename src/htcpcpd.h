#pragma once

#include <stddef.h>
#include <time.h>

typedef struct
{
    unsigned short status;
    char message[32];
} Code;

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

typedef struct
{
    unsigned char ready : 1;
    unsigned char is_brewing : 1;
    unsigned char is_pouring_milk : 1;
    unsigned char is_teapot : 1;
    unsigned char milk_available : 1;
    time_t lastbrew;
} pot;

void load_file(char * filename, loaded_file * dest);
void error(const char * msg);
