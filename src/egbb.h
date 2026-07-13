#ifndef EGBB_H
#define EGBB_H

#include "sloppy.h"
#include "util.h"

/* Store a normalized copy of <path> (with a trailing slash) as the
   endgame bitbase path. An empty path clears the setting.  */
extern void set_egbb_path(const char *path);

/* Store the egbb path, enable a default load type if the bitbases were
   disabled, and load the bitbases. This is what the Xboard "egtpath"
   command and the UCI "EgbbPath" option do.  */
extern void activate_egbb_path(const char *path);

/* Convert an egbb load type string (as used in the config file and the
   UCI "EgbbLoadType" option) into an EgbbLoadType value.
   Returns -1 if the string isn't a valid load type.  */
extern int parse_egbb_load_type(const char *str);

/* Convert an EgbbLoadType value into its string form.  */
extern const char *egbb_load_type_str(EgbbLoadType load_type);

/* Load the endgame bitbase library.
   Returns true if successfull.  */
extern bool load_bitbases(void);

/* Unload the endgame bitbase object/library.  */
extern void unload_bitbases(void);

/* Probe the bitbases for a result.
   Returns VAL_NONE if the position is not found.  */
extern int probe_bitbases(const Board *board, int ply, int depth);

#endif /* EGBB_H */
