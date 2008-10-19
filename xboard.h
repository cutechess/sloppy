#ifndef XBOARD_H
#define XBOARD_H

struct _Chess;


extern CmdType get_xboard_cmd_type(struct _Chess *chess);

/* Read an Xboard command from last_input and execute it.

   The specifications of the XBoard/Winboard protocol can be found here:
   http://www.research.digital.com/SRC/personal/mann/xboard/engine-intf.html  */
extern int read_xb_input(struct _Chess *chess);

#endif /* XBOARD_H */

