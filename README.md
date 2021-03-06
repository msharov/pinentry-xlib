
# pinentry-xlib

Pinentry is the program called by background gpg and gpg-agent processes
to prompt the user for passwords. The stock gpg package includes several
[pinentry programs](https://github.com/gpg/pinentry), including one
for the terminal using curses, and X versions using gtk, gtk2, qt, or
qt4 toolkits. These UI toolkits are quite large, and the gtk2 pinentry
ends up linked to 60 (!) shared libraries with a combined size of
60M. Because pinentry does little more than display a dialog box and
input the password, this seems rather excessive. So this project is a
toolkit-less version of pinentry using only the basic X API, loading
only 6 shared libraries and having a 3M footprint.

There is also a similar program, ssh-askpass, used by ssh and ssh-agent
to ask for passwords. It can also be replaced by pinentry-xlib.

To compile you need X11 libs and gcc or another C11-supporting compiler:

```sh
./configure --prefix=/usr
make
make install
```

pinentry and ssh-askpass use symlinks to allow keeping multiple versions
installed simultaneously, so to enable pinentry-xlib you will need to:

```sh
cd /usr/bin
rm pinentry ssh-askpass
ln -s pinentry-xlib pinentry
ln -s pinentry-xlib ssh-askpass
```

For usage instructions consult pinentry info page installed with gpg.
Report bugs on [project bugtracker](https://github.com/msharov/pinentry-xlib/issues).
