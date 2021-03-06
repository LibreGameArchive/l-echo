// echo_debug.h

/*
    This file is part of L-Echo.

    L-Echo is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    L-Echo is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with L-Echo.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "echo_platform.h"

#ifdef DEBUG
	#ifdef ECHO_NDS
		#include <nds.h>
		/// It used to be that ints can't be printed onto the NDS console, so n-echo used iprintf
		#define ECHO_PRINT printf
	#else
		#include <cstdio>
		/// Good old printf
		#define ECHO_PRINT printf
	#endif
#else
	/// If there is no debug, just swallow the args
	#define ECHO_PRINT(...)
#endif
