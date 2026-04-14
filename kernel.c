//Hudson Strauss

//Refer to mmio.h for pin declarations
#include "string.h"
#include "uart.h"
#include "gpio.h"
#include "mmio.h"
#include "timer.h"
#include "cpu.h"
#include "irq.h"
#include "alloc.h"
#include "mmu.h"
#include "task.h"
#include "sched.h"
#include "vfs.h"
#include "spinlock.h"
#include "semaphore.h"
#include "fb.h"
#include "mutex.h"
#include "msgqueue.h"
#include "panic.h"
#include "user.h"
#include "syscall.h"
#include "version.h"
#include "test.h"
#include "home.h"
#include "elf.h"
#include "sd.h"
#include "fat32.h"
#include "kb.h"
#include "platform.h"
#include "tinycc.h"

extern const unsigned char embedded_user_app_data[];
extern const unsigned int embedded_user_app_size;
extern const char embedded_user_app_name[];
static char shell_getc(void);
static int shell_try_getc(void);
static void shell_ungetc(char c);
static void build_path(char *out, int outlen, const char *name);
static void str_copy_limit(char *dst, const char *src, int cap);

// skip past the current argument to the next space-separated one
static const char *next_arg(const char *s){
	while(*s && *s != ' ') ++s;
	while(*s == ' ') ++s;
	return s;
}

static void unsupported_command(const char *feature){
	uart_puts(feature);
	uart_puts(" is not available on this platform\n");
}

#define VI_MAX_TEXT VFS_MAX_FSIZE
#define VI_SCREEN_ROWS 20
#define VI_SCREEN_COLS 78
#define VI_KEY_UP    1001
#define VI_KEY_DOWN  1002
#define VI_KEY_LEFT  1003
#define VI_KEY_RIGHT 1004
#define SHELL_HISTORY_MAX 16
#define SHELL_LINE_MAX 256

static char shell_history[SHELL_HISTORY_MAX][SHELL_LINE_MAX];
static int shell_history_count = 0;
static int shell_unget_buf = -1;

typedef struct {
	char path[128];
	char buf[VI_MAX_TEXT + 1];
	char screen_cache[VI_SCREEN_ROWS][VI_SCREEN_COLS + 2];
	char status_cache[128];
	int len;
	int cursor;
	int rowoff;
	int coloff;
	int dirty;
	int mode_insert;
	int quit;
	int cache_valid;
} vi_editor_t;

static int vi_read_key(void){
	char c = shell_getc();
	if(c != 27)
		return (unsigned char)c;

	int c1 = shell_try_getc();
	if(c1 < 0)
		return 27;
	if(c1 != '[')
		shell_ungetc((char)c1);
	if(c1 != '[')
		return 27;

	int c2 = shell_getc();
	if(c2 == 'A') return VI_KEY_UP;
	if(c2 == 'B') return VI_KEY_DOWN;
	if(c2 == 'C') return VI_KEY_RIGHT;
	if(c2 == 'D') return VI_KEY_LEFT;
	return 27;
}

static void shell_store_history(const char *line){
	if(!line || !line[0]) return;
	if(shell_history_count > 0 &&
	   str_eq(shell_history[(shell_history_count - 1) % SHELL_HISTORY_MAX], line))
		return;
	if(shell_history_count < SHELL_HISTORY_MAX){
		str_copy_limit(shell_history[shell_history_count], line, SHELL_LINE_MAX);
		shell_history_count++;
		return;
	}
	for(int i = 1; i < SHELL_HISTORY_MAX; i++)
		str_copy_limit(shell_history[i - 1], shell_history[i], SHELL_LINE_MAX);
	str_copy_limit(shell_history[SHELL_HISTORY_MAX - 1], line, SHELL_LINE_MAX);
}

static void shell_replace_line(char *dst, int maxLen, const char *src, int *len_io){
	while(*len_io > 0){
		uart_putc('\b');
		uart_putc(' ');
		uart_putc('\b');
		(*len_io)--;
	}
	int i = 0;
	while(src[i] && i < maxLen - 1){
		dst[i] = src[i];
		uart_putc(src[i]);
		i++;
	}
	dst[i] = '\0';
	*len_io = i;
}

