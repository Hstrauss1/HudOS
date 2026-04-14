#include "tinycc.h"
#include "alloc.h"
#include "platform.h"
#include "string.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "vfs.h"

#define TCC_MAGIC0 'T'
#define TCC_MAGIC1 'C'
#define TCC_MAGIC2 'C'
#define TCC_MAGIC3 '0'

#define TCC_OP_PUTS  1
#define TCC_OP_SLEEP 2
#define TCC_OP_EXIT  3
#define TCC_OP_PUTC  4
#define TCC_OP_PUSH_CONST 5
#define TCC_OP_PUSH_VAR   6
#define TCC_OP_STORE_VAR  7
#define TCC_OP_ADD        8
#define TCC_OP_SUB        9
#define TCC_OP_LT         10
#define TCC_OP_LE         11
#define TCC_OP_GT         12
#define TCC_OP_GE         13
#define TCC_OP_EQ         14
#define TCC_OP_NE         15
#define TCC_OP_NEG        16
#define TCC_OP_JMP        17
#define TCC_OP_JZ         18
#define TCC_OP_RET        19
#define TCC_OP_MUL        20
#define TCC_OP_DIV        21
#define TCC_OP_MOD        22
#define TCC_OP_NOT        23
#define TCC_OP_LAND       24
#define TCC_OP_LOR        25
#define TCC_OP_DUP        26
#define TCC_OP_POP        27

typedef struct {
	unsigned char *prog;
	int size;
} tinycc_task_ctx_t;

#define TCC_MAX_VARS 32
#define TCC_VAR_NAME_MAX 32
#define TCC_STACK_MAX 64
#define TCC_TEMP_MAX 512
#define TCC_LOOP_MAX 8
#define TCC_LOOP_PATCH_MAX 32

static tinycc_task_ctx_t tinycc_tasks[MAX_TASKS];

typedef struct {
	char name[TCC_VAR_NAME_MAX];
} tinycc_var_t;

typedef struct {
	int break_patch[TCC_LOOP_PATCH_MAX];
	int break_count;
	int continue_patch[TCC_LOOP_PATCH_MAX];
	int continue_count;
} tinycc_loop_t;

typedef struct {
	unsigned char *out;
	int cap;
	int pos;
	unsigned count;
	int vars;
	tinycc_var_t var[TCC_MAX_VARS];
	int loop_depth;
	tinycc_loop_t *loop;
} tinycc_compile_t;

