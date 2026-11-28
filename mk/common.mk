#
# Copyright 2014-2026 the Pacemaker project contributors
#
# The version control history for this file may have further details.
#
# This source code is licensed under the GNU General Public License version 2
# or later (GPLv2+) WITHOUT ANY WARRANTY.
#

#
# Some variables to help with silent rules
# https://www.gnu.org/software/automake/manual/html_node/Automake-silent_002drules-Option.html

V ?= $(AM_DEFAULT_VERBOSITY)

# When a make command is prefixed with one of the AM_V_* macros, it may also be
# desirable to suffix the command with this, to silence stdout.
PCMK_quiet = $(pcmk_quiet_$(V))
pcmk_quiet_0 = >/dev/null
pcmk_quiet_1 = 

MAINTAINERCLEANFILES	= Makefile.in

AM_CPPFLAGS		= -I$(top_builddir)/include 	\
			  -I$(top_srcdir)/include