static void str_copy_limit(char *dst, const char *src, int cap){
	int i = 0;
	while(src[i] && i < cap - 1){
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
}

static void mem_move(char *dst, const char *src, int n){
	if(n <= 0 || dst == src) return;
	if(dst < src){
		for(int i = 0; i < n; i++) dst[i] = src[i];
	} else {
		for(int i = n - 1; i >= 0; i--) dst[i] = src[i];
	}
}

static int vi_line_start(const vi_editor_t *ed, int idx){
	if(idx < 0) idx = 0;
	if(idx > ed->len) idx = ed->len;
	while(idx > 0 && ed->buf[idx - 1] != '\n') idx--;
	return idx;
}

static int vi_line_end(const vi_editor_t *ed, int idx){
	if(idx < 0) idx = 0;
	if(idx > ed->len) idx = ed->len;
	while(idx < ed->len && ed->buf[idx] != '\n') idx++;
	return idx;
}

static int vi_cursor_col(const vi_editor_t *ed){
	int start = vi_line_start(ed, ed->cursor);
	return ed->cursor - start;
}

static int vi_cursor_row(const vi_editor_t *ed){
	int row = 0;
	for(int i = 0; i < ed->cursor && i < ed->len; i++)
		if(ed->buf[i] == '\n') row++;
	return row;
}

static void vi_set_cursor_row_col(vi_editor_t *ed, int target_row, int target_col){
	int row = 0;
	int pos = 0;
	if(target_row < 0) target_row = 0;
	if(target_col < 0) target_col = 0;
	while(row < target_row && pos < ed->len){
		if(ed->buf[pos++] == '\n') row++;
	}
	int line_end = vi_line_end(ed, pos);
	int line_len = line_end - pos;
	if(target_col > line_len) target_col = line_len;
	ed->cursor = pos + target_col;
}

static void vi_insert_char(vi_editor_t *ed, char c){
	if(ed->len >= VI_MAX_TEXT) return;
	mem_move(ed->buf + ed->cursor + 1, ed->buf + ed->cursor, ed->len - ed->cursor);
	ed->buf[ed->cursor] = c;
	ed->len++;
	ed->cursor++;
	ed->buf[ed->len] = '\0';
	ed->dirty = 1;
}

static void vi_delete_at(vi_editor_t *ed, int idx){
	if(idx < 0 || idx >= ed->len) return;
	mem_move(ed->buf + idx, ed->buf + idx + 1, ed->len - idx - 1);
	ed->len--;
	ed->buf[ed->len] = '\0';
	ed->dirty = 1;
}

static void vi_backspace(vi_editor_t *ed){
	if(ed->cursor <= 0) return;
	vi_delete_at(ed, ed->cursor - 1);
	ed->cursor--;
}

static void vi_delete_line(vi_editor_t *ed){
	int start = vi_line_start(ed, ed->cursor);
	int end = vi_line_end(ed, ed->cursor);
	if(end < ed->len && ed->buf[end] == '\n') end++;
	if(start == 0 && end >= ed->len){
		ed->len = 0;
		ed->cursor = 0;
		ed->buf[0] = '\0';
		ed->dirty = 1;
		return;
	}
	mem_move(ed->buf + start, ed->buf + end, ed->len - end);
	ed->len -= (end - start);
	if(start > ed->len) start = ed->len;
	ed->cursor = start;
	ed->buf[ed->len] = '\0';
	ed->dirty = 1;
}

static int vi_save(vi_editor_t *ed){
	int fd = vfs_open(ed->path, O_WRONLY | O_CREAT | O_TRUNC);
	if(fd < 0) return -1;
	int wrote = vfs_write(fd, ed->buf, ed->len);
	vfs_close(fd);
	if(wrote != ed->len) return -1;
	ed->dirty = 0;
	return 0;
}

static void vi_scroll(vi_editor_t *ed){
	int row = vi_cursor_row(ed);
	int col = vi_cursor_col(ed);
	if(row < ed->rowoff) ed->rowoff = row;
	if(row >= ed->rowoff + VI_SCREEN_ROWS) ed->rowoff = row - VI_SCREEN_ROWS + 1;
	if(col < ed->coloff) ed->coloff = col;
	if(col >= ed->coloff + VI_SCREEN_COLS) ed->coloff = col - VI_SCREEN_COLS + 1;
	if(ed->rowoff < 0) ed->rowoff = 0;
	if(ed->coloff < 0) ed->coloff = 0;
}

static int str_eq_limit(const char *a, const char *b){
	int i = 0;
	while(a[i] || b[i]){
		if(a[i] != b[i]) return 0;
		i++;
	}
	return 1;
}

static void vi_build_status(const vi_editor_t *ed, const char *msg, char *out, int cap){
	int i = 0;
	const char *mode = ed->mode_insert ? "INSERT" : "NORMAL";
	const char *dirty = ed->dirty ? " [+]" : "";
	const char *tail = msg ? msg : "";
	const char *parts[7] = {mode, "  ", ed->path, dirty, "  ", tail, 0};
	for(int p = 0; parts[p]; p++){
		const char *s = parts[p];
		while(*s && i < cap - 1) out[i++] = *s++;
	}
	out[i] = '\0';
}

static void vi_build_screen_line(const vi_editor_t *ed, int *idx_io, char *out){
	int idx = *idx_io;
	int col = 0;
	int line_col = 0;
	if(idx >= ed->len){
		out[0] = '~';
		out[1] = '\0';
		*idx_io = idx;
		return;
	}
	while(idx < ed->len && ed->buf[idx] != '\n'){
		if(line_col >= ed->coloff && col < VI_SCREEN_COLS){
			char c = ed->buf[idx];
			if(c < 32 || c > 126) c = '?';
			out[col++] = c;
		}
		line_col++;
		idx++;
	}
	out[col] = '\0';
	if(idx < ed->len && ed->buf[idx] == '\n') idx++;
	*idx_io = idx;
}

static void vi_draw_status(const vi_editor_t *ed, const char *msg){
	uart_puts("\033[22;1H\033[7m");
	kprintf("%s  %s%s  %s", ed->mode_insert ? "INSERT" : "NORMAL",
		ed->path, ed->dirty ? " [+]" : "", msg ? msg : "");
	uart_puts("\033[K\033[m");
}

static void vi_render(vi_editor_t *ed, const char *msg){
	vi_scroll(ed);
	uart_puts("\033[?25l");
	int file_row = 0;
	int idx = 0;
	char line[VI_SCREEN_COLS + 2];
	char status[128];
	while(file_row < ed->rowoff && idx < ed->len){
		if(ed->buf[idx++] == '\n') file_row++;
	}
	for(int screen_row = 0; screen_row < VI_SCREEN_ROWS; screen_row++){
		vi_build_screen_line(ed, &idx, line);
		if(!ed->cache_valid || !str_eq_limit(ed->screen_cache[screen_row], line)){
			kprintf("\033[%d;1H", screen_row + 1);
			uart_puts("\033[K");
			uart_puts(line);
			uart_puts("\033[K");
			str_copy_limit(ed->screen_cache[screen_row], line, VI_SCREEN_COLS + 2);
		}
	}
	vi_build_status(ed, msg, status, sizeof(status));
	if(!ed->cache_valid || !str_eq_limit(ed->status_cache, status)){
		vi_draw_status(ed, msg);
		str_copy_limit(ed->status_cache, status, sizeof(ed->status_cache));
	}
	ed->cache_valid = 1;
	int cursor_row = vi_cursor_row(ed) - ed->rowoff + 1;
	int cursor_col = vi_cursor_col(ed) - ed->coloff + 1;
	if(cursor_row < 1) cursor_row = 1;
	if(cursor_row > VI_SCREEN_ROWS) cursor_row = VI_SCREEN_ROWS;
	if(cursor_col < 1) cursor_col = 1;
	if(cursor_col > VI_SCREEN_COLS) cursor_col = VI_SCREEN_COLS;
	kprintf("\033[%d;%dH", cursor_row, cursor_col);
	uart_puts("\033[?25h");
}

static void vi_command_prompt(vi_editor_t *ed){
	char cmd[16];
	int i = 0;
	vi_draw_status(ed, ":");
	kprintf("\033[22;3H");
	while(i < (int)sizeof(cmd) - 1){
		int c = vi_read_key();
		if(c == '\r' || c == '\n') break;
		if(c == 27) return;
		if((c == '\b' || c == 0x7f) && i > 0){
			i--;
			uart_puts("\b \b");
			continue;
		}
		if(c < 32 || c > 126) continue;
		cmd[i++] = c;
		uart_putc(c);
	}
	cmd[i] = '\0';
	if(str_eq(cmd, "q")){
		if(ed->dirty){
			vi_render(ed, "No write since last change (:q! to quit)");
			return;
		}
		ed->quit = 1;
		return;
	}
	if(str_eq(cmd, "q!")){
		ed->quit = 1;
		return;
	}
	if(str_eq(cmd, "w")){
		if(vi_save(ed) < 0) vi_render(ed, "write failed");
		else vi_render(ed, "written");
		return;
	}
	if(str_eq(cmd, "wq") || str_eq(cmd, "x")){
		if(vi_save(ed) < 0){
			vi_render(ed, "write failed");
			return;
		}
		ed->quit = 1;
		return;
	}
	vi_render(ed, "unknown command");
}

static void vi_move_left(vi_editor_t *ed){
	if(ed->cursor > 0) ed->cursor--;
}

static void vi_move_right(vi_editor_t *ed){
	if(ed->cursor < ed->len) ed->cursor++;
}

static void vi_move_down(vi_editor_t *ed){
	int row = vi_cursor_row(ed);
	int col = vi_cursor_col(ed);
	vi_set_cursor_row_col(ed, row + 1, col);
}

static void vi_move_up(vi_editor_t *ed){
	int row = vi_cursor_row(ed);
	int col = vi_cursor_col(ed);
	if(row > 0) vi_set_cursor_row_col(ed, row - 1, col);
}

static void vi_open_command(const char *arg){
	if(!arg || !*arg){
		uart_puts("usage: vi <file>\n");
		return;
	}
	char path[128];
	build_path(path, 128, arg);
	vi_editor_t *ed = (vi_editor_t *)kmalloc(sizeof(vi_editor_t));
	if(!ed){
		uart_puts("vi: out of memory\n");
		return;
	}
	memset(ed, 0, sizeof(*ed));
	str_copy_limit(ed->path, path, sizeof(ed->path));
	void (*saved_hook)(char c) = uart_get_output_hook();
	uart_set_output_hook(0);

	int fd = vfs_open(path, O_RDONLY);
	if(fd >= 0){
		int n = vfs_read(fd, ed->buf, VI_MAX_TEXT);
		vfs_close(fd);
		if(n > 0){
			ed->len = n;
			ed->buf[ed->len] = '\0';
		}
	}

	uart_puts("\033[2J\033[H");
	vi_render(ed, "i=insert  Esc=normal  :w  :q  :wq");
	int pending_d = 0;
	int pending_z = 0;
	int quit_confirm = 0;
	while(!ed->quit){
		int c = vi_read_key();
		if(ed->mode_insert){
			if(c == 3 || c == 17){
				ed->quit = 1;
				break;
			}
			if(c == 27){
				ed->mode_insert = 0;
				vi_render(ed, 0);
				continue;
			}
			if(c == VI_KEY_LEFT) vi_move_left(ed);
			else if(c == VI_KEY_RIGHT) vi_move_right(ed);
			else if(c == VI_KEY_UP) vi_move_up(ed);
			else if(c == VI_KEY_DOWN) vi_move_down(ed);
			if(c == '\r' || c == '\n'){
				vi_insert_char(ed, '\n');
			} else if(c == '\b' || c == 0x7f){
				vi_backspace(ed);
			} else if(c >= 32 && c <= 126){
				vi_insert_char(ed, c);
			}
			vi_render(ed, 0);
			continue;
		}

		if(c == 3 || c == 17){
			if(ed->dirty && !quit_confirm){
				quit_confirm = 1;
				pending_d = 0;
				pending_z = 0;
				vi_render(ed, "Ctrl-C again to quit without saving");
				continue;
			}
			ed->quit = 1;
			break;
		}
		quit_confirm = 0;

		if(c == 'd' && pending_d){
			vi_delete_line(ed);
			pending_d = 0;
			pending_z = 0;
			vi_render(ed, "line deleted");
			continue;
		}
		pending_d = (c == 'd');
		if(c != 'Z') pending_z = 0;

		if(c == 'h' || c == VI_KEY_LEFT) vi_move_left(ed);
		else if(c == 'l' || c == VI_KEY_RIGHT) vi_move_right(ed);
		else if(c == 'j' || c == VI_KEY_DOWN) vi_move_down(ed);
		else if(c == 'k' || c == VI_KEY_UP) vi_move_up(ed);
		else if(c == '0') ed->cursor = vi_line_start(ed, ed->cursor);
		else if(c == '$') ed->cursor = vi_line_end(ed, ed->cursor);
		else if(c == 'i') ed->mode_insert = 1;
		else if(c == 'a'){ if(ed->cursor < ed->len) ed->cursor++; ed->mode_insert = 1; }
		else if(c == 'o'){
			ed->cursor = vi_line_end(ed, ed->cursor);
			vi_insert_char(ed, '\n');
			ed->mode_insert = 1;
		} else if(c == 'x'){
			vi_delete_at(ed, ed->cursor);
			if(ed->cursor > ed->len) ed->cursor = ed->len;
		} else if(c == 'Q'){
			ed->quit = 1;
			break;
		} else if(c == 'Z' && pending_z){
			if(vi_save(ed) < 0){
				pending_z = 0;
				vi_render(ed, "write failed");
				continue;
			}
			ed->quit = 1;
			break;
		} else if(c == 'Z'){
			pending_z = 1;
			vi_render(ed, "press Z again to save and quit");
			continue;
		} else if(c == ':'){
			pending_d = 0;
			pending_z = 0;
			vi_command_prompt(ed);
			if(!ed->quit) vi_render(ed, 0);
			continue;
		}
		vi_render(ed, 0);
	}
	kfree(ed);
	uart_set_output_hook(saved_hook);
	uart_puts("\033[2J\033[H");
	uart_puts("left vi\n");
}

static void install_embedded_user_app(void){
	if(embedded_user_app_size == 0 || embedded_user_app_name[0] == '\0')
		return;

	char path[64] = "/bin/";
	int i = 5;
	for(int j = 0; embedded_user_app_name[j] && i < (int)sizeof(path) - 1; j++)
		path[i++] = embedded_user_app_name[j];
	path[i] = '\0';

	int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
	if(fd < 0){
		kprintf("failed to install embedded app: %s\n", embedded_user_app_name);
		return;
	}
	vfs_write(fd, embedded_user_app_data, (int)embedded_user_app_size);
	vfs_close(fd);
	kprintf("embedded app installed: %s\n", path);
}

static void install_code_demo(void){
	static const char demo_src[] =
		"long u_puts(char *s);\n"
		"long u_putc(char c);\n"
		"long u_sleep(long ms);\n"
		"\n"
		"char banner;\n"
		"int values[4];\n"
		"\n"
		"int sum_pair(int a, int b){\n"
		"    return a + b;\n"
		"}\n"
		"\n"
		"int main(void){\n"
		"    int i;\n"
		"    int total;\n"
		"    int *p;\n"
		"\n"
		"    banner = 'H';\n"
		"    values[0] = 1;\n"
		"    values[1] = 2;\n"
		"    values[2] = 3;\n"
		"    values[3] = 4;\n"
		"\n"
		"    p = &values[0];\n"
		"    total = 0;\n"
		"\n"
		"    u_puts(\"Tiny C quick demo: \");\n"
		"    u_putc(banner);\n"
		"    u_puts(\"\\n\");\n"
		"\n"
		"    for(i = 0; i < 4; i = i + 1){\n"
		"        total = total + p[i];\n"
		"        u_putc('0' + p[i]);\n"
		"    }\n"
		"\n"
		"    u_puts(\"\\nsum=\");\n"
		"    u_putc('0' + total);\n"
		"    u_puts(\"\\npair=\");\n"
		"    u_putc('0' + sum_pair(values[1], values[2]));\n"
		"    u_puts(\"\\nsize(long)=\");\n"
		"    u_putc('0' + sizeof(long));\n"
		"    u_puts(\"\\n\");\n"
		"\n"
		"    u_sleep(100);\n"
		"    return 0;\n"
		"}\n";

	int fd = vfs_open("/Code/quick_demo.c", O_WRONLY | O_CREAT | O_TRUNC);
	if(fd < 0){
		kprintf("failed to install code demo\n");
		return;
	}
	vfs_write(fd, demo_src, k_strlen(demo_src));
	vfs_close(fd);
}

// ── shell working directory ───────────────────────────────────────────────────

static char cwd[128] = "/";

// build an absolute path from a user-supplied name (relative or absolute)
// result written into out[outlen]
static void build_path(char *out, int outlen, const char *name){
	if(name[0] == '/'){
		int i = 0;
		while(name[i] && i < outlen - 1){ out[i] = name[i]; i++; }
		out[i] = '\0';
	} else {
		int i = 0;
		while(cwd[i] && i < outlen - 1){ out[i] = cwd[i]; i++; }
		// add separator only when cwd is not "/"
		if(i > 0 && out[i-1] != '/' && i < outlen - 1) out[i++] = '/';
		int j = 0;
		while(name[j] && i < outlen - 1){ out[i++] = name[j++]; }
		out[i] = '\0';
	}
}

static void normalize_path(char *path){
	char out[128];
	int src = 0;
	int dst = 0;

	out[dst++] = '/';
	out[dst] = '\0';

	while(path[src]){
		int start;
		int len = 0;

		while(path[src] == '/')
			src++;
		if(!path[src])
			break;

		start = src;
		while(path[src] && path[src] != '/'){
			src++;
			len++;
		}

		if(len == 1 && path[start] == '.')
			continue;

		if(len == 2 && path[start] == '.' && path[start + 1] == '.'){
			if(dst > 1){
				dst--;
				while(dst > 1 && out[dst - 1] != '/')
					dst--;
				out[dst] = '\0';
			}
			continue;
		}

		if(dst > 1 && dst < (int)sizeof(out) - 1)
			out[dst++] = '/';
		for(int i = 0; i < len && dst < (int)sizeof(out) - 1; i++)
			out[dst++] = path[start + i];
		out[dst] = '\0';
	}

	if(dst == 0){
		out[0] = '/';
		out[1] = '\0';
	}

	dst = 0;
	while(out[dst] && dst < 127){
		path[dst] = out[dst];
		dst++;
	}
	path[dst] = '\0';
}

// --- commands ---

static void help_command(){
	uart_puts("Commands:\n");
	uart_puts("\n[system]\n");
	uart_puts("  help                show this message\n");
	uart_puts("  info                version and build info\n");
	uart_puts("  why                 why I built this\n");
	uart_puts("  clear               clear screen\n");
	uart_puts("  echo <text>         print text\n");
	uart_puts("  el                  show exception level\n");
	uart_puts("  uptime              time since boot\n");
	uart_puts("  panic               trigger kernel panic\n");
	uart_puts("  crashtest <type>    null/assert/brk/undef/align\n");
#if PLATFORM_HAS_GPIO
	uart_puts("\n[gpio]\n");
	uart_puts("  led <pin> <on/off>  set GPIO pin output\n");
	uart_puts("  blink <pin>         blink GPIO pin 5 times\n");
	uart_puts("  readpin <pin>       read GPIO pin state\n");
#endif
	uart_puts("\n[timer/irq]\n");
	uart_puts("  ticks               show timer IRQ count\n");
	uart_puts("  irqtest             count IRQs over 3s\n");
	uart_puts("  timerdbg            dump GIC/timer state\n");
	uart_puts("\n[memory]\n");
	uart_puts("  peek <addr>         read 32-bit value at hex address\n");
	uart_puts("  poke <addr> <val>   write 32-bit hex value to address\n");
	uart_puts("  dump <addr> [n]     dump n words (default 8)\n");
	uart_puts("  heapinfo            show allocator state\n");
	uart_puts("  malloc <size>       test allocate bytes\n");
	uart_puts("  free <addr>         free allocated memory\n");
	uart_puts("\n[tasks]\n");
	uart_puts("  tasks               list running tasks\n");
	uart_puts("  spawn               spawn a background counter task\n");
	uart_puts("  kill <id>           kill a task by ID\n");
	uart_puts("  sleep <ms>          sleep current task for ms\n");
	uart_puts("  yield               yield to next task\n");
	uart_puts("  uspawn              spawn a user-mode demo task (EL1)\n");
	uart_puts("\n[ipc/sync]\n");
	uart_puts("  locktest            test spinlock primitives\n");
	uart_puts("  semtest             producer/consumer semaphore demo\n");
	uart_puts("  mqtest              message queue ping-pong demo\n");
	uart_puts("  mutextest           mutex shared counter demo\n");
	uart_puts("\n[filesystem]\n");
	uart_puts("  ls [path]           list directory contents\n");
	uart_puts("  mkdir <name>        create directory\n");
	uart_puts("  mkfile <name>       create empty file\n");
	uart_puts("  write <name> <data> write data to file\n");
	uart_puts("  cat <name>          read file contents\n");
	uart_puts("  rm <name>           remove file or empty directory\n");
	uart_puts("  cd <path>           change working directory\n");
	uart_puts("  pwd                 print working directory\n");
	uart_puts("  tcc <src> -o <out>  host-side Tiny C compiler (see repo ./tcc)\n");
	uart_puts("  toycc <src> -o <out> in-kernel toy Tiny C bytecode compiler\n");
	uart_puts("  vi <file>           open vi-like editor\n");
	uart_puts("  exec <path>         load ELF binary from VFS and run it\n");
#if PLATFORM_HAS_USB_KEYBOARD
	uart_puts("\n[keyboard]\n");
	uart_puts("  kbinit              init USB HID keyboard (DWC2 OTG)\n");
#endif
#if PLATFORM_HAS_SD
	uart_puts("\n[sd card / fat32]\n");
	uart_puts("  sdinit              initialize SD card\n");
	uart_puts("  sdls [path]         list directory on SD card (FAT32)\n");
	uart_puts("  sdcat <path>        print file from SD card\n");
	uart_puts("  sdexec <path>       load and run ELF from SD card\n");
	uart_puts("  mount               copy /bin/* from SD into VFS /bin\n");
#endif
#if PLATFORM_HAS_FRAMEBUFFER
	uart_puts("\n[framebuffer]\n");
	uart_puts("  fbtest              draw test pattern on framebuffer\n");
	uart_puts("  fbmirror            toggle horizontal mirror (QEMU fix)\n");
	uart_puts("  home                launch live desktop (auto-updates)\n");
	uart_puts("  home stop           close the desktop\n");
#endif
	uart_puts("\n[self-tests]\n");
	uart_puts("  test_uart           test UART and string library\n");
#if PLATFORM_HAS_GPIO
	uart_puts("  test_gpio           test GPIO driver\n");
#endif
	uart_puts("  test_timer          test timer driver\n");
	uart_puts("  test_alloc          test memory allocator\n");
	uart_puts("  test_all            run all self-tests\n");
}
static void info_command(){
	kprintf("%s v%s\n", HUDOS_NAME, HUDOS_VERSION);
	kprintf("Arch:   %s running at %s\n", HUDOS_ARCH, HUDOS_EL);
	kprintf("Board:  %s\n", HUDOS_BOARD);
	kprintf("Author: %s\n", HUDOS_AUTHOR);
	kprintf("Built:  %s %s\n", __DATE__, __TIME__);
}
static void why_command(){
	uart_puts("I wanted to get better at actually coding, I know most theory but have bad impl. skills\n");
}
static void clear_command(){
	uart_puts("\033[2J\033[H");
}
static void echo_command(const char *arg){
	uart_puts(arg);
	uart_puts("\n");
}

// led <pin> <on/off>
static void led_command(const char *arg){
#if !PLATFORM_HAS_GPIO
	(void)arg;
	unsupported_command("gpio");
	return;
#else
	int pin = k_atoi(arg);
	arg = next_arg(arg);
	gpio_set_function(pin, GPIO_FUNC_OUTPUT);
	if(str_eq(arg, "on")){
		gpio_write(pin, 1);
		uart_puts("pin on\n");
	} else if(str_eq(arg, "off")){
		gpio_write(pin, 0);
		uart_puts("pin off\n");
	} else {
		uart_puts("usage: led <pin> <on/off>\n");
	}
#endif
}

// blink <pin>
static void blink_command(const char *arg){
#if !PLATFORM_HAS_GPIO
	(void)arg;
	unsupported_command("gpio");
	return;
#else
	int pin = k_atoi(arg);
	gpio_set_function(pin, GPIO_FUNC_OUTPUT);
	uart_puts("blinking pin ");
	char buf[12];
	itoa(pin, buf, 12);
	uart_puts(buf);
	uart_puts("...\n");
	for(int i = 0; i < 5; i++){
		gpio_write(pin, 1);
		delay_ms(500);
		gpio_write(pin, 0);
		delay_ms(500);
	}
	uart_puts("done\n");
#endif
}

// readpin <pin>
static void readpin_command(const char *arg){
#if !PLATFORM_HAS_GPIO
	(void)arg;
	unsupported_command("gpio");
	return;
#else
	int pin = k_atoi(arg);
	gpio_set_function(pin, GPIO_FUNC_INPUT);
	int val = gpio_read(pin);
	uart_puts("pin ");
	char buf[12];
	itoa(pin, buf, 12);
	uart_puts(buf);
	if(val)
		uart_puts(": HIGH\n");
	else
		uart_puts(": LOW\n");
#endif
}

static void uptime_command(){
	unsigned long ticks = timer_get_ticks();
	unsigned long hz = timer_clock_hz();
	unsigned long secs = ticks / hz;
	unsigned long frac = ((ticks % hz) * 1000UL) / hz;
	char buf[12];
	itoa((int)secs, buf, 12);
	uart_puts(buf);
	uart_puts(".");
	if(frac < 100) uart_puts("0");
	if(frac < 10) uart_puts("0");
	itoa((int)frac, buf, 12);
	uart_puts(buf);
	uart_puts("s\n");
}

static void el_command(){
	kprintf("Current exception level: EL%d\n", cpu_get_el());
}

static void panic_command(){
	panic("user triggered panic from shell");
}

static void crashtest_command(const char *arg){
	if(str_eq(arg, "null")){
		kprintf("dereferencing NULL pointer...\n");
		volatile int *p = (volatile int *)0;
		*p = 42;
	} else if(str_eq(arg, "assert")){
		kprintf("triggering failed assert...\n");
		ASSERT(1 == 2);
	} else if(str_eq(arg, "brk")){
		kprintf("triggering BRK...\n");
		__asm__ volatile("brk #1");
	} else if(str_eq(arg, "undef")){
		kprintf("executing undefined instruction...\n");
		__asm__ volatile(".word 0x00000000");
	} else if(str_eq(arg, "align")){
		kprintf("triggering unaligned access...\n");
		volatile unsigned long *p = (volatile unsigned long *)0x80003;
		(void)*p;
	} else {
		kprintf("usage: crashtest <null|assert|brk|undef|align>\n");
	}
}

static void ticks_command(){
	kprintf("timer IRQ count: %d\n", timer_get_irq_count());
}

static void irqtest_command(){
	kprintf("waiting 3 seconds, counting timer IRQs...\n");
	unsigned long before = timer_get_irq_count();
	delay_ms(3000);
	unsigned long after = timer_get_irq_count();
	kprintf("timer IRQs in 3s: %d\n", after - before);
}

// peek <hex_addr>
static void peek_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	volatile unsigned int *ptr = (volatile unsigned int *)addr;
	uart_puts("[");
	uart_puthex(addr);
	uart_puts("] = ");
	uart_puthex(*ptr);
	uart_puts("\n");
}

