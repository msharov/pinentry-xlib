// This file is part of the pinentry-x11 project
//
// Copyright (c) 2014 by Mike Sharov <msharov@users.sourceforge.net>
// This file is free software, distributed under the MIT License.

#pragma once
#include "config.h"
#include <stdbool.h>

//----------------------------------------------------------------------

enum {
    PASSWORD_MAXLEN = 128,
    PROMPT_MAXLEN = 16
};

typedef enum {
    PromptForPassword,
    ShowMessage,
    AskYesNoQuestion
} edlgtype_t;

//----------------------------------------------------------------------

// Parameters for X window creation
extern int _argc;
extern const char* const* _argv;

// Pinentry dialog parameters
extern edlgtype_t _dialogType;
extern char* _description;
extern char _prompt [PROMPT_MAXLEN];
extern unsigned _confirms;

// Dialog return value
extern char _password [PASSWORD_MAXLEN];
extern size_t _passwordLen;

//----------------------------------------------------------------------

bool OpenX (const char* displayName);
bool RunMainDialog (void);
