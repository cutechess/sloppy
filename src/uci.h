#ifndef UCI_H
#define UCI_H

struct _Chess;


/* Switch to UCI mode and print the engine identification and the list
   of available options, ending with "uciok".  */
extern void enter_uci_mode(struct _Chess *chess);

/* Classify the UCI command in last_input: the command type decides what
   happens to a search that's running when the command arrives.  */
extern CmdType get_uci_cmd_type(const struct _Chess *chess);

/* Read a UCI command from last_input and execute it.

   The specification of the UCI protocol can be found here:
   http://wbec-ridderkerk.nl/html/UCIProtocol.html  */
extern int read_uci_input(struct _Chess *chess);

/* Returns true if the command waiting in the input queue is one that
   asks for the best move of an infinite search ("stop" or "ponderhit").
   Any other command means the GUI has abandoned the search and doesn't
   expect a best move for it.  */
extern bool uci_queued_cmd_wants_bestmove(void);

/* Wait for the GUI to end an infinite search. Needed when the search
   finishes on its own, because the "bestmove" reply must not be sent
   before the GUI asks for it.
   Returns true if the best move should be sent ("stop" or "ponderhit"
   arrived, or the input ended), or false if it must be suppressed
   because the GUI abandoned the search with some other command, which
   is left in the input queue for the main loop.  */
extern bool uci_wait_for_stop(struct _Chess *chess);

#endif /* UCI_H */