// poke <hex_addr> <hex_val>
static void poke_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	arg = next_arg(arg);
	unsigned long val = k_hextoul(arg);
	volatile unsigned int *ptr = (volatile unsigned int *)addr;
	*ptr = (unsigned int)val;
	uart_puts("[");
	uart_puthex(addr);
	uart_puts("] <- ");
	uart_puthex(val);
	uart_puts("\n");
}

// dump <hex_addr> [count]
static void dump_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	arg = next_arg(arg);
	int count = 8;
	if(*arg >= '0' && *arg <= '9')
		count = k_atoi(arg);
	if(count > 64) count = 64;

	volatile unsigned int *ptr = (volatile unsigned int *)addr;
	for(int i = 0; i < count; i++){
		if(i % 4 == 0){
			uart_puthex(addr + i * 4);
			uart_puts(": ");
		}
		uart_puthex(ptr[i]);
		uart_puts(" ");
		if(i % 4 == 3)
			uart_puts("\n");
	}
	if(count % 4 != 0)
		uart_puts("\n");
}

static void heapinfo_command(){
	kprintf("heap used: %x (%d KB)\n", alloc_used(), alloc_used() / 1024);
	kprintf("heap free: %x (%d MB)\n", alloc_free(), alloc_free() / 1024 / 1024);
	kprintf("free blocks: %d\n", alloc_free_blocks());
}

