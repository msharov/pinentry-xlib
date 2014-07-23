// This file is part of the pinentry-xlib project
//
// Copyright (c) 2014 by Mike Sharov <msharov@users.sourceforge.net>
// This file is free software, distributed under the MIT License.

#include "xdlg.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <signal.h>

//----------------------------------------------------------------------
// Extern interface set from main

int _argc = 0;
const char* const* _argv = NULL;
char* _displayName = NULL;

edlgtype_t _dialogType = PromptForPassword;
char* _description = NULL;
char _prompt [PROMPT_MAXLEN] = DEFAULT_PASSWORD_PROMPT;
unsigned _confirms = 0;
unsigned _parentWindow = 0;
unsigned _entryTimeout = 0;
bool _nograb = false;

char _password [PASSWORD_MAXLEN] = "";
size_t _passwordLen = 0;

//----------------------------------------------------------------------
// Module internal variables

// X server information
static Display* _display = NULL;
static int _screen = 0;
static bool _isGrabbed = false;

enum {
    a_ATOM,
    a_STRING,
    a_CARDINAL,
    a_WM_CLIENT_MACHINE,
    a_WM_PROTOCOLS,
    a_WM_DELETE_WINDOW,
    a_NET_WM_PID,
    a_NET_WM_STATE,
    a_NET_WM_STATE_NORMAL,
    a_NET_WM_STATE_MODAL,
    a_NET_WM_STATE_DEMANDS_ATTENTION,
    a_NET_WM_STATE_ABOVE,
    a_NET_WM_WINDOW_TYPE,
    a_NET_WM_WINDOW_TYPE_DIALOG,
    a_NAtoms
};
static Atom _atoms [a_NAtoms] = { None };

// Pinentry window
static Window _w = 0;
static GC _gc = None;
static XFontStruct* _font = NULL;
static XFontStruct* _wfontinfo = NULL;
static unsigned long _fg = 0, _bg = 0;
static unsigned _wwidth = 0;
static unsigned _wheight = 0;

typedef struct {
    unsigned x, y;
} point_t;

static struct {
    point_t	f;
    point_t	fl;
    point_t	desc;
    point_t	descsz;
    point_t	prompt;
    point_t	box;
    point_t	confirmprompt;
    point_t	confirmbox;
    unsigned	promptw;
    unsigned	confirmpromptw;
} _wl;

// Entry runtime information
enum {
    MAX_QUALITY = 128,
    MAX_BOXES_POW = 4,
    MAX_BOXES = 1<<MAX_BOXES_POW	// The number of password char placeholder boxes visible
};
static char _confirmPrompt [PROMPT_MAXLEN] = "";
static char _confirmBuf [PASSWORD_MAXLEN] = "";
static size_t _confirmBufLen = 0;
static unsigned _confirmsPass = 0;
static bool _accepted = false;
static bool _timedOut = false;

//----------------------------------------------------------------------
// Module internal functions

static bool OpenX (void);
static void CloseX (void);
static int OnXlibError (Display* dpy, XErrorEvent* e);
static int OnXlibIOError (Display* dpy);
static void OnAlarm (int sig);

static void CreatePinentryWindow (void);
static void ClosePinentryWindow (void);
static void LayoutWindow (void);
static void DrawWindow (void);
static void DrawPasswordBoxLine (unsigned x, unsigned y, unsigned pwlen);
static unsigned ComputeQuality (void);
static bool OnKey (wchar_t k);

#define STRBLK(s)	s,strlen(s)

//----------------------------------------------------------------------
// X connection management

