#include "config.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

//----------------------------------------------------------------------

static Display* _display = NULL;
static int _screen = 0;
static Window _w = 0;
static GC _gc = None;
static XFontStruct* _font = NULL;
static XFontStruct* _wfontinfo = NULL;
static unsigned long _fg = 0, _bg = 0;
static unsigned _wwidth = 320;
static unsigned _wheight = 240;

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

//----------------------------------------------------------------------

static bool OpenX (void);
static void CloseX (void);
static void CreatePinentryWindow (int argc, char** argv);
static void ClosePinentryWindow (void);
static void RunXMainLoop (void);
static void DrawWindow (void);

//----------------------------------------------------------------------

int main (int argc, char* argv[])
{
    if (!OpenX()) {
	printf ("ERR Unable to open X display\n");
	return (EXIT_FAILURE);
    }
    CreatePinentryWindow (argc, argv);
    RunXMainLoop();
    return (EXIT_SUCCESS);
}

static bool OpenX (void)
{
    _display = XOpenDisplay (NULL);
    if (!_display)
	return (false);
    atexit (CloseX);
    _screen = DefaultScreen (_display);
    _fg = WhitePixel(_display, _screen);
    _bg = BlackPixel(_display, _screen);
    _font = XLoadQueryFont (_display, "*terminus-medium-r-normal--18*");
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
    ClosePinentryWindow();
    if (_font)
	XFreeFont (_display, _font);
    XCloseDisplay (_display);
    _font = NULL;
    _display = NULL;
}

static void CreatePinentryWindow (int argc, char** argv)
{
    _w = XCreateSimpleWindow (_display, RootWindow(_display, _screen), 0, 0, _wwidth, _wheight, 0, _fg, _bg);

    XSelectInput (_display, _w, ExposureMask| KeyPressMask| ButtonPressMask| StructureNotifyMask);

    // Setup properties for the window manager
    // The size hints
    XSizeHints szHints;
    szHints.flags = PMinSize| PMaxSize| PWinGravity;
    szHints.min_width = _wwidth;
    szHints.min_height = _wheight;
    szHints.max_width = _wwidth;
    szHints.max_height = _wheight;
    szHints.win_gravity = CenterGravity;
    XSetStandardProperties (_display, _w, PINENTRY_NAME, PINENTRY_NAME, None, argv, argc, &szHints);
    // Hostname
    char hostname [HOST_NAME_MAX];
    if (0 == gethostname (hostname, sizeof(hostname)))
	XChangeProperty (_display, _w, _atoms[a_WM_CLIENT_MACHINE], _atoms[a_STRING], 8, PropModeReplace, (const unsigned char*) hostname, strlen(hostname));
    // Process id
    unsigned int pid = getpid();
    XChangeProperty (_display, _w, _atoms[a_NET_WM_PID], _atoms[a_CARDINAL], 32, PropModeReplace, (const unsigned char*) &pid, 1);
    // WM_PROTOCOLS (to use the close button)
    XChangeProperty (_display, _w, _atoms[a_WM_PROTOCOLS], _atoms[a_ATOM], 32, PropModeReplace, (const unsigned char*) &_atoms[a_WM_DELETE_WINDOW], 1);
    // That this is a system-wide modal dialog
    XSetTransientForHint (_display, _w, RootWindow(_display, _screen));
    // _NET_WM_WINDOW_TYPE set to DIALOG
    XChangeProperty (_display, _w, _atoms[a_NET_WM_WINDOW_TYPE], _atoms[a_ATOM], 32, PropModeReplace, (const unsigned char*) &_atoms[a_NET_WM_WINDOW_TYPE_DIALOG], 1);
    // _NET_WM_STATE set to NORMAL size, MODAL, ABOVE, and DEMANDS_ATTENTION
    XChangeProperty (_display, _w, _atoms[a_NET_WM_STATE], _atoms[a_ATOM], 32, PropModeReplace, (const unsigned char*) &_atoms[a_NET_WM_STATE_NORMAL], 4);

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

    // When all of the above is done, map the window
    XMapRaised (_display, _w);
}

static void ClosePinentryWindow (void)
{
    if (_wfontinfo)
	XFreeFontInfo (NULL, _wfontinfo, 0);
    _wfontinfo = NULL;
    XFreeGC (_display, _gc);
    _gc = None;
    XUnmapWindow (_display, _w);
    XDestroyWindow (_display, _w);
    _w = None;
}

static void RunXMainLoop (void)
{
    for (XEvent e;;) {
	XNextEvent (_display, &e);
	switch (e.type) {
	    case ConfigureNotify:
		_wwidth = e.xconfigure.width;
		_wheight = e.xconfigure.height;
		break;
	    case Expose:
		while (XCheckTypedEvent (_display, Expose, &e)) {}
		DrawWindow();
		break;
	    case ButtonPress:
	    case KeyPress:
		return;
	}
    }
}

static void DrawWindow (void)
{
    XDrawRectangle (_display, _w, _gc, 0, 0, _wwidth-1, _wheight-1);
    XDrawRectangle (_display, _w, _gc, _wwidth/4, _wheight/4, _wwidth/2-1, _wheight/2-1);
    int fw = _wfontinfo->max_bounds.width, fh = _wfontinfo->ascent;
    XDrawString (_display, _w, _gc, fw, fh, "Pinentry", strlen("Pinentry"));
}
