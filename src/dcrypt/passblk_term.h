extern int rows;
extern int cols;

void clear(void);
void moveto(int r, int c);
void spaces(int n);

void quit(const char* msg, char* arg, int err);
void park_cursor(void);
void hide_cursor(void);
void show_cursor(void);
void erase_line(void);

void clearbox(void);
void drawbox(int r, int c, int w, int h);
void output(char* s, int len);
void outstr(char* s);

void link_plain_partitions(void);