static bool OpenX (void)
{
    // Open display
    _display = XOpenDisplay (_displayName);
    if (!_display)
	return (false);
    atexit (CloseX);
    signal (SIGALRM, OnAlarm);
    XSetErrorHandler (OnXlibError);
    XSetIOErrorHandler (OnXlibIOError);
    _screen = DefaultScreen (_display);
    // Allocate colors
    const char* fgname = XGetDefault (_display, PINENTRY_NAME, "foreground");
    XColor color, dbcolor;
    if (fgname && XAllocNamedColor (_display, DefaultColormap (_display,_screen), fgname, &color, &dbcolor))
	_fg = color.pixel;
    else
	_fg = WhitePixel(_display, _screen);
    const char* bgname = XGetDefault (_display, PINENTRY_NAME, "background");
    if (bgname && XAllocNamedColor (_display, DefaultColormap (_display,_screen), bgname, &color, &dbcolor))
	_bg = color.pixel;
    else
	_bg = BlackPixel(_display, _screen);
    // Load font
    const char* fontname = XGetDefault (_display, PINENTRY_NAME, "font");
    if (!fontname)
	fontname = DEFAULT_FONT_NAME;
    _font = XLoadQueryFont (_display, fontname);
    // Get Atom ids needed to create a window
    //{{{ c_AtomNames - parallel to enum
    static const char* c_AtomNames [a_NAtoms] = {
	"ATOM",
	"STRING",
	"CARDINAL",
	"WM_CLIENT_MACHINE",
	"WM_PROTOCOLS",
	"WM_DELETE_WINDOW",
	"_NET_WM_PID",
	"_NET_WM_STATE",
	"_NET_WM_STATE_NORMAL",
	"_NET_WM_STATE_MODAL",
	"_NET_WM_STATE_DEMANDS_ATTENTION",
	"_NET_WM_STATE_ABOVE",
	"_NET_WM_WINDOW_TYPE",
	"_NET_WM_WINDOW_TYPE_DIALOG"
    };
    //}}}
    XInternAtoms (_display, (char**) c_AtomNames, a_NAtoms, false, _atoms);
    return (true);
}

static void CloseX (void)
{
    if (_display) {
	ClosePinentryWindow();
	if (_font)
	    XFreeFont (_display, _font);
	XCloseDisplay (_display);
    }
    if (_displayName)
	free (_displayName);
    if (_description)
	free (_description);
    _font = NULL;
    _display = NULL;
    _displayName = NULL;
    _description = NULL;
}

static void OnAlarm (int sig UNUSED)
{
    _timedOut = true;
    // RunMainDialog is stuck in XNextEvent, so need to get the server to send some kind of message
    XResizeWindow (_display, _w, 1, 1);	// Synchronous calls will flush the fd, so need an async call
    XFlush (_display);
}

static int OnXlibError (Display* dpy, XErrorEvent* e)
{
    char errorbuf [512];
    XGetErrorText (dpy, e->error_code, errorbuf, sizeof(errorbuf));
    printf ("ERR X request %u.%u error: %s\n", e->request_code, e->minor_code, errorbuf);
    _timedOut = true;
    return (0);
}

static int OnXlibIOError (Display* dpy UNUSED)
{
    _w = None;
    _display = NULL;
    puts ("ERR connection to X server terminated");
    exit (EXIT_FAILURE);
    return (0);
}

//----------------------------------------------------------------------
// Pinentry main dialog

bool RunMainDialog (void)
{
    if (!_display && !OpenX()) {
	printf ("ERR Unable to open X display %s\n", _displayName);
	return (false);
    }
    CreatePinentryWindow();
    for (XEvent e; !_timedOut;) {
	if (0 > XNextEvent (_display, &e))
	    break;
	if (e.xany.window != _w)
	    continue;
	if (e.type == ConfigureNotify) {
	    if (_wwidth != (unsigned) e.xconfigure.width || _wheight != (unsigned) e.xconfigure.height) {
		_wwidth = e.xconfigure.width;
		_wheight = e.xconfigure.height;
		DrawWindow();
	    }
	} else if (e.type == Expose) {
	    while (XCheckTypedEvent (_display, Expose, &e)) {}
	    DrawWindow();
	    if (_dialogType == PromptForPassword && !_nograb && !_isGrabbed) {
		if (GrabSuccess != XGrabKeyboard (_display, _w, true, GrabModeAsync, GrabModeAsync, CurrentTime)) {
		    puts ("ERR failed to grab the keyboard");
		    break;
		}
		_isGrabbed = true;
		XGrabServer (_display);
	    }
	} else if (e.type == DestroyNotify) {
	    _w = None;
	    break;
	} else if (e.type == KeyPress) {
	    KeySym ksym = 0;
	    XLookupString (&e.xkey, NULL, 0, &ksym, NULL);
	    if (OnKey (ksym))
		break;
	} else if (e.type == ButtonPress
		|| (e.type == ClientMessage
		    && (Atom) e.xclient.data.l[0] == _atoms[a_WM_DELETE_WINDOW]))
	    break;
    }
    ClosePinentryWindow();
    const bool r = _accepted;
    _accepted = false;	// Reset for next time
    return (r);
}