// malloc <size>
static void malloc_command(const char *arg){
	unsigned long size;
	if(arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X'))
		size = k_hextoul(arg);
	else
		size = k_atoi(arg);
	void *ptr = kmalloc(size);
	if(ptr)
		kprintf("allocated %d bytes at %p\n", size, ptr);
	else
		kprintf("allocation failed (out of memory)\n");
}

// free <hex_addr>
static void free_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	kfree((void *)addr);
	kprintf("freed %x\n", addr);
}

// --- task commands ---

static void tasks_command(){
	static const char *state_names[] = {"unused", "ready", "running", "dead", "sleeping"};
	char buf[20];
	int n = task_count();

	// count alive tasks
	int alive = 0;
	extern task_t tasks[];
	for(int i = 0; i < n; i++){
		if(tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_DEAD)
			alive++;
	}
	itoa(alive, buf, 20);
	uart_puts("tasks: ");
	uart_puts(buf);
	uart_puts(" active\n");

	for(int i = 0; i < n; i++){
		if(tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_DEAD)
			continue;
		uart_puts("  [");
		itoa(tasks[i].id, buf, 20);
		uart_puts(buf);
		uart_puts("] ");
		uart_puts(tasks[i].name);
		uart_puts("  ");
		uart_puts(state_names[tasks[i].state]);
		if(tasks[i].state == TASK_SLEEPING){
			uart_puts("  wake=");
			uart_puthex(tasks[i].wake_time);
		}
		uart_puts("\n");
	}
}

