/*
 * net_ban.c — the one function upstream's no-network driver leaves behind.
 *
 * TyrQuake's NetQuake server carries a `ban` console command, and the table
 * that lists it names NET_Ban_f directly. That function lives in the
 * datagram network driver, net_dgrm.c, because banning is done by network
 * address — so a build that selects upstream's own loopback-only driver,
 * net_none.c, compiles cleanly and then fails to link on this one name.
 * There is no upstream switch that removes the command with the driver.
 *
 * This file supplies it, in this port's own layer rather than by editing the
 * game. It does what the real one does when there is nothing to ban: it says
 * so to the player who asked, and changes nothing. There is no network stack
 * on this board, so there is no address for anyone to arrive from.
 */
#include "quakedef.h"

#include "client.h"
#include "host.h"
#include "net.h"
#include "server.h"

void
NET_Ban_f(client_t *client)
{
    SV_ClientPrintf(client, "Banning is not available: this build has no network.\n");
}