static void CreatePinentryWindow (void)
{
    _w = XCreateSimpleWindow (_display, RootWindow(_display, _screen), 0, 0, 1, 1, 0, _fg, _bg);

    XSelectInput (_display, _w, ExposureMask| KeyPressMask| ButtonPressMask| StructureNotifyMask);

    // Create and setup the GC
    _gc = XCreateGC (_display, _w, 0, NULL);
    XSetForeground (_display, _gc, _fg);

    if (_font)
	XSetFont (_display, _gc, _font->fid);
    _wfontinfo = XQueryFont (_display, XGContextFromGC (_gc));
    if (!_wfontinfo) {
	printf ("ERR No fonts available\n");
	exit (EXIT_FAILURE);
    }

    // Now layout the controls, measuring the actual necessary window size
    LayoutWindow();
    XResizeWindow (_display, _w, _wwidth, _wheight);

    // Setup properties for the window manager
    // The size hints
    XSizeHints szHints;
    szHints.flags = PMinSize| PMaxSize| PWinGravity;
    szHints.min_width = _wwidth;
    szHints.min_height = _wheight;
    szHints.max_width = _wwidth;
    szHints.max_height = _wheight;
    szHints.win_gravity = CenterGravity;
    XSetStandardProperties (_display, _w, PINENTRY_NAME, PINENTRY_NAME, None, (char**) _argv, _argc, &szHints);
    // Hostname
    char hostname [HOST_NAME_MAX];
    if (0 == gethostname (hostname, sizeof(hostname)))
	XChangeProperty (_display, _w, _atoms[a_WM_CLIENT_MACHINE], _atoms[a_STRING], 8, PropModeReplace, (const unsigned char*) STRBLK(hostname));
    // Process id
    unsigned int pid = getpid();
    XChangeProperty (_display, _w, _atoms[a_NET_WM_PID], _atoms[a_CARDINAL], 32, PropModeReplace, (const unsigned char*) &pid, 1);
    // WM_PROTOCOLS (to use the close button)
    XChangeProperty (_display, _w, _atoms[a_WM_PROTOCOLS], _atoms[a_ATOM], 32, PropModeReplace, (const unsigned char*) &_atoms[a_WM_DELETE_WINDOW], 1);
    // That this is a system-wide modal dialog
    if (!_parentWindow)	// Parent window is only used for the transient for hint. The dialog itself is always a toplevel window, parented to root.
	_parentWindow = RootWindow (_display, _screen);
    XSetTransientForHint (_display, _w, _parentWindow);
    // _NET_WM_WINDOW_TYPE set to DIALOG
    XChangeProperty (_display, _w, _atoms[a_NET_WM_WINDOW_TYPE], _atoms[a_ATOM], 32, PropModeReplace, (const unsigned char*) &_atoms[a_NET_WM_WINDOW_TYPE_DIALOG], 1);
    // _NET_WM_STATE set to NORMAL size, MODAL, ABOVE, and DEMANDS_ATTENTION
    XChangeProperty (_display, _w, _atoms[a_NET_WM_STATE], _atoms[a_ATOM], 32, PropModeReplace, (const unsigned char*) &_atoms[a_NET_WM_STATE_NORMAL], 4);

    // When all of the above is done, map the window
    XMapRaised (_display, _w);
}

static void ClosePinentryWindow (void)
{
    if (_wfontinfo)
	XFreeFontInfo (NULL, _wfontinfo, 0);
    _wfontinfo = NULL;
    if (_display) {
	XUngrabServer (_display);
	XUngrabKeyboard (_display, CurrentTime);
	_isGrabbed = false;
	if (_gc != None)
	    XFreeGC (_display, _gc);
	if (_w != None)
	    XDestroyWindow (_display, _w);
	XFlush (_display);
	_gc = None;
	_w = None;
    }
    alarm (0);	// Cancel entry timeout
    _timedOut = false;
}