static void kill_command(const char *arg){
	int id = k_atoi(arg);
	if(id == 0){
		uart_puts("cannot kill kernel task\n");
		return;
	}
	if(task_kill(id) < 0){
		uart_puts("failed to kill task ");
	} else {
		uart_puts("killed task ");
	}
	char buf[12];
	itoa(id, buf, 12);
	uart_puts(buf);
	uart_puts("\n");
}

// a demo background task that counts and prints periodically
static void counter_task_fn(void){
	int count = 0;
	while(1){
		count++;
		if(count % 5 == 0){
			char buf[20];
			uart_puts("[counter] ");
			itoa(count, buf, 20);
			uart_puts(buf);
			uart_puts("\n");
		}
		task_sleep_ms(500);
	}
}

static void spawn_command(){
	int id = task_create_named(counter_task_fn, "counter");
	if(id < 0){
		kprintf("failed to create task\n");
		return;
	}
	kprintf("spawned task %d\n", id);
}

static void sleep_command(const char *arg){
	unsigned long ms = k_atoi(arg);
	char buf[20];
	itoa((int)ms, buf, 20);
	uart_puts("sleeping ");
	uart_puts(buf);
	uart_puts("ms...\n");
#if PLATFORM_INIT_IRQS
	task_sleep_ms(ms);
#else
	delay_ms(ms);
#endif
	uart_puts("woke up\n");
}

static void yield_command(){
	uart_puts("yielding...\n");
	yield();
	uart_puts("returned to shell\n");
}

// --- framebuffer test ---

static void fbtest_command(){
#if !PLATFORM_HAS_FRAMEBUFFER
	unsupported_command("framebuffer");
	return;
#else
	if(fb_width() == 0){
		kprintf("no framebuffer\n");
		return;
	}
	// draw colored rectangles
	fb_clear(0xFF000020);
	fb_fill_rect(20, 20, 100, 80, 0xFFFF0000);  // red
	fb_fill_rect(140, 20, 100, 80, 0xFF00FF00);  // green
	fb_fill_rect(260, 20, 100, 80, 0xFF0000FF);  // blue
	fb_fill_rect(380, 20, 100, 80, 0xFFFFFF00);  // yellow

	// draw text
	fb_put_string(20, 120, "HudOS v1.0", 0xFFFFFFFF, 0xFF000020);
	fb_put_string(20, 140, "Bare metal Raspberry Pi 4 OS", 0xFF00FF00, 0xFF000020);
	fb_put_string(20, 170, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0xFFFFFFFF, 0xFF000020);
	fb_put_string(20, 180, "abcdefghijklmnopqrstuvwxyz", 0xFFFFFFFF, 0xFF000020);
	fb_put_string(20, 190, "0123456789 !@#$%^&*()-=+[]", 0xFFFFFFFF, 0xFF000020);

	// reset console below test area
	fb_console_init(0xFFFFFFFF, 0xFF000020);
	kprintf("fbtest: drew test pattern\n");
#endif
}

// --- spinlock test ---

static void locktest_command(){
	spinlock_t test = SPINLOCK_INIT;
	kprintf("spinlock test:\n");
	kprintf("  trylock: %d\n", spin_trylock(&test));   // should be 1 (acquired)
	kprintf("  trylock: %d\n", spin_trylock(&test));   // should be 0 (already held)
	spin_unlock(&test);
	kprintf("  unlocked, trylock: %d\n", spin_trylock(&test)); // should be 1 again
	spin_unlock(&test);
	kprintf("  lock/unlock pair... ");
	spin_lock(&test);
	spin_unlock(&test);
	kprintf("ok\n");
}

// --- semaphore producer/consumer demo ---

static semaphore_t demo_sem;
static volatile int demo_data;

static void producer_fn(void){
	for(int i = 1; i <= 5; i++){
		demo_data = i;
		kprintf("[producer] sent %d\n", i);
		sem_signal(&demo_sem);
		task_sleep_ms(200);
	}
}

static void consumer_fn(void){
	for(int i = 0; i < 5; i++){
		sem_wait(&demo_sem);
		kprintf("[consumer] got %d\n", demo_data);
	}
	kprintf("[consumer] done\n");
}

static void semtest_command(){
	sem_init(&demo_sem, 0);
	demo_data = 0;
	int c = task_create_named(consumer_fn, "consumer");
	int p = task_create_named(producer_fn, "producer");
	if(c < 0 || p < 0){
		kprintf("failed to create tasks\n");
		return;
	}
	kprintf("spawned producer=%d consumer=%d\n", p, c);
}

// --- message queue ping-pong demo ---

#define MSG_PING 1
#define MSG_PONG 2

static msgqueue_t mq_a; // pinger sends here
static msgqueue_t mq_b; // ponger sends here

static void pinger_fn(void){
	for(int i = 1; i <= 5; i++){
		kprintf("[ping] sending %d\n", i);
		mq_send(&mq_a, MSG_PING, &i, sizeof(i));
		msg_t reply;
		mq_recv(&mq_b, &reply);
		int val;
		memcpy(&val, reply.data, sizeof(val));
		kprintf("[ping] got pong %d\n", val);
	}
	kprintf("[ping] done\n");
}

static void ponger_fn(void){
	for(int i = 0; i < 5; i++){
		msg_t msg;
		mq_recv(&mq_a, &msg);
		int val;
		memcpy(&val, msg.data, sizeof(val));
		kprintf("[pong] got %d, replying\n", val);
		val *= 10;
		mq_send(&mq_b, MSG_PONG, &val, sizeof(val));
	}
	kprintf("[pong] done\n");
}

static void mqtest_command(){
	mq_init(&mq_a);
	mq_init(&mq_b);
	int p = task_create_named(pinger_fn, "pinger");
	int q = task_create_named(ponger_fn, "ponger");
	if(p < 0 || q < 0){
		kprintf("failed to create tasks\n");
		return;
	}
	kprintf("spawned pinger=%d ponger=%d\n", p, q);
}

// --- mutex shared counter demo ---

static mutex_t demo_mutex;
static volatile int shared_counter;

static void counter_a_fn(void){
	for(int i = 0; i < 10; i++){
		mutex_lock(&demo_mutex);
		shared_counter++;
		int val = shared_counter;
		mutex_unlock(&demo_mutex);
		kprintf("[A] counter=%d\n", val);
		yield();
	}
}

static void counter_b_fn(void){
	for(int i = 0; i < 10; i++){
		mutex_lock(&demo_mutex);
		shared_counter++;
		int val = shared_counter;
		mutex_unlock(&demo_mutex);
		kprintf("[B] counter=%d\n", val);
		yield();
	}
}

static void mutextest_command(){
	mutex_init(&demo_mutex);
	shared_counter = 0;
	int a = task_create_named(counter_a_fn, "counter-A");
	int b = task_create_named(counter_b_fn, "counter-B");
	if(a < 0 || b < 0){
		kprintf("failed to create tasks\n");
		return;
	}
	kprintf("spawned A=%d B=%d, yield to run\n", a, b);
}

// --- user mode demo ---

// this function runs at EL1 — can only use syscalls, no direct UART/MMIO
static void user_demo_fn(void){
	sys_puts("[user] hello from EL1!\n");
	for(int i = 0; i < 3; i++){
		sys_puts("[user] tick\n");
		sys_yield();
	}
	sys_puts("[user] exiting\n");
	sys_exit();
}

static void uspawn_command(){
	int id = user_task_create(user_demo_fn, "user-demo");
	if(id < 0){
		kprintf("failed to create user task\n");
		return;
	}
	kprintf("spawned user task %d\n", id);
}

