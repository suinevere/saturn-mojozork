/*----------------------
 | online.h
 | Description: The "Play Online" mode -- dialing the NetLink modem, running the
 |   multizork telnet terminal, and the Zork I typeahead the terminal shares with
 |   the local game.
 | Author: suinevere
 | Dependencies: net/net_connect.h, console_view.h, input.h, typeahead.h, menu.h
 ----------------------*/
#ifndef ONLINE_H
#define ONLINE_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | ensure_online_typeahead
 | Description: Builds the online terminal's typeahead trie from the local
 |   ZORK1.Z3 on the disc (multizork is Zork I, so it shares that dictionary,
 |   grammar and solution overlay). Idempotent, and rebuilt when the difficulty
 |   changes. Call it during the title screen's silent window: the reads it makes
 |   stop CD-DA, so doing it there instead of on the first "Play Online" keeps the
 |   menu track from stuttering.
 | Author: suinevere
 | Dependencies: typeahead.h, typeahead_extract.h, typeahead_solution.h,
 |   game_catalog.h, SRL
 | Globals: g_online_ta, g_online_diff, g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void ensure_online_typeahead(void);

/*----------------------
 | online_mode
 | Description: Connects to the multizork server (auto-redialing a few times,
 |   since the NetLink<->DreamPi carrier handshake is probabilistic) and runs the
 |   telnet terminal until the link drops or the player quits, then returns to the
 |   mode menu.
 | Author: suinevere
 | Dependencies: net/net_connect.h, term.h, console.h, console_view.h, input.h,
 |   keyboard.h, saturn_keyboard.h, typeahead.h, menu.h, soft_reset.h, music.h,
 |   SRL
 | Globals: g_online_ta, g_dialnum, g_sel_track, g_scroll, g_pad, g_kbd_visible
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void online_mode(void);

#ifdef __cplusplus
}
#endif

#endif
