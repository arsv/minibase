#include <sys/socket.h>

#include <netlink.h>
#include <netlink/recv.h>

#include <string.h>

/* Shift-buffer routines for incoming NL messages */

void nr_buf_set(struct nrbuf* nr, void* buf, uint len)
{
	nr->buf = buf;
	nr->ptr = buf;
	nr->msg = buf;
	nr->end = buf + len;
}

int nr_recv(int fd, struct nrbuf* nr)
{
	void* buf = nr->buf;
	void* ptr = nr->ptr;
	void* end = nr->end;

	if(ptr < buf || ptr >= end)
		return -ENOBUFS;

	long left = end - ptr;
	long ret;

	if((ret = sys_recv(fd, ptr, left, 0)) > 0)
		nr->ptr = ptr + ret;

	return ret;
}

/* Typical situation in nr:

         buf                  ptr
         v                    v
       | message message messa..... |
                 ^                 ^
                 msg               end

   The first invocation returns the (complete) message at msg, while also
   advancing msg past that point:

         buf                  ptr
         v                    v
       | message message messa..... |
                         ^         ^
                         msg       end

   The second invocation fails to find a complete message at msg, so it
   returns NULL while also shifting the buffer:

         buf  ptr
         v    v
       | messa..................... |
         ^                         ^
         msg                       end

   The caller is then expected to call nr_recv() again, filling the tail
   of the buffer and hopefully completing the message. */

struct nlmsg* nr_next(struct nrbuf* nr)
{
	void* buf = nr->buf;
	void* ptr = nr->ptr;
	void* msg = nr->msg;
	void* end = nr->end;
	struct nlmsg* nlm;

	if(msg < buf || msg >= end)
		return NULL;

	long len = ptr - msg;

	if((nlm = nl_msg(msg, len))) {
		msg += nl_len(nlm);
		if(msg < ptr) {
			nr->msg = msg;
		} else {
			nr->msg = buf;
			nr->ptr = buf;
		}
		return nlm;
	} else if(msg > buf) {
		memmove(buf, msg, len);
		nr->ptr = buf + len;
		nr->msg = buf;
	}

	return NULL;
}
