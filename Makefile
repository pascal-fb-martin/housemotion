# HouseDVR - A simple home web server To access CCTV recordings.
#
# Copyright 2024, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=housemotion
STORE=/videos

# Application build. --------------------------------------------

OBJS= housemotion_store.o housemotion_feed.o housemotion.o
LIBOJS=

all: housemotion

clean:
	rm -f *.o *.a housemotion

rebuild: clean all

%.o: %.c
	gcc -c -Wall -g -O -o $@ $<

housemotion: $(OBJS)
	gcc -g -O -o housemotion $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lmagic -lrt

# Distribution agnostic file installation -----------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/cctv
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/cctv

install-runtime: install-preamble
	if [ "x$(DESTDIR)" = "x" ] ; then grep -q '^motion:' /etc/passwd || useradd -r motion -s /usr/sbin/nologin -d /var/lib/house ; fi
	$(INSTALL) -m 0755 -d $(DESTDIR)$(STORE)
	if [ "x$(DESTDIR)" = "x" ] ; then chown -R motion $(DESTDIR)$(STORE) ; fi
	$(INSTALL) -m 0755 -s housemotion $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housemotion

install-app: install-ui install-runtime

uninstall-app:
	rm -f $(DESTDIR)$(prefix)/bin/housemotion
	rm -f $(DESTDIR)$(SHARE)/public/cctv

purge-app:

purge-config:
	rm -f $(DESTDIR)/etc/house/motion.config
	tm -f $(DESTDIR)/etc/default/housemotion

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package:
	rm -rf build
	install -m 0755 -d build/$(HAPP)/DEBIAN
	cat debian/control | sed "s/{{arch}}/`dpkg --print-architecture`/" > build/$(HAPP)/DEBIAN/control
	install -m 0644 debian/copyright build/$(HAPP)/DEBIAN
	install -m 0644 debian/changelog build/$(HAPP)/DEBIAN
	install -m 0755 debian/postinst build/$(HAPP)/DEBIAN
	install -m 0755 debian/prerm build/$(HAPP)/DEBIAN
	install -m 0755 debian/postrm build/$(HAPP)/DEBIAN
	make DESTDIR=build/$(HAPP) install-package
	cd build ; fakeroot dpkg-deb -b $(HAPP) .

# System installation. ------------------------------------------

include $(SHARE)/install.mak