// --- filesystem commands ---

static void pwd_command(void){
	uart_puts(cwd);
	uart_puts("\n");
}

static void cd_command(const char *arg){
	char path[128];
	build_path(path, 128, arg);
	normalize_path(path);
	int inode = vfs_resolve(path);
	if(inode < 0 || vfs_inode_type(inode) != VFS_DIR){
		uart_puts("no such directory: ");
		uart_puts(arg);
		uart_puts("\n");
		return;
	}
	int i = 0;
	while(path[i] && i < 127){ cwd[i] = path[i]; i++; }
	cwd[i] = '\0';
}

static void ls_command(const char *arg){
	char path[128];
	if(arg && arg[0])
		build_path(path, 128, arg);
	else {
		int i = 0;
		while(cwd[i] && i < 127){ path[i] = cwd[i]; i++; }
		path[i] = '\0';
	}
	int dir = vfs_resolve(path);
	if(dir < 0 || vfs_inode_type(dir) != VFS_DIR){
		uart_puts("no such directory: ");
		uart_puts(path);
		uart_puts("\n");
		return;
	}
	int found = 0;
	char buf[20];
	for(int i = 0; i < vfs_max_inodes(); i++){
		if(!vfs_inode_used(i) || vfs_inode_parent(i) != dir) continue;
		found++;
		int t = vfs_inode_type(i);
		uart_puts(t == VFS_DIR ? "  d  " : "  f  ");
		uart_puts(vfs_inode_name(i));
		if(t == VFS_FILE){
			uart_puts("  ");
			itoa(vfs_inode_size(i), buf, 20);
			uart_puts(buf);
			uart_puts("B");
		}
		uart_puts("\n");
	}
	if(!found) uart_puts("(empty)\n");
}

static void mkdir_command(const char *arg){
	char path[128];
	build_path(path, 128, arg);
	if(vfs_mkdir(path) < 0){
		uart_puts("mkdir failed: ");
		uart_puts(arg);
		uart_puts("\n");
	} else {
		uart_puts("created dir '");
		uart_puts(arg);
		uart_puts("'\n");
	}
}

static void mkfile_command(const char *arg){
	char path[128];
	build_path(path, 128, arg);
	int fd = vfs_open(path, O_WRONLY | O_CREAT);
	if(fd < 0){
		uart_puts("failed to create file\n");
		return;
	}
	vfs_close(fd);
	uart_puts("created '");
	uart_puts(arg);
	uart_puts("'\n");
}

// write <name> <data>
static void write_command(const char *arg){
	const char *name_start = arg;
	const char *data = next_arg(arg);
	char name[32];
	int i = 0;
	while(name_start[i] && name_start[i] != ' ' && i < 31){
		name[i] = name_start[i]; i++;
	}
	name[i] = '\0';

	char path[128];
	build_path(path, 128, name);
	int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
	if(fd < 0){
		uart_puts("failed to open/create file\n");
		return;
	}
	int len = k_strlen(data);
	vfs_write(fd, data, len);
	vfs_close(fd);
	char buf[12];
	itoa(len, buf, 12);
	uart_puts("wrote ");
	uart_puts(buf);
	uart_puts(" bytes to '");
	uart_puts(name);
	uart_puts("'\n");
}

static void cat_command(const char *arg){
	char path[128];
	build_path(path, 128, arg);
	int fd = vfs_open(path, O_RDONLY);
	if(fd < 0){
		uart_puts("file not found: ");
		uart_puts(arg);
		uart_puts("\n");
		return;
	}
	char buf[128];
	int last = '\n';
	while(1){
		int len = vfs_read(fd, buf, sizeof(buf) - 1);
		if(len <= 0)
			break;
		buf[len] = '\0';
		uart_puts(buf);
		last = (unsigned char)buf[len - 1];
	}
	vfs_close(fd);
	if(last != '\n') uart_puts("\n");
}

static void rm_command(const char *arg){
	char path[128];
	build_path(path, 128, arg);
	if(vfs_unlink(path) < 0){
		uart_puts("rm failed (not found or dir not empty): ");
		uart_puts(arg);
		uart_puts("\n");
	} else {
		uart_puts("removed '");
		uart_puts(arg);
		uart_puts("'\n");
	}
}

static void toycc_command(const char *arg){
	char src_name[64];
	char out_name[64];
	int i = 0;

	while(*arg == ' ') arg++;
	while(arg[i] && arg[i] != ' ' && i < (int)sizeof(src_name) - 1){
		src_name[i] = arg[i];
		i++;
	}
	src_name[i] = '\0';
	arg += i;
	while(*arg == ' ') arg++;

	if(src_name[0] == '\0' || !str_starts_with(arg, "-o ")){
		uart_puts("usage: toycc <src> -o <out>\n");
		return;
	}

	arg += 3;
	while(*arg == ' ') arg++;
	i = 0;
	while(arg[i] && arg[i] != ' ' && i < (int)sizeof(out_name) - 1){
		out_name[i] = arg[i];
		i++;
	}
	out_name[i] = '\0';

	if(out_name[0] == '\0'){
		uart_puts("usage: toycc <src> -o <out>\n");
		return;
	}

	char src_path[128];
	char out_path[128];
	build_path(src_path, 128, src_name);
	build_path(out_path, 128, out_name);

	if(tinycc_compile(src_path, out_path) < 0){
		uart_puts("toycc failed\n");
		uart_puts("supported subset: int vars, assignments/+=/++/--, if/else, do/while/for, break/continue, ! && ||, +-*/% comparisons, u_puts(\"...\"), u_putc(expr), u_sleep(expr), return expr;\n");
		return;
	}

	uart_puts("toycc compiled '");
	uart_puts(src_name);
	uart_puts("' -> '");
	uart_puts(out_name);
	uart_puts("'\n");
}

static void tcc_command(const char *arg){
	(void)arg;
	uart_puts("tcc is the real Tiny C compiler on the host side.\n");
	uart_puts("use ./tcc <src> -o <out> in the repo, or use toycc here for the in-kernel subset.\n");
}

// exec <vfs-path>  — load ELF64 binary from VFS and run it as a user task
static void exec_command(const char *arg){
	char path[128];
	build_path(path, 128, arg);

	// derive a short task name from the filename
	const char *name = arg;
	for(const char *p = arg; *p; p++)
		if(*p == '/') name = p + 1;

	int tid;
	if(tinycc_is_program(path))
		tid = tinycc_exec(path, name);
	else
		tid = elf_exec(path, name);
	if(tid < 0){
		uart_puts("exec failed\n");
	} else {
		char buf[12];
		itoa(tid, buf, 12);
		uart_puts("exec: task ");
		uart_puts(buf);
		uart_puts(" started\n");
		if(!PLATFORM_INIT_IRQS){
			uart_puts("exec: switching to task now\n");
			yield();
			uart_puts("exec: task returned\n");
		}
	}
}

static void exec_shortcut_command(const char *arg){
	if(!arg || !*arg){
		uart_puts("usage: ./<program>\n");
		return;
	}
	exec_command(arg);
}

// --- SD card / FAT32 commands ---

static void sdinit_command(void){
	if(sd_init() == 0){
		uart_puts("SD card initialized\n");
		if(fat32_mount() == 0)
			uart_puts("FAT32 partition mounted\n");
		else
			uart_puts("FAT32 mount failed (no FAT32 partition?)\n");
	} else {
		uart_puts("SD init failed\n");
	}
}

// callback used by sdls
static void sdls_cb(const char *name, int is_dir, unsigned int size){
	uart_puts(is_dir ? "  d  " : "  f  ");
	uart_puts(name);
	if(!is_dir){
		uart_puts("  ");
		char buf[16];
		itoa((int)size, buf, 16);
		uart_puts(buf);
		uart_puts("B");
	}
	uart_puts("\n");
}

static void sdls_command(const char *arg){
	if(!fat32_mounted()){
		uart_puts("SD not mounted — run sdinit first\n");
		return;
	}
	const char *path = (arg && arg[0]) ? arg : "/";
	uart_puts("SD:");
	uart_puts(path);
	uart_puts("\n");
	if(fat32_readdir(path, sdls_cb) < 0)
		uart_puts("(not found or not a directory)\n");
}

// Max size we'll try to load from SD for cat/exec
#define SD_MAX_LOAD (256 * 1024u)