static int is_space(char c){
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_digit(char c){
	return c >= '0' && c <= '9';
}

static unsigned read_u32(const unsigned char *p){
	return (unsigned)p[0] |
	       ((unsigned)p[1] << 8) |
	       ((unsigned)p[2] << 16) |
	       ((unsigned)p[3] << 24);
}

static void write_u16(unsigned char *p, unsigned value){
	p[0] = (unsigned char)(value & 0xFF);
	p[1] = (unsigned char)((value >> 8) & 0xFF);
}

static void write_u32(unsigned char *p, unsigned value){
	p[0] = (unsigned char)(value & 0xFF);
	p[1] = (unsigned char)((value >> 8) & 0xFF);
	p[2] = (unsigned char)((value >> 16) & 0xFF);
	p[3] = (unsigned char)((value >> 24) & 0xFF);
}

static void copy_name(char *dst, const char *src, int cap){
	int i = 0;
	while(src[i] && i < cap - 1){
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
}

static int emit_u8(tinycc_compile_t *ctx, unsigned value){
	if(ctx->pos + 1 > ctx->cap)
		return -1;
	ctx->out[ctx->pos++] = (unsigned char)value;
	return 0;
}

static int emit_u32(tinycc_compile_t *ctx, unsigned value){
	if(ctx->pos + 4 > ctx->cap)
		return -1;
	write_u32(ctx->out + ctx->pos, value);
	ctx->pos += 4;
	return 0;
}

static int emit_bytes(tinycc_compile_t *ctx, const unsigned char *data, int len){
	if(ctx->pos + len > ctx->cap)
		return -1;
	memcpy(ctx->out + ctx->pos, data, (unsigned long)len);
	ctx->pos += len;
	return 0;
}

static void patch_u32(tinycc_compile_t *ctx, int pos, unsigned value){
	write_u32(ctx->out + pos, value);
}

static int code_pos(const tinycc_compile_t *ctx){
	return ctx->pos - 8;
}

static void skip_ws_and_comments(const char **ps){
	const char *s = *ps;
	for(;;){
		while(is_space(*s)) s++;
		if(s[0] == '/' && s[1] == '/'){
			s += 2;
			while(*s && *s != '\n') s++;
			continue;
		}
		if(s[0] == '/' && s[1] == '*'){
			s += 2;
			while(s[0] && !(s[0] == '*' && s[1] == '/')) s++;
			if(s[0]) s += 2;
			continue;
		}
		break;
	}
	*ps = s;
}

static int skip_preprocessor_line(const char **ps, const char *end){
	const char *s = *ps;
	if(s >= end || *s != '#')
		return 0;
	while(s < end && *s && *s != '\n') s++;
	*ps = s;
	return 1;
}

static int match_kw(const char **ps, const char *kw){
	const char *s = *ps;
	int n = k_strlen(kw);
	if(k_strncmp(s, kw, n) != 0)
		return 0;
	if((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') ||
	   (s[n] >= '0' && s[n] <= '9') || s[n] == '_')
		return 0;
	*ps = s + n;
	return 1;
}

static int expect_char(const char **ps, char want){
	skip_ws_and_comments(ps);
	if(**ps != want)
		return 0;
	(*ps)++;
	return 1;
}

static int parse_number(const char **ps, int *out){
	skip_ws_and_comments(ps);
	const char *s = *ps;
	int neg = 0;
	int value = 0;
	if(*s == '-'){
		neg = 1;
		s++;
	}
	if(!is_digit(*s))
		return 0;
	while(is_digit(*s)){
		value = value * 10 + (*s - '0');
		s++;
	}
	*out = neg ? -value : value;
	*ps = s;
	return 1;
}

static int parse_c_string(const char **ps, char *out, int out_cap, int *out_len){
	skip_ws_and_comments(ps);
	const char *s = *ps;
	int len = 0;
	if(*s != '"')
		return 0;
	s++;
	while(*s && *s != '"'){
		char c = *s++;
		if(c == '\\'){
			char esc = *s++;
			if(esc == 'n') c = '\n';
			else if(esc == 'r') c = '\r';
			else if(esc == 't') c = '\t';
			else if(esc == '\\') c = '\\';
			else if(esc == '"') c = '"';
			else return 0;
		}
		if(len >= out_cap - 1)
			return 0;
		out[len++] = c;
	}
	if(*s != '"')
		return 0;
	s++;
	out[len] = '\0';
	*out_len = len;
	*ps = s;
	return 1;
}

static int parse_c_char(const char **ps, int *out){
	skip_ws_and_comments(ps);
	const char *s = *ps;
	char c;
	if(*s != '\'')
		return 0;
	s++;
	if(*s == '\\'){
		s++;
		if(*s == 'n') c = '\n';
		else if(*s == 'r') c = '\r';
		else if(*s == 't') c = '\t';
		else if(*s == '\\') c = '\\';
		else if(*s == '\'') c = '\'';
		else return 0;
		s++;
	} else {
		if(*s == '\0' || *s == '\'')
			return 0;
		c = *s++;
	}
	if(*s != '\'')
		return 0;
	s++;
	*out = (unsigned char)c;
	*ps = s;
	return 1;
}

static int is_ident_start(char c){
	return (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_char(char c){
	return is_ident_start(c) || is_digit(c);
}

static int parse_identifier(const char **ps, char *out, int cap){
	skip_ws_and_comments(ps);
	const char *s = *ps;
	int i = 0;
	if(!is_ident_start(*s))
		return 0;
	while(is_ident_char(*s) && i < cap - 1)
		out[i++] = *s++;
	out[i] = '\0';
	if(is_ident_char(*s))
		return 0;
	*ps = s;
	return 1;
}

static int match_str(const char **ps, const char *tok){
	skip_ws_and_comments(ps);
	int n = k_strlen(tok);
	if(k_strncmp(*ps, tok, n) != 0)
		return 0;
	*ps += n;
	return 1;
}

static int match_word_prefix(const char *s, const char *kw){
	int n = k_strlen(kw);
	char tail;
	if(k_strncmp(s, kw, n) != 0)
		return 0;
	tail = s[n];
	return (tail < 'a' || tail > 'z') &&
	       (tail < 'A' || tail > 'Z') &&
	       (tail < '0' || tail > '9') &&
	       tail != '_';
}

static int starts_decl_like(const char *s){
	return match_word_prefix(s, "int") ||
	       match_word_prefix(s, "void") ||
	       match_word_prefix(s, "long") ||
	       match_word_prefix(s, "char") ||
	       match_word_prefix(s, "short") ||
	       match_word_prefix(s, "unsigned") ||
	       match_word_prefix(s, "signed") ||
	       match_word_prefix(s, "const") ||
	       match_word_prefix(s, "static") ||
	       match_word_prefix(s, "extern");
}

static int skip_decl_or_header(const char **ps, const char *end, int *opens_block){
	const char *s = *ps;
	int paren_depth = 0;

	while(s < end && *s){
		char c = *s++;
		if(c == '"'){
			while(s < end && *s){
				char q = *s++;
				if(q == '\\' && s < end && *s) s++;
				else if(q == '"') break;
			}
			continue;
		}
		if(c == '\''){
			while(s < end && *s){
				char q = *s++;
				if(q == '\\' && s < end && *s) s++;
				else if(q == '\'') break;
			}
			continue;
		}
		if(c == '('){
			paren_depth++;
			continue;
		}
		if(c == ')'){
			if(paren_depth == 0) return 0;
			paren_depth--;
			continue;
		}
		if(paren_depth == 0 && (c == ';' || c == '{')){
			*opens_block = (c == '{');
			*ps = s;
			return 1;
		}
	}

	return 0;
}

static int find_var(const tinycc_compile_t *ctx, const char *name){
	for(int i = 0; i < ctx->vars; i++)
		if(str_eq(ctx->var[i].name, name))
			return i;
	return -1;
}

static int declare_var(tinycc_compile_t *ctx, const char *name){
	if(ctx->vars >= TCC_MAX_VARS || find_var(ctx, name) >= 0)
		return -1;
	copy_name(ctx->var[ctx->vars].name, name, TCC_VAR_NAME_MAX);
	return ctx->vars++;
}

static int emit_op(tinycc_compile_t *ctx, unsigned op){
	ctx->count++;
	return emit_u8(ctx, op);
}

static int emit_puts(tinycc_compile_t *ctx, const char *text, int len){
	if(emit_op(ctx, TCC_OP_PUTS) < 0 || ctx->pos + 2 + len + 1 > ctx->cap)
		return -1;
	write_u16(ctx->out + ctx->pos, (unsigned)len);
	ctx->pos += 2;
	memcpy(ctx->out + ctx->pos, text, (unsigned long)len);
	ctx->pos += len;
	ctx->out[ctx->pos++] = '\0';
	return 0;
}

static int emit_push_const(tinycc_compile_t *ctx, int value){
	if(emit_op(ctx, TCC_OP_PUSH_CONST) < 0 || emit_u32(ctx, (unsigned)value) < 0)
		return -1;
	return 0;
}

static int emit_push_var(tinycc_compile_t *ctx, int slot){
	if(emit_op(ctx, TCC_OP_PUSH_VAR) < 0 || emit_u8(ctx, (unsigned)slot) < 0)
		return -1;
	return 0;
}

static int emit_store_var(tinycc_compile_t *ctx, int slot){
	if(emit_op(ctx, TCC_OP_STORE_VAR) < 0 || emit_u8(ctx, (unsigned)slot) < 0)
		return -1;
	return 0;
}

static int emit_jump(tinycc_compile_t *ctx, unsigned op, int *patch_pos){
	if(emit_op(ctx, op) < 0)
		return -1;
	*patch_pos = ctx->pos;
	if(emit_u32(ctx, 0) < 0)
		return -1;
	return 0;
}

static tinycc_loop_t *current_loop(tinycc_compile_t *ctx){
	if(ctx->loop_depth <= 0)
		return 0;
	return &ctx->loop[ctx->loop_depth - 1];
}

static tinycc_loop_t *push_loop(tinycc_compile_t *ctx){
	tinycc_loop_t *loop;
	if(ctx->loop_depth >= TCC_LOOP_MAX)
		return 0;
	loop = &ctx->loop[ctx->loop_depth++];
	memset(loop, 0, sizeof(*loop));
	return loop;
}

static void pop_loop(tinycc_compile_t *ctx){
	if(ctx->loop_depth > 0)
		ctx->loop_depth--;
}

static int add_loop_patch(int *list, int *count, int patch_pos){
	if(*count >= TCC_LOOP_PATCH_MAX)
		return -1;
	list[*count] = patch_pos;
	(*count)++;
	return 0;
}

static void patch_loop_list(tinycc_compile_t *ctx, const int *list, int count, unsigned target){
	for(int i = 0; i < count; i++)
		patch_u32(ctx, list[i], target);
}

static int parse_expr(tinycc_compile_t *ctx, const char **ps, const char *end);
static int parse_statement(tinycc_compile_t *ctx, const char **ps, const char *end, int *saw_return);

static int parse_primary(tinycc_compile_t *ctx, const char **ps, const char *end){
	(void)end;
	int value = 0;

	skip_ws_and_comments(ps);
	if(expect_char(ps, '(')){
		if(parse_expr(ctx, ps, end) < 0 || !expect_char(ps, ')'))
			return -1;
		return 0;
	}
	if(parse_c_char(ps, &value) || parse_number(ps, &value))
		return emit_push_const(ctx, value);
	return -1;
}

static int parse_postfix(tinycc_compile_t *ctx, const char **ps, const char *end){
	char name[TCC_VAR_NAME_MAX];
	const char *save = *ps;

	if(parse_identifier(ps, name, sizeof(name))){
		int slot = find_var(ctx, name);
		if(slot < 0)
			return -1;
		if(match_str(ps, "++")){
			if(emit_push_var(ctx, slot) < 0 ||
			   emit_op(ctx, TCC_OP_DUP) < 0 ||
			   emit_push_const(ctx, 1) < 0 ||
			   emit_op(ctx, TCC_OP_ADD) < 0 ||
			   emit_store_var(ctx, slot) < 0)
				return -1;
			return 0;
		}
		if(match_str(ps, "--")){
			if(emit_push_var(ctx, slot) < 0 ||
			   emit_op(ctx, TCC_OP_DUP) < 0 ||
			   emit_push_const(ctx, 1) < 0 ||
			   emit_op(ctx, TCC_OP_SUB) < 0 ||
			   emit_store_var(ctx, slot) < 0)
				return -1;
			return 0;
		}
		return emit_push_var(ctx, slot);
	}

	*ps = save;
	return parse_primary(ctx, ps, end);
}

static int parse_unary(tinycc_compile_t *ctx, const char **ps, const char *end){
	char name[TCC_VAR_NAME_MAX];

	if(match_str(ps, "++")){
		if(!parse_identifier(ps, name, sizeof(name)))
			return -1;
		int slot = find_var(ctx, name);
		if(slot < 0 ||
		   emit_push_var(ctx, slot) < 0 ||
		   emit_push_const(ctx, 1) < 0 ||
		   emit_op(ctx, TCC_OP_ADD) < 0 ||
		   emit_op(ctx, TCC_OP_DUP) < 0 ||
		   emit_store_var(ctx, slot) < 0)
			return -1;
		return 0;
	}
	if(match_str(ps, "--")){
		if(!parse_identifier(ps, name, sizeof(name)))
			return -1;
		int slot = find_var(ctx, name);
		if(slot < 0 ||
		   emit_push_var(ctx, slot) < 0 ||
		   emit_push_const(ctx, 1) < 0 ||
		   emit_op(ctx, TCC_OP_SUB) < 0 ||
		   emit_op(ctx, TCC_OP_DUP) < 0 ||
		   emit_store_var(ctx, slot) < 0)
			return -1;
		return 0;
	}
	if(match_str(ps, "!")){
		if(parse_unary(ctx, ps, end) < 0 || emit_op(ctx, TCC_OP_NOT) < 0)
			return -1;
		return 0;
	}
	if(match_str(ps, "-")){
		if(parse_unary(ctx, ps, end) < 0 || emit_op(ctx, TCC_OP_NEG) < 0)
			return -1;
		return 0;
	}
	return parse_postfix(ctx, ps, end);
}

static int parse_multiplicative(tinycc_compile_t *ctx, const char **ps, const char *end){
	if(parse_unary(ctx, ps, end) < 0)
		return -1;
	while(1){
		unsigned op = 0;
		if(match_str(ps, "*")) op = TCC_OP_MUL;
		else if(match_str(ps, "/")) op = TCC_OP_DIV;
		else if(match_str(ps, "%")) op = TCC_OP_MOD;
		else break;
		if(parse_unary(ctx, ps, end) < 0 || emit_op(ctx, op) < 0)
			return -1;
	}
	return 0;
}

static int parse_additive(tinycc_compile_t *ctx, const char **ps, const char *end){
	if(parse_multiplicative(ctx, ps, end) < 0)
		return -1;
	while(1){
		if(match_str(ps, "+")){
			if(parse_multiplicative(ctx, ps, end) < 0 || emit_op(ctx, TCC_OP_ADD) < 0)
				return -1;
			continue;
		}
		if(match_str(ps, "-")){
			if(parse_multiplicative(ctx, ps, end) < 0 || emit_op(ctx, TCC_OP_SUB) < 0)
				return -1;
			continue;
		}
		break;
	}
	return 0;
}

static int parse_relational(tinycc_compile_t *ctx, const char **ps, const char *end){
	if(parse_additive(ctx, ps, end) < 0)
		return -1;
	while(1){
		unsigned op = 0;
		if(match_str(ps, "<=")) op = TCC_OP_LE;
		else if(match_str(ps, ">=")) op = TCC_OP_GE;
		else if(match_str(ps, "<")) op = TCC_OP_LT;
		else if(match_str(ps, ">")) op = TCC_OP_GT;
		else break;
		if(parse_additive(ctx, ps, end) < 0 || emit_op(ctx, op) < 0)
			return -1;
	}
	return 0;
}

static int parse_equality(tinycc_compile_t *ctx, const char **ps, const char *end){
	if(parse_relational(ctx, ps, end) < 0)
		return -1;
	while(1){
		unsigned op = 0;
		if(match_str(ps, "==")) op = TCC_OP_EQ;
		else if(match_str(ps, "!=")) op = TCC_OP_NE;
		else break;
		if(parse_relational(ctx, ps, end) < 0 || emit_op(ctx, op) < 0)
			return -1;
	}
	return 0;
}

static int parse_logical_and(tinycc_compile_t *ctx, const char **ps, const char *end){
	if(parse_equality(ctx, ps, end) < 0)
		return -1;
	while(match_str(ps, "&&")){
		int rhs_patch = -1;
		int false_patch = -1;
		int end_patch = -1;
		if(emit_jump(ctx, TCC_OP_JZ, &rhs_patch) < 0 ||
		   parse_equality(ctx, ps, end) < 0 ||
		   emit_jump(ctx, TCC_OP_JZ, &false_patch) < 0 ||
		   emit_push_const(ctx, 1) < 0 ||
		   emit_jump(ctx, TCC_OP_JMP, &end_patch) < 0)
			return -1;
		patch_u32(ctx, rhs_patch, (unsigned)code_pos(ctx));
		patch_u32(ctx, false_patch, (unsigned)code_pos(ctx));
		if(emit_push_const(ctx, 0) < 0)
			return -1;
		patch_u32(ctx, end_patch, (unsigned)code_pos(ctx));
	}
	return 0;
}

static int parse_logical_or(tinycc_compile_t *ctx, const char **ps, const char *end){
	if(parse_logical_and(ctx, ps, end) < 0)
		return -1;
	while(match_str(ps, "||")){
		int eval_rhs_patch = -1;
		int false_patch = -1;
		int lhs_true_patch = -1;
		int rhs_true_patch = -1;
		if(emit_jump(ctx, TCC_OP_JZ, &eval_rhs_patch) < 0 ||
		   emit_push_const(ctx, 1) < 0 ||
		   emit_jump(ctx, TCC_OP_JMP, &lhs_true_patch) < 0)
			return -1;
		patch_u32(ctx, eval_rhs_patch, (unsigned)code_pos(ctx));
		if(parse_logical_and(ctx, ps, end) < 0 ||
		   emit_jump(ctx, TCC_OP_JZ, &false_patch) < 0 ||
		   emit_push_const(ctx, 1) < 0 ||
		   emit_jump(ctx, TCC_OP_JMP, &rhs_true_patch) < 0)
			return -1;
		patch_u32(ctx, false_patch, (unsigned)code_pos(ctx));
		if(emit_push_const(ctx, 0) < 0)
			return -1;
		patch_u32(ctx, lhs_true_patch, (unsigned)code_pos(ctx));
		patch_u32(ctx, rhs_true_patch, (unsigned)code_pos(ctx));
	}
	return 0;
}

static int parse_assign_expr(tinycc_compile_t *ctx, const char **ps, const char *end){
	char name[TCC_VAR_NAME_MAX];
	const char *save = *ps;
	unsigned op = 0;

	if(parse_identifier(ps, name, sizeof(name))){
		int slot = find_var(ctx, name);
		if(slot < 0)
			return -1;
		if(match_str(ps, "+=")) op = TCC_OP_ADD;
		else if(match_str(ps, "-=")) op = TCC_OP_SUB;
		else if(match_str(ps, "*=")) op = TCC_OP_MUL;
		else if(match_str(ps, "/=")) op = TCC_OP_DIV;
		else if(match_str(ps, "%=")) op = TCC_OP_MOD;
		else if(match_str(ps, "=")) op = TCC_OP_STORE_VAR;
		else {
			*ps = save;
			return parse_logical_or(ctx, ps, end);
		}

		if(op != TCC_OP_STORE_VAR && emit_push_var(ctx, slot) < 0)
			return -1;
		if(parse_assign_expr(ctx, ps, end) < 0)
			return -1;
		if(op != TCC_OP_STORE_VAR && emit_op(ctx, op) < 0)
			return -1;
		if(emit_op(ctx, TCC_OP_DUP) < 0 || emit_store_var(ctx, slot) < 0)
			return -1;
		return 0;
	}

	*ps = save;
	return parse_logical_or(ctx, ps, end);
}

static int parse_expr(tinycc_compile_t *ctx, const char **ps, const char *end){
	return parse_assign_expr(ctx, ps, end);
}

static int parse_declaration(tinycc_compile_t *ctx, const char **ps, const char *end, char terminator){
	while(1){
		char name[TCC_VAR_NAME_MAX];
		if(!parse_identifier(ps, name, sizeof(name)))
			return -1;
		int slot = declare_var(ctx, name);
		if(slot < 0)
			return -1;
		if(match_str(ps, "=")){
			if(parse_expr(ctx, ps, end) < 0 || emit_store_var(ctx, slot) < 0)
				return -1;
		}
		if(match_str(ps, ",")) continue;
		break;
	}
	return expect_char(ps, terminator) ? 0 : -1;
}

static int parse_expr_until(tinycc_compile_t *ctx, const char **ps, const char *end, char terminator){
	if(parse_expr(ctx, ps, end) < 0 ||
	   !expect_char(ps, terminator) ||
	   emit_op(ctx, TCC_OP_POP) < 0)
		return -1;
	return 0;
}

static int parse_for_clause(tinycc_compile_t *ctx, const char **ps, const char *end, char terminator, int allow_decl){
	skip_ws_and_comments(ps);
	if(**ps == terminator){
		(*ps)++;
		return 0;
	}
	if(allow_decl && match_kw(ps, "int"))
		return parse_declaration(ctx, ps, end, terminator);
	return parse_expr_until(ctx, ps, end, terminator);
}

static int parse_block(tinycc_compile_t *ctx, const char **ps, const char *end, int *saw_return){
	if(!expect_char(ps, '{'))
		return -1;
	while(1){
		skip_ws_and_comments(ps);
		if(*ps >= end)
			return -1;
		if(**ps == '}'){
			(*ps)++;
			return 0;
		}
		if(parse_statement(ctx, ps, end, saw_return) < 0)
			return -1;
	}
}

static int parse_for(tinycc_compile_t *ctx, const char **ps, const char *end, int *saw_return){
	unsigned char step_buf[TCC_TEMP_MAX];
	tinycc_compile_t step = *ctx;
	tinycc_loop_t *loop;
	step.out = step_buf;
	step.cap = sizeof(step_buf);
	step.pos = 0;
	step.count = 0;
	step.loop_depth = 0;
	step.loop = 0;
	int jz_patch = -1;
	int loop_top;
	int step_target;
	int end_target;

	if(!expect_char(ps, '('))
		return -1;
	if(parse_for_clause(ctx, ps, end, ';', 1) < 0)
		return -1;

	loop_top = code_pos(ctx);
	skip_ws_and_comments(ps);
	if(**ps != ';'){
		if(parse_expr(ctx, ps, end) < 0 || emit_jump(ctx, TCC_OP_JZ, &jz_patch) < 0)
			return -1;
	}
	if(!expect_char(ps, ';'))
		return -1;

	skip_ws_and_comments(ps);
	if(**ps != ')'){
		if(parse_expr_until(&step, ps, end, ')') < 0)
			return -1;
	} else {
		(*ps)++;
	}

	loop = push_loop(ctx);
	if(!loop)
		return -1;
	if(parse_statement(ctx, ps, end, saw_return) < 0)
		return -1;
	step_target = code_pos(ctx);
	patch_loop_list(ctx, loop->continue_patch, loop->continue_count, (unsigned)step_target);
	if(emit_bytes(ctx, step.out, step.pos) < 0)
		return -1;
	ctx->count += step.count;
	if(emit_op(ctx, TCC_OP_JMP) < 0 || emit_u32(ctx, (unsigned)loop_top) < 0)
		return -1;
	end_target = code_pos(ctx);
	if(jz_patch >= 0)
		patch_u32(ctx, jz_patch, (unsigned)end_target);
	patch_loop_list(ctx, loop->break_patch, loop->break_count, (unsigned)end_target);
	pop_loop(ctx);
	return 0;
}

static int parse_while(tinycc_compile_t *ctx, const char **ps, const char *end, int *saw_return){
	tinycc_loop_t *loop;
	int loop_top;
	int jz_patch = -1;
	int end_target;

	if(!expect_char(ps, '('))
		return -1;
	loop_top = code_pos(ctx);
	loop = push_loop(ctx);
	if(!loop)
		return -1;
	if(parse_expr(ctx, ps, end) < 0 ||
	   !expect_char(ps, ')') ||
	   emit_jump(ctx, TCC_OP_JZ, &jz_patch) < 0 ||
	   parse_statement(ctx, ps, end, saw_return) < 0 ||
	   emit_op(ctx, TCC_OP_JMP) < 0 ||
	   emit_u32(ctx, (unsigned)loop_top) < 0)
		return -1;
	patch_loop_list(ctx, loop->continue_patch, loop->continue_count, (unsigned)loop_top);
	end_target = code_pos(ctx);
	patch_u32(ctx, jz_patch, (unsigned)end_target);
	patch_loop_list(ctx, loop->break_patch, loop->break_count, (unsigned)end_target);
	pop_loop(ctx);
	return 0;
}

static int parse_do_while(tinycc_compile_t *ctx, const char **ps, const char *end, int *saw_return){
	tinycc_loop_t *loop;
	int loop_top = code_pos(ctx);
	int cond_target;
	int jz_patch = -1;
	int end_target;

	loop = push_loop(ctx);
	if(!loop)
		return -1;
	if(parse_statement(ctx, ps, end, saw_return) < 0)
		return -1;
	cond_target = code_pos(ctx);
	patch_loop_list(ctx, loop->continue_patch, loop->continue_count, (unsigned)cond_target);
	if(!match_kw(ps, "while") ||
	   !expect_char(ps, '(') ||
	   parse_expr(ctx, ps, end) < 0 ||
	   !expect_char(ps, ')') ||
	   !expect_char(ps, ';') ||
	   emit_jump(ctx, TCC_OP_JZ, &jz_patch) < 0 ||
	   emit_op(ctx, TCC_OP_JMP) < 0 ||
	   emit_u32(ctx, (unsigned)loop_top) < 0)
		return -1;
	end_target = code_pos(ctx);
	patch_u32(ctx, jz_patch, (unsigned)end_target);
	patch_loop_list(ctx, loop->break_patch, loop->break_count, (unsigned)end_target);
	pop_loop(ctx);
	return 0;
}

static int parse_if(tinycc_compile_t *ctx, const char **ps, const char *end, int *saw_return){
	int else_patch = -1;
	int end_patch = -1;

	if(!expect_char(ps, '('))
		return -1;
	if(parse_expr(ctx, ps, end) < 0 ||
	   !expect_char(ps, ')') ||
	   emit_jump(ctx, TCC_OP_JZ, &else_patch) < 0 ||
	   parse_statement(ctx, ps, end, saw_return) < 0)
		return -1;

	skip_ws_and_comments(ps);
	if(match_kw(ps, "else")){
		if(emit_jump(ctx, TCC_OP_JMP, &end_patch) < 0)
			return -1;
		patch_u32(ctx, else_patch, (unsigned)code_pos(ctx));
		if(parse_statement(ctx, ps, end, saw_return) < 0)
			return -1;
		patch_u32(ctx, end_patch, (unsigned)code_pos(ctx));
		return 0;
	}

	patch_u32(ctx, else_patch, (unsigned)code_pos(ctx));
	return 0;
}

static int parse_statement(tinycc_compile_t *ctx, const char **ps, const char *end, int *saw_return){
	skip_ws_and_comments(ps);
	if(*ps >= end)
		return -1;
	if(**ps == ';'){
		(*ps)++;
		return 0;
	}
	if(**ps == '{')
		return parse_block(ctx, ps, end, saw_return);
	if(match_kw(ps, "int"))
		return parse_declaration(ctx, ps, end, ';');
	if(match_kw(ps, "if"))
		return parse_if(ctx, ps, end, saw_return);
	if(match_kw(ps, "do"))
		return parse_do_while(ctx, ps, end, saw_return);
	if(match_kw(ps, "while"))
		return parse_while(ctx, ps, end, saw_return);
	if(match_kw(ps, "for"))
		return parse_for(ctx, ps, end, saw_return);
	if(match_kw(ps, "u_puts")){
		char text[256];
		int len = 0;
		if(!expect_char(ps, '(') ||
		   !parse_c_string(ps, text, sizeof(text), &len) ||
		   !expect_char(ps, ')') ||
		   !expect_char(ps, ';'))
			return -1;
		return emit_puts(ctx, text, len);
	}
	if(match_kw(ps, "u_putc") || match_kw(ps, "uput_c")){
		if(!expect_char(ps, '(') ||
		   parse_expr(ctx, ps, end) < 0 ||
		   !expect_char(ps, ')') ||
		   !expect_char(ps, ';') ||
		   emit_op(ctx, TCC_OP_PUTC) < 0)
			return -1;
		return 0;
	}
	if(match_kw(ps, "u_sleep")){
		if(!expect_char(ps, '(') ||
		   parse_expr(ctx, ps, end) < 0 ||
		   !expect_char(ps, ')') ||
		   !expect_char(ps, ';') ||
		   emit_op(ctx, TCC_OP_SLEEP) < 0)
			return -1;
		return 0;
	}
	if(match_kw(ps, "break")){
		int patch_pos;
		tinycc_loop_t *loop = current_loop(ctx);
		if(!loop ||
		   !expect_char(ps, ';') ||
		   emit_jump(ctx, TCC_OP_JMP, &patch_pos) < 0 ||
		   add_loop_patch(loop->break_patch, &loop->break_count, patch_pos) < 0)
			return -1;
		return 0;
	}
	if(match_kw(ps, "continue")){
		int patch_pos;
		tinycc_loop_t *loop = current_loop(ctx);
		if(!loop ||
		   !expect_char(ps, ';') ||
		   emit_jump(ctx, TCC_OP_JMP, &patch_pos) < 0 ||
		   add_loop_patch(loop->continue_patch, &loop->continue_count, patch_pos) < 0)
			return -1;
		return 0;
	}
	if(match_kw(ps, "return")){
		if(parse_expr(ctx, ps, end) < 0 ||
		   !expect_char(ps, ';') ||
		   emit_op(ctx, TCC_OP_RET) < 0)
			return -1;
		*saw_return = 1;
		return 0;
	}
	return parse_expr_until(ctx, ps, end, ';');
}

static int compile_buffer(const char *src, int src_len, unsigned char *out, int out_cap, int *out_len){
	const char *p = src;
	const char *end = src + src_len;
	int saw_any = 0;
	int saw_return = 0;
	int in_body = 0;
	tinycc_compile_t *ctx;
	int result = -1;

	if(out_cap < 8)
		return -1;

	ctx = (tinycc_compile_t *)kmalloc(sizeof(*ctx));
	if(!ctx)
		return -1;
	memset(ctx, 0, sizeof(*ctx));
	ctx->loop = (tinycc_loop_t *)kmalloc(sizeof(tinycc_loop_t) * TCC_LOOP_MAX);
	if(!ctx->loop){
		kfree(ctx);
		return -1;
	}
	memset(ctx->loop, 0, sizeof(tinycc_loop_t) * TCC_LOOP_MAX);
	ctx->out = out;
	ctx->cap = out_cap;
	ctx->pos = 8;

	out[0] = TCC_MAGIC0;
	out[1] = TCC_MAGIC1;
	out[2] = TCC_MAGIC2;
	out[3] = TCC_MAGIC3;
	write_u32(out + 4, 0);

	while(p < end && *p){
		skip_ws_and_comments(&p);
		if(skip_preprocessor_line(&p, end))
			continue;
		if(p >= end || !*p) break;

		if(!in_body){
			int opens_block = 0;
			if(starts_decl_like(p) && skip_decl_or_header(&p, end, &opens_block)){
				if(opens_block)
					in_body = 1;
				continue;
			}
			if(*p == '{'){
				in_body = 1;
				p++;
				continue;
			}
			return -1;
		}

		if(*p == '}'){
			in_body = 0;
			p++;
			continue;
		}

		if(parse_statement(ctx, &p, end, &saw_return) < 0)
			goto out;
		saw_any = 1;
	}

	if(in_body || !saw_any)
		goto out;
	if(!saw_return){
		if(emit_push_const(ctx, 0) < 0 || emit_op(ctx, TCC_OP_RET) < 0)
			goto out;
	}

	write_u32(out + 4, ctx->count);
	*out_len = ctx->pos;
	result = 0;

out:
	kfree(ctx->loop);
	kfree(ctx);
	return result;
}

int tinycc_compile(const char *src_path, const char *out_path){
	int in_fd = vfs_open(src_path, O_RDONLY);
	if(in_fd < 0)
		return -1;

	char *src = (char *)kmalloc(VFS_MAX_FSIZE + 1);
	unsigned char *prog = (unsigned char *)kmalloc(VFS_MAX_FSIZE);
	int src_len;
	int prog_len = 0;
	int result = -1;
	if(!src || !prog){
		if(src) kfree(src);
		if(prog) kfree(prog);
		vfs_close(in_fd);
		return -1;
	}

	src_len = vfs_read(in_fd, src, VFS_MAX_FSIZE);
	vfs_close(in_fd);
	if(src_len <= 0){
		kfree(src);
		kfree(prog);
		return -1;
	}
	src[src_len] = '\0';

	if(compile_buffer(src, src_len, prog, VFS_MAX_FSIZE, &prog_len) < 0){
		kfree(src);
		kfree(prog);
		return -1;
	}
	kfree(src);

	int out_fd = vfs_open(out_path, O_WRONLY | O_CREAT | O_TRUNC);
	if(out_fd < 0){
		kfree(prog);
		return -1;
	}
	int wrote = vfs_write(out_fd, prog, prog_len);
	vfs_close(out_fd);
	if(wrote == prog_len)
		result = 0;
	kfree(prog);
	return result;
}

int tinycc_is_program(const char *path){
	int fd = vfs_open(path, O_RDONLY);
	if(fd < 0)
		return 0;
	unsigned char hdr[4];
	int n = vfs_read(fd, hdr, 4);
	vfs_close(fd);
	if(n != 4)
		return 0;
	return hdr[0] == TCC_MAGIC0 && hdr[1] == TCC_MAGIC1 &&
	       hdr[2] == TCC_MAGIC2 && hdr[3] == TCC_MAGIC3;
}

static void tinycc_task_entry(void){
	int id = task_current()->id;
	tinycc_task_ctx_t *ctx = &tinycc_tasks[id];
	unsigned char *p = ctx->prog;
	unsigned char *end = ctx->prog + ctx->size;
	unsigned char *code;
	int stack[TCC_STACK_MAX];
	int sp = 0;
	int vars[TCC_MAX_VARS];

#define TCC_PUSH(v) do { if(sp >= TCC_STACK_MAX) goto done; stack[sp++] = (v); } while(0)
#define TCC_POP() ((sp > 0) ? stack[--sp] : 0)

	if(!p || ctx->size < 8)
		task_exit();

	if(p[0] != TCC_MAGIC0 || p[1] != TCC_MAGIC1 || p[2] != TCC_MAGIC2 || p[3] != TCC_MAGIC3)
		task_exit();

	memset(vars, 0, sizeof(vars));
	code = p + 8;
	p = code;

	while(p < end){
		unsigned char op = *p++;
		if(op == TCC_OP_PUTS){
			unsigned len;
			if(p + 2 > end) break;
			len = (unsigned)p[0] | ((unsigned)p[1] << 8);
			p += 2;
			if(p + len + 1 > end) break;
			uart_puts((const char *)p);
			p += len + 1;
		} else if(op == TCC_OP_SLEEP){
			unsigned long ms = (unsigned long)TCC_POP();
			if(PLATFORM_INIT_IRQS)
				task_sleep_ms(ms);
			else
				delay_ms(ms);
		} else if(op == TCC_OP_PUTC){
			uart_putc((char)TCC_POP());
		} else if(op == TCC_OP_PUSH_CONST){
			if(p + 4 > end) break;
			TCC_PUSH((int)read_u32(p));
			p += 4;
		} else if(op == TCC_OP_PUSH_VAR){
			if(p >= end) break;
			TCC_PUSH(vars[*p++]);
		} else if(op == TCC_OP_STORE_VAR){
			if(p >= end) break;
			vars[*p++] = TCC_POP();
		} else if(op == TCC_OP_ADD){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs + rhs);
		} else if(op == TCC_OP_SUB){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs - rhs);
		} else if(op == TCC_OP_MUL){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs * rhs);
		} else if(op == TCC_OP_DIV){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(rhs ? (lhs / rhs) : 0);
		} else if(op == TCC_OP_MOD){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(rhs ? (lhs % rhs) : 0);
		} else if(op == TCC_OP_NOT){
			int value = TCC_POP();
			TCC_PUSH(value == 0);
		} else if(op == TCC_OP_DUP){
			int value = TCC_POP();
			TCC_PUSH(value);
			TCC_PUSH(value);
		} else if(op == TCC_OP_POP){
			(void)TCC_POP();
		} else if(op == TCC_OP_LAND){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH((lhs != 0) && (rhs != 0));
		} else if(op == TCC_OP_LOR){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH((lhs != 0) || (rhs != 0));
		} else if(op == TCC_OP_LT){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs < rhs);
		} else if(op == TCC_OP_LE){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs <= rhs);
		} else if(op == TCC_OP_GT){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs > rhs);
		} else if(op == TCC_OP_GE){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs >= rhs);
		} else if(op == TCC_OP_EQ){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs == rhs);
		} else if(op == TCC_OP_NE){
			int rhs = TCC_POP();
			int lhs = TCC_POP();
			TCC_PUSH(lhs != rhs);
		} else if(op == TCC_OP_NEG){
			int value = TCC_POP();
			TCC_PUSH(-value);
		} else if(op == TCC_OP_JMP){
			if(p + 4 > end) break;
			{
				unsigned target = read_u32(p);
				if(target >= (unsigned)(end - code)) break;
				p = code + target;
			}
		} else if(op == TCC_OP_JZ){
			unsigned target;
			if(p + 4 > end) break;
			target = read_u32(p);
			p += 4;
			if(target >= (unsigned)(end - code)) break;
			if(TCC_POP() == 0)
				p = code + target;
		} else if(op == TCC_OP_RET){
			(void)TCC_POP();
			break;
		} else if(op == TCC_OP_EXIT){
			if(p + 4 > end) break;
			p += 4;
			break;
		} else {
			break;
		}
	}