static void LayoutWindow (void)
{
    // Window is laid out in font units
    _wl.f.x = _wfontinfo->max_bounds.width;
    _wl.f.y = _wfontinfo->ascent;
    _wl.fl.x = 3*_wl.f.x/2;
    _wl.fl.y = 3*_wl.f.y/2;
    // On top is the description of the query
    _wl.desc.x = _wl.fl.x;
    _wl.desc.y = _wl.f.y;
    _wl.descsz.x = 0;
    _wl.descsz.y = 0;
    // Measure each line and compute the bounding box
    for (const char *d = _description, *dend = d+strlen(d), *dlend; d < dend; d = dlend+1) {
	if (!(dlend = strchr (d, '\n')))
	    dlend = dend;
	unsigned lw = XTextWidth (_wfontinfo, d, dlend-d);
	if (lw > _wl.descsz.x)
	    _wl.descsz.x = lw;
	_wl.descsz.y += _wl.fl.y;
    }
    // Under that is the prompt and the password mask box line
    _wl.promptw = XTextWidth (_wfontinfo, STRBLK(_prompt));
    _wl.prompt.x = _wl.desc.x;
    _wl.box.x = _wl.prompt.x+_wl.promptw+_wl.f.x;
    _wl.box.y = _wl.desc.y+_wl.descsz.y+_wl.fl.y;
    _wl.prompt.y = _wl.box.y+_wl.f.y;
    // If confirmation is enabled (new password), add it next
    if (_confirms > 0) {
	_wl.confirmprompt.x = _wl.prompt.x;
	_wl.confirmprompt.y = _wl.prompt.y+_wl.fl.y;
	_wl.confirmpromptw = XTextWidth (_wfontinfo, STRBLK(_confirmPrompt));
	if (_confirms > 1)
	    _wl.confirmpromptw += 2*_wl.f.x;	// Add space for confirm count
	int wider = _wl.confirmpromptw - _wl.promptw;
	if (wider > 0) {
	    _wl.promptw += wider;
	    _wl.box.x += wider;
	}
	_wl.confirmbox.x = _wl.box.x;
	_wl.confirmbox.y = _wl.box.y+_wl.fl.y;
    }
    // Calculate window size
    unsigned boxlinew = _wl.box.x - _wl.prompt.x + MAX_BOXES*_wl.fl.x;
    // Width is the max of description width and the box line
    _wwidth = _wl.descsz.x;
    if (_wwidth < boxlinew)
	_wwidth = boxlinew;
    _wwidth += 2*_wl.fl.x;	// plus margin
    // Height is the sum of description and the box line, plus margins
    _wheight = _wl.box.y;
    if (_confirms > 0)
	_wheight = _wl.confirmbox.y;
    _wheight += 2*_wl.fl.y;
}

static void DrawWindow (void)
{
    // Drawing the window indicates activity, so reset the timeout
    alarm (_entryTimeout);
    // Start with a clear window
    XClearWindow (_display, _w);
    // Window border
    XDrawRectangle (_display, _w, _gc, 1, 1, _wwidth-3, _wheight-3);
    // Description
    unsigned l = 0;
    for (const char *d = _description, *dend = d+strlen(d), *dlend; d < dend; d = dlend+1) {
	if (!(dlend = strchr (d, '\n')))
	    dlend = dend;
	XDrawString (_display, _w, _gc, _wl.desc.x, _wl.desc.y+(++l)*_wl.fl.y, d, dlend-d);
    }

    // If just showing a message, the prompt line has accept instructions
    if (_dialogType == ShowMessage) {
	XDrawString (_display, _w, _gc, _wl.prompt.x, _wl.prompt.y, STRBLK(SHOW_MESSAGE_PROMPT));
	return;
    } else if (_dialogType == AskYesNoQuestion) {
	XDrawString (_display, _w, _gc, _wl.prompt.x, _wl.prompt.y, STRBLK(ASK_YES_NO_QUESTION_PROMPT));
	return;
    }

    // Prompt
    XDrawString (_display, _w, _gc, _wl.prompt.x, _wl.prompt.y, STRBLK(_prompt));
    // Password box mask
    DrawPasswordBoxLine (_wl.box.x, _wl.box.y, _passwordLen);

    // Second line for new passwords
    if (!_confirms)
	return;
    if (!_confirmsPass) {	// Quality bar
	XDrawString (_display, _w, _gc, _wl.confirmprompt.x, _wl.confirmprompt.y, STRBLK(QUALITY_PROMPT));
	const unsigned quality = ComputeQuality(), barw = (MAX_BOXES-1)*_wl.fl.x+_wl.f.x, barh = _wl.f.y;
	XDrawRectangle (_display, _w, _gc, _wl.confirmbox.x, _wl.confirmbox.y, barw-1, barh-1);
	XFillRectangle (_display, _w, _gc, _wl.confirmbox.x, _wl.confirmbox.y, quality*barw/MAX_QUALITY, barh);
	// Draw good password boundaries.
	// 56 bits is good enough against a single adversary with a GPU cracker.
	// 80 bits is good enough for all but the most sensitive stuff
	enum { BAD_QUALITY = 56, GOOD_QUALITY = 80 };
	XDrawRectangle (_display, _w, _gc, _wl.confirmbox.x + BAD_QUALITY*barw/MAX_QUALITY, _wl.confirmbox.y,
					    (GOOD_QUALITY-BAD_QUALITY)*barw/MAX_QUALITY, barh-1);
    } else {		// Confirmation prompt and boxes
	XDrawString (_display, _w, _gc, _wl.confirmprompt.x, _wl.confirmprompt.y, STRBLK(_confirmPrompt));
	DrawPasswordBoxLine (_wl.confirmbox.x, _wl.confirmbox.y, _confirmBufLen);
    }
}