static void sdcat_command(const char *arg){
	if(!arg || !arg[0]){ uart_puts("usage: sdcat <path>\n"); return; }
	if(!fat32_mounted()){ uart_puts("SD not mounted\n"); return; }

	unsigned char *buf = (unsigned char *)kmalloc(SD_MAX_LOAD);
	if(!buf){ uart_puts("out of memory\n"); return; }

	int n = fat32_load(arg, buf, SD_MAX_LOAD);
	if(n < 0){
		uart_puts("not found: ");
		uart_puts(arg);
		uart_puts("\n");
	} else {
		buf[n] = '\0';
		uart_puts((char *)buf);
		if(n > 0 && buf[n-1] != '\n') uart_puts("\n");
	}
	kfree(buf);
}

static void sdexec_command(const char *arg){
	if(!arg || !arg[0]){ uart_puts("usage: sdexec <path>\n"); return; }
	if(!fat32_mounted()){ uart_puts("SD not mounted\n"); return; }

	unsigned char *buf = (unsigned char *)kmalloc(SD_MAX_LOAD);
	if(!buf){ uart_puts("out of memory\n"); return; }

	int n = fat32_load(arg, buf, SD_MAX_LOAD);
	if(n < 0){
		uart_puts("not found: ");
		uart_puts(arg);
		uart_puts("\n");
		kfree(buf);
		return;
	}

	// derive short task name from filename
	const char *name = arg;
	for(const char *p = arg; *p; p++)
		if(*p == '/') name = p + 1;

	int tid = elf_exec_buf(buf, (unsigned int)n, name);
	kfree(buf);
	if(tid < 0){
		uart_puts("exec failed\n");
	} else {
		char tmp[12];
		itoa(tid, tmp, 12);
		uart_puts("exec: task ");
		uart_puts(tmp);
		uart_puts(" started\n");
	}
}

// mount: copy every regular file under SD "/" into VFS "/bin"
// collect_cb and mount_command share static arrays (mount is synchronous).
#define MOUNT_MAX_FILES 32
static char mount_names[MOUNT_MAX_FILES][13];
static int  mount_is_dir[MOUNT_MAX_FILES];
static int  mount_count;

static void mount_collect_cb(const char *name, int is_dir, unsigned int sz){
	(void)sz;
	if(mount_count >= MOUNT_MAX_FILES) return;
	int j = 0;
	while(name[j] && j < 12){ mount_names[mount_count][j] = name[j]; j++; }
	mount_names[mount_count][j] = '\0';
	mount_is_dir[mount_count] = is_dir;
	mount_count++;
}

static unsigned char mount_buf[SD_MAX_LOAD];

static void mount_command(void){
	if(!fat32_mounted()){ uart_puts("SD not mounted — run sdinit first\n"); return; }

	mount_count = 0;
	fat32_readdir("/", mount_collect_cb);

	int copied = 0;
	for(int i = 0; i < mount_count; i++){
		if(mount_is_dir[i]) continue;

		// build SD path
		char sdpath[16];
		sdpath[0] = '/';
		int j = 0;
		while(mount_names[i][j] && j < 13){ sdpath[j+1] = mount_names[i][j]; j++; }
		sdpath[j+1] = '\0';

		int n = fat32_load(sdpath, mount_buf, SD_MAX_LOAD);
		if(n < 0) continue;

		// write into VFS /bin/<name>
		char vpath[32];
		vpath[0]='/'; vpath[1]='b'; vpath[2]='i'; vpath[3]='n'; vpath[4]='/';
		j = 0;
		while(mount_names[i][j] && j < 12){ vpath[5+j] = mount_names[i][j]; j++; }
		vpath[5+j] = '\0';

		int fd = vfs_open(vpath, O_WRONLY | O_CREAT | O_TRUNC);
		if(fd < 0){ uart_puts("vfs open failed: "); uart_puts(vpath); uart_puts("\n"); continue; }
		vfs_write(fd, mount_buf, n);
		vfs_close(fd);

		uart_puts("  mounted ");
		uart_puts(vpath);
		uart_puts("\n");
		copied++;
	}

	char buf[12];
	itoa(copied, buf, 12);
	uart_puts("mount: ");
	uart_puts(buf);
	uart_puts(" file(s) copied to /bin\n");
}

// --- self-test commands ---

static void test_uart_command(void){
	int r = test_uart();
	if(r == 0) kprintf("test_uart: PASSED\n");
	else        kprintf("test_uart: %d FAILURE(S)\n", r);
}

static void test_gpio_command(void){
	int r = test_gpio();
	if(r == 0) kprintf("test_gpio: PASSED\n");
	else        kprintf("test_gpio: %d FAILURE(S)\n", r);
}

static void test_timer_command(void){
	int r = test_timer();
	if(r == 0) kprintf("test_timer: PASSED\n");
	else        kprintf("test_timer: %d FAILURE(S)\n", r);
}

static void test_alloc_command(void){
	int r = test_alloc();
	if(r == 0) kprintf("test_alloc: PASSED\n");
	else        kprintf("test_alloc: %d FAILURE(S)\n", r);
}

static void test_all_command(void){
	int total = 0;
	kprintf("Running all self-tests...\n");
	total += test_uart();
	total += test_gpio();
	total += test_timer();
	total += test_alloc();
	kprintf("==============================\n");
	if(total == 0) kprintf("ALL TESTS PASSED\n");
	else            kprintf("TOTAL FAILURES: %d\n", total);
	kprintf("==============================\n");
}

static void command_error(){
	uart_puts("unrecognized command\n");
}