done:
#undef TCC_PUSH
#undef TCC_POP
	tinycc_task_cleanup(id);
	task_exit();
}

int tinycc_exec(const char *path, const char *task_name){
	int fd = vfs_open(path, O_RDONLY);
	if(fd < 0)
		return -1;

	int size = vfs_seek(fd, 0, SEEK_END);
	vfs_seek(fd, 0, SEEK_SET);
	if(size < 8 || size > VFS_MAX_FSIZE){
		vfs_close(fd);
		return -1;
	}

	unsigned char *buf = (unsigned char *)kmalloc((unsigned long)size);
	if(!buf){
		vfs_close(fd);
		return -1;
	}
	if(vfs_read(fd, buf, size) != size){
		vfs_close(fd);
		kfree(buf);
		return -1;
	}
	vfs_close(fd);

	if(buf[0] != TCC_MAGIC0 || buf[1] != TCC_MAGIC1 || buf[2] != TCC_MAGIC2 || buf[3] != TCC_MAGIC3){
		kfree(buf);
		return -1;
	}

	int tid = task_create_named(tinycc_task_entry, task_name);
	if(tid < 0){
		kfree(buf);
		return -1;
	}

	tinycc_tasks[tid].prog = buf;
	tinycc_tasks[tid].size = size;
	return tid;
}

void tinycc_task_cleanup(int task_id){
	if(task_id < 0 || task_id >= MAX_TASKS)
		return;
	if(tinycc_tasks[task_id].prog){
		kfree(tinycc_tasks[task_id].prog);
		tinycc_tasks[task_id].prog = 0;
	}
	tinycc_tasks[task_id].size = 0;
}
