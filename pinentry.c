// This file is part of the pinentry-x11 project
//
// Copyright (c) 2014 by Mike Sharov <msharov@users.sourceforge.net>
// This file is free software, distributed under the MIT License.

#include "xdlg.h"
#include <getopt.h>
#include <signal.h>

//----------------------------------------------------------------------

static bool _askpassMode = false;	// If using the ssh-askpass interface

//----------------------------------------------------------------------

static void OnSignal (int sig);
static void InstallCleanupHandler (void);
static void ParseCommandLine (int argc, char* argv[]);
static void PrintHelp (void);

//----------------------------------------------------------------------

int main (int argc, char* argv[])
{
    InstallCleanupHandler();
    ParseCommandLine (argc, argv);
    if (!OpenX()) {
	printf ("ERR Unable to open X display %s\n", _displayName);
	return (EXIT_FAILURE);
    }
    bool accepted = RunMainDialog();
    if (_dialogType == PromptForPassword) {
	if (_askpassMode) {
	    if (accepted)
		puts (_password);
	} else {
	    if (accepted)
		printf ("D %s\nOK\n", _password);
	    else
		puts ("ERR 83886179 cancelled");
	}
    } else if (_dialogType == AskYesNoQuestion)
	puts (accepted ? "OK" : "ERR 83886179 cancelled");
    else if (_dialogType == ShowMessage)
	puts ("OK");
    return (EXIT_SUCCESS);
}

static void OnSignal (int sig)
{
    printf ("ERR %s\n", strsignal(sig));
    fflush (stdout);
    abort();
}

static void InstallCleanupHandler (void)
{
    #define S(n) (1<<(n))
    const unsigned c_Sigmask = S(SIGINT)|S(SIGQUIT)|S(SIGTERM)|S(SIGPWR)|S(SIGILL)
				|S(SIGBUS)|S(SIGFPE)|S(SIGSYS)|S(SIGSEGV)|S(SIGXCPU);
    #undef S
    for (unsigned i = 0; i < NSIG; ++i)
	if ((1<<i) & c_Sigmask)
	    signal (i, OnSignal);
}

static void ParseCommandLine (int argc, char* argv[])
{
    static const struct option c_LongOpts[] = {
	{ "version",		no_argument,		0, 'v' },
	{ "help",		no_argument,		0, 'h' },
	{ "no-global-grab",	no_argument,		0, 'g' },
	{ "parent-wid",		required_argument,	0, 'w' },
	{ "timeout",		required_argument,	0, 't' },
	{ "display",		required_argument,	0, 'd' },
	{ "ttyname",		required_argument,	0, 0 },
	{ "ttytype",		required_argument,	0, 0 },
	{ "lc-ctype",		required_argument,	0, 0 },
	{ "lc-messages",	required_argument,	0, 0 },
	{ NULL,			0,			0, 0 }
    };
    for (int oi = 0, c; 0 <= (c = getopt_long (argc, argv, "vhg", c_LongOpts, &oi));) {
	if (c == 'v') {
	    puts ("pinentry-xlib " PINENTRY_VERSTRING);
	    exit (EXIT_SUCCESS);
	} else if (c == 'h' || c == '?') {
	    PrintHelp();
	    exit (EXIT_SUCCESS);
	} else if (c == 'g')
	    _nograb = true;
	else if (c == 'w')
	    _parentWindow = atoi (optarg);
	else if (c == 't')
	    _entryTimeout = atoi (optarg);
	else if (c == 'd')
	    _displayName = strdup (optarg);
    }
    if (optind+1 == argc) {
	_description = strdup (argv[optind]);
	_askpassMode = true;
    } else
	_description = strdup (DEFAULT_DESCRIPTION);
    _argc = argc;
    _argv = (const char* const*) argv;
}

static void PrintHelp (void)
{
    // These are the same as gpg's pinentry plus the options for ssh-askpass
    puts ("Usage: pinentry-xlib [OPTIONS] [DESCRIPTION]\n"
	"Ask securely for a secret and print it to stdout.\n\n"
	"      --display DISPLAY Set the X display\n"
	"      --ttyname PATH    Set the tty terminal node name\n"
	"      --ttytype NAME    Set the tty terminal type\n"
	"      --lc-ctype        Set the tty LC_CTYPE value\n"
	"      --lc-messages     Set the tty LC_MESSAGES value\n"
	"      --timeout SECS    Timeout waiting for input after this many seconds\n"
	"  -g, --no-global-grab  Grab keyboard only while window is focused\n"
	"      --parent-wid      Parent window ID (for positioning)\n"
	"  -d, --debug           Turn on debugging output\n"
	"  -h, --help            Display this help and exit\n"
	"      --version         Output version information and exit");
}
