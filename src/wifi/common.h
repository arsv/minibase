/* Protocol constants used between wimon and wictl */

#define CMD_STATUS       1
#define CMD_NEUTRAL      2
#define CMD_WIRED        3
#define CMD_SCAN         4
#define CMD_ROAMING      5
#define CMD_FIXEDAP      6
#define CMD_SETPRIO      7

#define ATTR_LINK        1
#define ATTR_WIFI        2
#define ATTR_SCAN        3

#define ATTR_IFI        10
#define ATTR_NAME       11
#define ATTR_MODE       12
#define ATTR_STATE      13
#define ATTR_FREQ       14
#define ATTR_BSSID      15
#define ATTR_SIGNAL     16
#define ATTR_SSID       17
#define ATTR_TYPE       18
#define ATTR_PRIO       19
#define ATTR_PSK        20
#define ATTR_IPADDR     21
#define ATTR_IPMASK     22

/* ATTR_LINK > ATTR_STATE */
#define LINK_OFF         1
#define LINK_ENABLED     2
#define LINK_CARRIER     3
#define LINK_STARTING    4
#define LINK_ACTIVE      5
#define LINK_STOPPING    6

/* ATTR_LINK > ATTR_TYPE */
#define LINK_GENERIC     0
#define LINK_NL80211     1

/* ATTR_SCAN > ATTR_TYPE; same as ST_* since the values are exported as is */
#define TYPE_WPS         (1<<0)
#define TYPE_WPA         (1<<1)
#define TYPE_RSN         (1<<3)
#define TYPE_RSN_PSK     (1<<4)
#define TYPE_RSN_P_TKIP  (1<<5)
#define TYPE_RSN_P_CCMP  (1<<6)
#define TYPE_RSN_G_TKIP  (1<<7)
#define TYPE_RSN_G_CCMP  (1<<8)

/* ATTR_WIFI > ATTR_STATE; same as WS_* constants */
#define WIFI_NONE        0
#define WIFI_IDLE        1
#define WIFI_SCANNING    2
#define WIFI_RETRYING    3
#define WIFI_STARTING    4
#define WIFI_CHANGING    5
#define WIFI_CONNECTED   6
