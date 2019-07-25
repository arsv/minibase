#include <sys/socket.h>
#include <sys/ppoll.h>
#include <nlusctl.h>

/* Special case of recv() on UC sockets, meant for use on the server side.

   Clients do not send notifications, so there should be at most one pending
   message in the socket at any point. The command are also so small all send
   and recv calls should be atomic, even over stream sockets, which allows
   for a really simple function compared to the general (client) case with
   multiple recv()s and nasty buffer-shifting code.

   The caller should do something like

	if((ret = uc_recv_while(fd, buf, sizeof(buf))) < 0)
		fail();
	if(!(msg = uc_msg(buf, ret)))
		fail();

	process_command(msg);

   There is no way to handle a second message with this interface,
   and we do not expect a second message anyway, so any trailing
   data gets reported as error. */

int uc_recv_whole(int fd, void* buf, int len)
{
	struct ucmsg* msg = buf;
	int rd;

	if((rd = sys_recv(fd, buf, len, 0)) <= 0)
		return rd;
	if((rd < sizeof(*msg))) /* should never happen */
		return -EBADMSG;
	if(msg->len != rd)
		return -EBADMSG;

	return rd;
}
