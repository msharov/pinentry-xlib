// This file is part of the pinentry-xlib project
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
extern char* _displayName;
extern int _argc;
extern const char* const* _argv;

// Pinentry dialog parameters
extern edlgtype_t _dialogType;
extern char* _description;
extern char _prompt [PROMPT_MAXLEN];
extern unsigned _confirms;
extern unsigned _parentWindow;
extern unsigned _entryTimeout;
extern bool _nograb;

// Dialog return value
extern char _password [PASSWORD_MAXLEN];
extern size_t _passwordLen;

//----------------------------------------------------------------------

bool RunMainDialog (void);
