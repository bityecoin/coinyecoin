
Debian
====================
This directory contains files used to package coinyecoind/coinyecoin-qt
for Debian-based Linux systems. If you compile coinyecoind/coinyecoin-qt yourself, there are some useful files here.

## coinyecoin: URI support ##


coinyecoin-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install coinyecoin-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your coinyecoin-qt binary to `/usr/bin`
and the `../../share/pixmaps/bitcoin128.png` to `/usr/share/pixmaps`

coinyecoin-qt.protocol (KDE)