static void DrawPasswordBoxLine (unsigned x, unsigned y, unsigned pwlen)
{
    const unsigned vispwlen = pwlen % MAX_BOXES, filldir = (pwlen >> MAX_BOXES_POW) & 1;
    for (unsigned bx = 0; bx < MAX_BOXES; ++bx) {
	// Rolling box line; fill boxes until the end, then clear them, then fill again
	if ((bx < vispwlen) ^ filldir)
	    XFillRectangle (_display, _w, _gc, x+bx*_wl.fl.x, y, _wl.f.x, _wl.f.y);
	else
	    XDrawRectangle (_display, _w, _gc, x+bx*_wl.fl.x, y, _wl.f.x-1, _wl.f.y-1);
    }
}

static unsigned ComputeQuality (void)
{
    // First get the charset size by checking for elements in each subset
    enum { Numbers = 1, Lowercase = 2, Uppercase = 4, Symbols = 8 };
    unsigned have = 0;
    for (unsigned i = 0; i < _passwordLen; ++i) {
	char c = _password[i];
	if (c >= '0' && c <= '9')	have |= Numbers;
	else if (c >= 'a' && c <= 'z')	have |= Lowercase;
	else if (c >= 'A' && c <= 'Z')	have |= Uppercase;
	else				have |= Symbols;
    }
    // Bits per set is: c_SetCount[] = { 10, 26, 26, 33 };
    // SetBits is a lookup table of log2(count)*16 (to avoid linking with -lm)
    // c_SetBits/16 is the set size for each character
    static const unsigned char c_SetBits[16] = { 0,53,75,83,75,83,91,95,81,87,94,98,94,98,103,105 };
    unsigned passwordBits = _passwordLen*c_SetBits[have]/16;
    return (passwordBits > MAX_QUALITY ? MAX_QUALITY : passwordBits);
}

static bool OnKey (wchar_t k)
{
    if (k == XK_Return) {
	if (_confirmsPass++ && 0 != memcmp (_password, _confirmBuf, _passwordLen))
	    ++_confirms;	// Ask again if does not match
	snprintf (_confirmPrompt, sizeof(_confirmPrompt), _confirms > 1 ? MULTI_CONFIRM_PROMPT : SINGLE_CONFIRM_PROMPT, _confirmsPass);
	memset (_confirmBuf, 0, sizeof(_confirmBuf));
	_confirmBufLen = 0;
	if (_confirmsPass > _confirms) {
	    _confirmsPass = 0;
	    return (_accepted = true);
	}
    } else if (k == XK_Escape) {
	_password[_passwordLen = 0] = 0;
	return (true);
    } else if (k == XK_BackSpace || k == XK_Delete) {
	if (_confirmsPass > 0) {
	    if (_confirmBufLen > 0)
		_confirmBuf[--_confirmBufLen] = 0;
	} else if (_passwordLen > 0)
	    _password[--_passwordLen] = 0;
    } else if (k >= ' ' && k <= '~') {
	if (_confirmsPass > 0) {
	    if (_confirmBufLen < sizeof(_confirmBuf)-1) {
		_confirmBuf[_confirmBufLen] = k;
		_confirmBuf[++_confirmBufLen] = 0;
	    }
	} else {
	    if (_passwordLen < sizeof(_password)-1) {
		_password[_passwordLen] = k;
		_password[++_passwordLen] = 0;
	    }
	}
    }
    DrawWindow();
    return (false);
}
