#pragma once

#include "httpd.h"

typedef enum
{
    VARIABLE,
    VALUE,
    //FILETYPE, (unused because of grammar)
    LOCALPATH,
    SERVERPATH,
    MIME,
    STATUS,
    COMMENT,
} ConfigState;

typedef enum
{
    END = 0,
    USHORT,
    STRING,
} VariableType;

typedef struct
{
    VariableType type;
    void * ptr;
    const char * name;
} Variable;

UsedFile * load_config_file(void);