static void check_keywords(const char *buffer){
	if(str_eq(buffer, "help")){
		help_command();
	} else if(str_eq(buffer, "info")){
		info_command();
	} else if(str_eq(buffer, "why")){
		why_command();
	} else if(str_eq(buffer, "clear")){
		clear_command();
	} else if(str_starts_with(buffer, "echo ")){
		echo_command(buffer + 5);
	} else if(str_starts_with(buffer, "led ")){
		led_command(buffer + 4);
	} else if(str_starts_with(buffer, "blink ")){
		blink_command(buffer + 6);
	} else if(str_starts_with(buffer, "readpin ")){
		readpin_command(buffer + 8);
	} else if(str_eq(buffer, "uptime")){
		uptime_command();
	} else if(str_eq(buffer, "el")){
		el_command();
	} else if(str_eq(buffer, "panic")){
		panic_command();
	} else if(str_starts_with(buffer, "crashtest ")){
		crashtest_command(buffer + 10);
	} else if(str_eq(buffer, "crashtest")){
		crashtest_command("");
	} else if(str_eq(buffer, "ticks")){
		ticks_command();
	} else if(str_eq(buffer, "irqtest")){
		irqtest_command();
	} else if(str_eq(buffer, "timerdbg")){
		irq_dump_gic();
	} else if(str_starts_with(buffer, "peek ")){
		peek_command(buffer + 5);
	} else if(str_starts_with(buffer, "poke ")){
		poke_command(buffer + 5);
	} else if(str_starts_with(buffer, "dump ")){
		dump_command(buffer + 5);
	} else if(str_eq(buffer, "heapinfo")){
		heapinfo_command();
	} else if(str_starts_with(buffer, "malloc ")){
		malloc_command(buffer + 7);
	} else if(str_starts_with(buffer, "free ")){
		free_command(buffer + 5);
	} else if(str_eq(buffer, "tasks")){
		tasks_command();
	} else if(str_eq(buffer, "spawn")){
		spawn_command();
	} else if(str_starts_with(buffer, "kill ")){
		kill_command(buffer + 5);
	} else if(str_starts_with(buffer, "sleep ")){
		sleep_command(buffer + 6);
	} else if(str_eq(buffer, "yield")){
		yield_command();
	} else if(str_eq(buffer, "uspawn")){
		uspawn_command();
	} else if(str_eq(buffer, "fbtest")){
		fbtest_command();
	} else if(str_eq(buffer, "fbmirror")){
#if PLATFORM_HAS_FRAMEBUFFER
		int m = !fb_get_mirror();
		fb_set_mirror(m);
		kprintf("fb mirror: %s\n", m ? "on" : "off");
#else
		unsupported_command("framebuffer");
#endif
	} else if(str_eq(buffer, "home stop")){
#if PLATFORM_HAS_FRAMEBUFFER
		home_stop();
		kprintf("desktop stopped\n");
#else
		unsupported_command("framebuffer");
#endif
	} else if(str_eq(buffer, "home")){
#if PLATFORM_HAS_FRAMEBUFFER
		if(fb_width() == 0){ kprintf("no framebuffer\n"); }
		else if(home_active()){ kprintf("desktop already running\n"); }
		else { home_start(); kprintf("desktop started\n"); }
#else
		unsupported_command("framebuffer");
#endif
	} else if(str_eq(buffer, "locktest")){
		locktest_command();
	} else if(str_eq(buffer, "semtest")){
		semtest_command();
	} else if(str_eq(buffer, "mqtest")){
		mqtest_command();
	} else if(str_eq(buffer, "mutextest")){
		mutextest_command();
	} else if(str_eq(buffer, "pwd")){
		pwd_command();
	} else if(str_starts_with(buffer, "cd ")){
		cd_command(buffer + 3);
	} else if(str_starts_with(buffer, "ls ")){
		ls_command(buffer + 3);
	} else if(str_eq(buffer, "ls")){
		ls_command("");
	} else if(str_starts_with(buffer, "mkdir ")){
		mkdir_command(buffer + 6);
	} else if(str_starts_with(buffer, "mkfile ")){
		mkfile_command(buffer + 7);
	} else if(str_starts_with(buffer, "write ")){
		write_command(buffer + 6);
	} else if(str_starts_with(buffer, "cat ")){
		cat_command(buffer + 4);
	} else if(str_starts_with(buffer, "toycc ")){
		toycc_command(buffer + 6);
	} else if(str_starts_with(buffer, "tcc ")){
		tcc_command(buffer + 4);
	} else if(str_starts_with(buffer, "vi ")){
		vi_open_command(buffer + 3);
	} else if(str_starts_with(buffer, "rm ")){
		rm_command(buffer + 3);
	} else if(str_starts_with(buffer, "./")){
		exec_shortcut_command(buffer + 2);
	} else if(str_starts_with(buffer, "exec ")){
		exec_command(buffer + 5);
	} else if(str_eq(buffer, "kbinit")){
#if PLATFORM_HAS_USB_KEYBOARD
		uart_puts("initializing USB keyboard...\n");
		if(kb_init() == 0)
			uart_puts("keyboard ready\n");
		else
			uart_puts("keyboard init failed (no USB keyboard?)\n");
#else
		unsupported_command("USB keyboard");
#endif
	} else if(str_eq(buffer, "sdinit")){
#if PLATFORM_HAS_SD
		sdinit_command();
#else
		unsupported_command("sd card");
#endif
	} else if(str_starts_with(buffer, "sdls ")){
#if PLATFORM_HAS_SD
		sdls_command(buffer + 5);
#else
		unsupported_command("sd card");
#endif
	} else if(str_eq(buffer, "sdls")){
#if PLATFORM_HAS_SD
		sdls_command("/");
#else
		unsupported_command("sd card");
#endif
	} else if(str_starts_with(buffer, "sdcat ")){
#if PLATFORM_HAS_SD
		sdcat_command(buffer + 6);
#else
		unsupported_command("sd card");
#endif
	} else if(str_starts_with(buffer, "sdexec ")){
#if PLATFORM_HAS_SD
		sdexec_command(buffer + 7);
#else
		unsupported_command("sd card");
#endif
	} else if(str_eq(buffer, "mount")){
#if PLATFORM_HAS_SD
		mount_command();
#else
		unsupported_command("sd card");
#endif
	} else if(str_eq(buffer, "test_uart")){
		test_uart_command();
	} else if(str_eq(buffer, "test_gpio")){
#if PLATFORM_HAS_GPIO
		test_gpio_command();
#else
		unsupported_command("gpio");
#endif
	} else if(str_eq(buffer, "test_timer")){
		test_timer_command();
	} else if(str_eq(buffer, "test_alloc")){
		test_alloc_command();
	} else if(str_eq(buffer, "test_all")){
		test_all_command();
	} else {
		command_error();
	}
}

// Read one character from either the USB keyboard or the UART IRQ ring buffer.
// Blocks until a character is available; polls keyboard every ~1ms.
static char shell_getc(void){
	while(1){
		int c = shell_try_getc();
		if(c >= 0) return (char)c;
		delay_us(1000);
	}
}

static int shell_try_getc(void){
	if(shell_unget_buf >= 0){
		int c = shell_unget_buf;
		shell_unget_buf = -1;
		return c;
	}
	kb_poll();
	if(kb_ready()) return (unsigned char)kb_getc();
	int c = uart_irq_getc();
	if(c >= 0) return c;
	return uart_poll_getc();
}

static void shell_ungetc(char c){
	shell_unget_buf = (unsigned char)c;
}

static void query_terminal(char *terminalBuffer, int maxLen){
	while(1){
		uart_puts(">");
		// Read a line from either USB keyboard or UART
		int i = 0;
		int history_index = shell_history_count;
		while(i < maxLen - 1){
			int c = vi_read_key();
			if(c == '\r' || c == '\n') break;
			if(c == VI_KEY_UP){
				if(shell_history_count > 0 && history_index > 0){
					history_index--;
					shell_replace_line(terminalBuffer, maxLen, shell_history[history_index], &i);
				}
				continue;
			}
			if(c == VI_KEY_DOWN){
				if(history_index < shell_history_count - 1){
					history_index++;
					shell_replace_line(terminalBuffer, maxLen, shell_history[history_index], &i);
				} else if(history_index == shell_history_count - 1){
					history_index = shell_history_count;
					shell_replace_line(terminalBuffer, maxLen, "", &i);
				}
				continue;
			}
			if(c == '\b' || c == 0x7f){  // backspace / delete
				if(i > 0){
					uart_putc('\b'); uart_putc(' '); uart_putc('\b');
					i--;
				}
				continue;
			}
			if(c < 32 || c > 126) continue; // ignore non-printable
			terminalBuffer[i++] = c;
			uart_putc(c);    // echo
		}
		terminalBuffer[i] = '\0';
		uart_puts("\n");
		shell_store_history(terminalBuffer);
		check_keywords(terminalBuffer);
	}
}

void kernel_main(void) {
	int terminal_Len = SHELL_LINE_MAX;
	char terminal_Buffer[terminal_Len];

	cpu_install_vectors();
	uart_init();

	kprintf("\n");
	kprintf("============================================\n");
	kprintf("  %s v%s\n", HUDOS_NAME, HUDOS_VERSION);
	kprintf("  Bare metal %s OS\n", HUDOS_ARCH);
	kprintf("  Board: %s\n", HUDOS_BOARD);
	kprintf("  Running at %s\n", HUDOS_EL);
	kprintf("  Built: %s %s\n", __DATE__, __TIME__);
	kprintf("  Author: %s\n", HUDOS_AUTHOR);
	kprintf("============================================\n");
	kprintf("\n");

	// initialize heap (needed before MMU for page table alloc)
	alloc_init();
	kprintf("heap initialized\n");

	if(PLATFORM_INIT_MMU){
		mmu_init();
		kprintf("MMU enabled\n");
	} else {
		kprintf("MMU skipped on this platform\n");
	}

	if(PLATFORM_HAS_FRAMEBUFFER){
		if(fb_init(640, 480) == 0){
			kprintf("framebuffer: 640x480x32 pitch=%d\n", fb_pitch());
			fb_console_init(0xFFFFFFFF, 0xFF000020); // white on dark blue
			uart_set_output_hook(fb_console_putc);
			fb_console_puts("HudOS framebuffer initialized\n");
		} else {
			kprintf("framebuffer: init failed (serial only)\n");
		}
	} else {
		kprintf("framebuffer: unavailable on this platform\n");
	}

	if(PLATFORM_INIT_IRQS){
		irq_init();
		timer_init(10);              // timer IRQ every 10ms (100Hz tick)
		uart_irq_enable();
		irq_enable(IRQ_UART);
		cpu_enable_irqs();
		kprintf("IRQs enabled (timer 10ms, UART RX)\n");
	} else {
		kprintf("IRQs skipped on this platform (polling serial)\n");
	}
	kprintf("timer freq: %x Hz\n", timer_get_freq());

	// initialize task system
	task_init();
	kprintf("scheduler initialized\n");

	// initialize virtual filesystem and standard directory tree
	vfs_init();
	vfs_mkdir("/bin");     // executables
	vfs_mkdir("/Code");    // demo source files
	vfs_mkdir("/etc");     // config files
	vfs_mkdir("/home");    // user home
	vfs_mkdir("/var");
	vfs_mkdir("/var/log"); // logs
	kprintf("vfs initialized\n");
	install_embedded_user_app();
	install_code_demo();

	if(PLATFORM_HAS_USB_KEYBOARD){
		if(kb_init() == 0)
			kprintf("USB keyboard initialized\n");
		else
			kprintf("USB keyboard not detected; serial input still available\n");
	} else {
		kprintf("USB keyboard support unavailable on this platform\n");
	}

	query_terminal(terminal_Buffer, terminal_Len);

	while (1) {
	}
}
