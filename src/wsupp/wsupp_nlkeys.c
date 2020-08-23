#include <netlink.h>
#include <netlink/pack.h>
#include <netlink/genl/nl80211.h>

#include "wsupp.h"
#include "wsupp_netlink.h"

/* EAPOL code does negotiations in the user space, but the resulting
   keys must be uploaded (installed, in 802.11 terms) back to the card
   and the upload happens via netlink.

   There are *NO COMPLETION NOTIFICATIONS* for these, it's fire-and-forget.
   That's really bad, because chances are we will trigger_dhpc() before the
   keys reach the card. See the CRACK papers for implications.

   And no, Netlink ACKs would not help. ACK means just that the kernel got
   the command. No point in requesting or waiting for them.

   We do not take seq numbers here, there's no point in doing that.
   Errors, if any, will be handled as misc NL errors, causing immediate
   connection abort in genl_error(). */

int upload_ptk(void)
{
	byte seq[6] = { 0, 0, 0, 0, 0, 0 };
	struct nlattr* at;
	int fd = netlink;

	nc_header(&nc, nl80211, 0, nlseq++);
	nc_gencmd(&nc, NL80211_CMD_NEW_KEY, 0);

	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifindex);
	nc_put(&nc, NL80211_ATTR_MAC, ap.bssid, sizeof(ap.bssid));

	nc_put_byte(&nc, NL80211_ATTR_KEY_IDX, 0);
	nc_put_int(&nc, NL80211_ATTR_KEY_CIPHER, ap.pairwise);
	nc_put(&nc, NL80211_ATTR_KEY_DATA, PTK, 16);
	nc_put(&nc, NL80211_ATTR_KEY_SEQ, seq, 6);

	at = nc_put_nest(&nc, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nc_put_flag(&nc, NL80211_KEY_DEFAULT_TYPE_UNICAST);
	nc_end_nest(&nc, at);

	return nc_send(fd, &nc);
}

int upload_gtk(void)
{
	struct nlattr* at;
	int fd = netlink;

	nc_header(&nc, nl80211, 0, nlseq++);
	nc_gencmd(&nc, NL80211_CMD_NEW_KEY, 0);

	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifindex);

	nc_put_byte(&nc, NL80211_ATTR_KEY_IDX, gtkindex);
	nc_put_int(&nc, NL80211_ATTR_KEY_CIPHER, ap.group);
	nc_put(&nc, NL80211_ATTR_KEY_DATA, GTK, ap.gtklen);
	nc_put(&nc, NL80211_ATTR_KEY_SEQ, RSC, 6);

	at = nc_put_nest(&nc, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nc_put_flag(&nc, NL80211_KEY_DEFAULT_TYPE_MULTICAST);
	nc_end_nest(&nc, at);

	return nc_send(fd, &nc);
}
