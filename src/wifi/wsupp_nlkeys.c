#include <netlink.h>
#include <netlink/genl.h>
#include <netlink/genl/nl80211.h>

#include "wsupp.h"
#include "wsupp_netlink.h"

/* EAPOL code does negotiations in the user space, but the resulting
   keys must be uploaded (installed, in 802.11 terms) back to the card
   and the upload happens via netlink.

   There are *NO COMPLETION NOTIFICATIONS* for these, it's fire-and-forget.
   That's really bad, because chances are we will trigger_dhpc() before the
   keys reach the card. See CRACK papers for implications.

   And no, Netlink ACKs would not help. ACK means just that the kernel got
   the command. No point in requesting or waiting for them.

   We do not take seq numbers here, there's no point in doing that.
   Errors, if any, will be handled as misc NL errors, causing immediate
   connection abort in genl_error(). */

int upload_ptk(void)
{
	uint8_t seq[6] = { 0, 0, 0, 0, 0, 0 };
	struct nlattr* at;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_NEW_KEY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, ap.bssid, sizeof(ap.bssid));

	nl_put_u8(&nl, NL80211_ATTR_KEY_IDX, 0);
	nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, ap.pairwise);
	nl_put(&nl, NL80211_ATTR_KEY_DATA, PTK, 16);
	nl_put(&nl, NL80211_ATTR_KEY_SEQ, seq, 6);

	at = nl_put_nest(&nl, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nl_put_empty(&nl, NL80211_KEY_DEFAULT_TYPE_UNICAST);
	nl_end_nest(&nl, at);

	return nl_send(&nl);
}

int upload_gtk(void)
{
	struct nlattr* at;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_NEW_KEY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	nl_put_u8(&nl, NL80211_ATTR_KEY_IDX, gtkindex);
	nl_put(&nl, NL80211_ATTR_KEY_SEQ, RSC, 6);

	nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, ap.group);
	nl_put(&nl, NL80211_ATTR_KEY_DATA, GTK, ap.gtklen);

	at = nl_put_nest(&nl, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nl_put_empty(&nl, NL80211_KEY_DEFAULT_TYPE_MULTICAST);
	nl_end_nest(&nl, at);

	return nl_send(&nl);
}
