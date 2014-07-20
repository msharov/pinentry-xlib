// This file is part of the pinentry-x11 project
//
// Copyright (c) 2014 by Mike Sharov <msharov@users.sourceforge.net>
// This file is free software, distributed under the MIT License.

#include "xdlg.h"

int main (int argc, char* argv[])
{
    _argc = argc;
    _argv = (const char* const*) argv;
    _description = "Please enter the passphrase to\nunlock the public key John Doe <jdoe@aol.com>\nFingerprint 08 D7 A9 BB";
    snprintf (_prompt, sizeof(_prompt), "%s:", "Passphrase");
    if (!OpenX (NULL)) {
	printf ("ERR Unable to open X display\n");
	return (EXIT_FAILURE);
    }
    bool accepted = RunMainDialog();
    if (_dialogType == PromptForPassword) {
	if (accepted)
	    printf ("Entered password: \"%s\"\n", _password);
	else
	    puts ("Entry cancelled");
    } else if (_dialogType == AskYesNoQuestion)
	puts (accepted ? "Yes" : "No");
    return (EXIT_SUCCESS);
}
