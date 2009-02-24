#ifndef INPUT_H
#define INPUT_H

struct _Chess;


/* Read input from stdin or last_input (if the input was already read) and
   perform the task associated with the command.

   These commands only work in the PROTO_NONE mode. Xboard commands are
   however valid also in the PROTO_NONE mode.  */
extern int read_input(struct _Chess *chess);

/* See if there's any input (with a line break) in stdin. If there is,
   then return the type of the input.  */
extern CmdType input_available(struct _Chess *chess);

#endif /* INPUT_H */

