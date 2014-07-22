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
static void RunAssuanProtocol (void);

//----------------------------------------------------------------------

int main (int argc, char* argv[])
{
    InstallCleanupHandler();
    ParseCommandLine (argc, argv);
    if (!_askpassMode)
	RunAssuanProtocol();
    else if (RunMainDialog())
	puts (_password);
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
    // These are the same as gpg's pinentry plus the description option as for ssh-askpass
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

enum ECmd {
    cmd_BYE,
    cmd_CONFIRM,
    cmd_GETINFO,
    cmd_GETPIN,
    cmd_MESSAGE,
    cmd_OPTION,
    cmd_SETDESC,
    cmd_SETPROMPT,
    cmd_SETQUALITYBAR,
    cmd_SETTIMEOUT,
    cmd_SETTITLE,
    cmd_SETCANCEL,
    cmd_SETERROR,
    cmd_SETNOTOK,
    cmd_SETOK,
    cmd_SETQUALITYBAR_TT,
    cmd_NCMDS
};

static enum ECmd MatchCommand (const char* l)
{
    static const char* c_Cmds [cmd_NCMDS] = {	// Parallel to ECmd
	"BYE",
	"CONFIRM",
	"GETINFO",
	"GETPIN",
	"MESSAGE",
	"OPTION",
	"SETDESC",
	"SETPROMPT",
	"SETQUALITYBAR",
	"SETTIMEOUT",
	"SETTITLE",
	"SETCANCEL",
	"SETERROR",
	"SETNOTOK",
	"SETOK",
	"SETQUALITYBAR_TT"
    };
    unsigned i;
    for (i = 0; i < cmd_NCMDS; ++i)
	if (0 == strncasecmp (c_Cmds[i], l, strlen(c_Cmds[i])))
	    break;
    return (i);
}

static void RunAssuanProtocol (void)
{
    puts ("OK Your orders please");
    char line [ASSUAN_LINE_LIMIT+2];
    while (fgets (line, sizeof(line), stdin)) {
	size_t linelen = strlen(line);
	if (line[linelen-1] != '\n') {
	    puts ("ERR line too long");
	    break;
	}
	line[linelen-1] = 0;
	const char* arg = strchr (line, ' ');
	arg += !!arg;

	enum ECmd cmd = MatchCommand (line);
	if (cmd == cmd_BYE) {
	    puts ("OK closing connection");
	    break;
	} else if (cmd == cmd_CONFIRM) {
	    _dialogType = AskYesNoQuestion;
	    bool accepted = RunMainDialog();
	    puts (accepted ? "OK" : "ERR 83886179 cancelled");
	} else if (cmd == cmd_GETPIN) {
	    _dialogType = PromptForPassword;
	    bool accepted = RunMainDialog();
	    if (accepted)
		printf ("D %s\nOK\n", _password);
	    else
		puts ("ERR 83886179 cancelled");
	    memset (_password, _passwordLen = 0, sizeof(_password));
	} else if (cmd == cmd_GETINFO && arg) {
	    if (strcasecmp (arg, "version"))
		puts ("D " PINENTRY_VERSTRING "\nOK");
	    else if (strcasecmp (arg, "pid"))
		printf ("D %u\nOK\n", getpid());
	    else
		puts ("ERR 83886355 unknown command");
	} else if (cmd == cmd_MESSAGE) {
	    _dialogType = ShowMessage;
	    RunMainDialog();
	    puts ("OK");
	} else if (cmd == cmd_OPTION && arg) {
	    const char* value = strchr (arg, '=');
	    value += !!value;
	    if (!strcasecmp (arg, "no-grab"))
		_nograb = true;
	    else if (!strcasecmp (arg, "grab"))
		_nograb = false;
	    else if (!strcasecmp (arg, "parent-wid") && value)
		_parentWindow = atoi (value);
	    else if (!strcasecmp (arg, "display") && value) {
		char* p = strdup (value);
		if (p) {
		    if (_displayName)
			free (_displayName);
		    _displayName = p;
		}
	    }
	    puts ("OK");
	} else if (cmd == cmd_SETDESC && arg) {
	    char* p = strdup (arg);
	    if (p) {
		if (_description)
		    free (_description);
		_description = p;
	    }
	    puts ("OK");
	} else if (cmd == cmd_SETPROMPT && arg) {
	    snprintf (_prompt, sizeof(_prompt), "%s:", arg);
	    puts ("OK");
	} else if (cmd == cmd_SETQUALITYBAR) {
	    _confirms = true;
	    if (!_prompt[0])
		strcpy (_prompt, "Passphrase:");
	    puts ("OK");
	} else if (cmd == cmd_SETTIMEOUT && arg) {
	    _entryTimeout = atoi(arg);
	    puts ("OK");
	} else if (cmd < cmd_NCMDS)	// SETTITLE and later commands are not supported in this implementation and are silently ignored
	    puts ("OK");
	else
	    puts ("ERR 83886355 unknown command");
    }
}
