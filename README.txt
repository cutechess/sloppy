Sloppy is a free chess engine
Copyright (C) 2007 - 2008 Ilari Pihlajisto (ilari.pihlajisto@mbnet.fi)


LICENSE AND WARRANTY

   Sloppy is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   Sloppy is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.


COMPILING

   In UNIX/Linux systems just type "make" to compile Sloppy. There is no
   "make install" though, so if you want to install Sloppy to an other folder
   you'll need to copy the executable and the opening book (if any) to said
   folder.
  
   The Windows version compiles at least with Mingw and Microsoft's Visual C++
   compiler. A makefile for both is included. You'll also need Pthreads for
   Windows: http://sourceware.org/pthreads-win32/


INTERFACE, COMMAND LINE OPTIONS

   Sloppy uses the Xboard chess engine communication protocol. So if you want to
   use a GUI (you probably do), make sure it implements Xboard protocol 2. If
   it only supports UCI, you can still use Sloppy with the wb2uci adapter:
   http://home.online.no/~malin/sjakk/Wb2Uci/


CONFIGURATION

   Sloppy ignores all command line arguments. Instead, you can configure Sloppy
   by editing the config file ("config").


ASCII INTERFACE COMMANDS

   In addition to all Xboard input, Sloppy accepts these commands:

   bench		runs Sloppy's own benchmark
   debug		toggles debugging mode
   divide [d]		perft to depth [d], prints a node count for every move
   help			shows this list
   perft [d]		runs the perft test to depth [d]
   printboard		prints an ASCII chess board and the FEN string
   printeval		prints the static evaluation
   printkey		prints the hash key
   printmat		prints the material each player has on the board
   printmoves		prints a list of legal moves
   quit			quits the program
   readpgn [f]		imports pgn file [f] to the book
   readpgnlist [f]	imports a list of pgn files (in file [f]) to the book
   testpos [sec] [fen]	runs a test position (eg. WAC, WCSAC)
   testsee [fen] [move]	tests the Static Exchange Evaluator
   testsuite [sec] [f]	runs a list of test positions (in file [f])
   xboard		switches to Xboard/Winboard mode

