#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCEINFO 0

#if defined(SOURCEINFO) && SOURCEINFO == 1
#define print_source_line(void)                                                \
  { printf("[SOURCELINE] %d\n[FUNC]%s\n", __LINE__, __func__); }
#else
void print_source_line(void) {}
#endif

#define STACK_SIZE 1024
#define MAX_WORDS 5000
#define MAX_HERE_SPACE 1024 * 64
#define CF_STACK 256
#define UNUSED(x) (void)(x)
typedef unsigned long long u64;

typedef long long i64;

typedef enum mode { INTERPRET = 1, COMPILE = 0 } MODE;

#define IMMEDIATE 0x01

typedef struct word WORD;

typedef struct word {
  const char *name;

  void (*code)(WORD *);

  u64 *continuation;

  // list of words
  u64 flags;
  u64 *data;
} WORD;

// main stack
u64 stack[STACK_SIZE];
u64 sp = 0;

// dictionary stack
WORD dictionary[MAX_WORDS];

u64 here = 0;

// memory for word definitions
WORD *current_def = NULL;
WORD *last_created = NULL;
u64 code_space[MAX_HERE_SPACE];
u64 *here_code = code_space;

// return stack
u64 rstack[STACK_SIZE];
u64 rsp = 0;

// control flow stack (IF/ELSE/THEN BEGIN/WHILE/REPEAT etc..)
u64 *cfstack[CF_STACK];
u64 cfsp = 0;
// dynamic heap allocated memory
u64 *data_space = NULL;
u64 dp = 0;
u64 data_cap = 0;

#define CELLSIZE sizeof(u64)

void execute(WORD *w);

void allstats(WORD *w) {
  UNUSED(w);
  printf("skforth stats: \n NAME | MAX_BYTES | CURRENT_BYTES \n[STACK] "
         "%llu %llu\n[DATA] %llu %llu\n[CODE] %llu "
         " %s\n[RSTACK] %llu %llu\n[CFSTACK] %llu %llu\n",
         ((u64)STACK_SIZE * 64), sp * 64, data_cap, dp,
         ((u64)MAX_HERE_SPACE * 64), "<no info>", ((u64)STACK_SIZE * 64),
         rsp * 64, ((u64)CF_STACK * 64), cfsp * 64);
}

#define CFPUSH(x)                                                              \
  cfsp >= (u64)CF_STACK ? (printf("[ERROR] Control Flow overflow"), NULL)      \
                        : (cfstack[cfsp++] = (x))
#define CFPOP()                                                                \
  (cfsp == (u64)0 ? (printf("[ERROR] Control Flow underflow\n"), NULL)         \
                  : cfstack[--cfsp])
// instruction pointer
u64 *ip = NULL;
MODE f_mode = INTERPRET;

int spush(u64 v) {
  if (sp == STACK_SIZE) {
    printf("[ERROR] Stack is full\n");
    print_source_line();
    return 0;
  }
  stack[sp++] = v;
  return -1;
}
u64 spop(void) {
  if (sp == 0) {
    printf("[ERROR] Stack is empty\n");
    print_source_line();
    return 0xDEADBEEF;
  }
  return stack[--sp];
}

void lit(WORD *w) {
  UNUSED(w);
  u64 value = *ip++;
  spush(value);
}

char *current_line_buffer = NULL;
u64 current_line_length = 256;
u64 input_index = 0;

void source_word(WORD *w) {
  UNUSED(w);
  spush((u64)current_line_buffer);
  spush(current_line_length);
}

void in_word(WORD *w) {
  UNUSED(w);
  spush((u64)&input_index);
}

void add_word(const char *name, void (*code)(WORD *),
              u64 *continuation_wordlist, u64 flags) {
  if (here == (u64)MAX_WORDS) {
    printf("[ERROR] Max number of WORDS reached");
    print_source_line();
    return;
  }
  WORD *w = &dictionary[here++];
  w->name = name;
  w->code = code;
  w->continuation = (u64 *)continuation_wordlist;
  w->flags = flags;
}
int streq(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b)
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

WORD *find_word(const char *name) {
  for (i64 x = here - 1; x >= 0; x -= 1) {
    if (streq(dictionary[x].name, name))
      return &dictionary[x];
  }
  return NULL;
}

// primitive: . (print top of stack)
void dot(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty.\n");
    return;
  }
  u64 val = spop();
  printf("%llu ", val);
}
void words(WORD *w) {
  UNUSED(w);
  for (i64 x = here - 1; x >= 0; x -= 1) {
    printf("%s ", dictionary[x].name);
  }
  printf("\n");
}
void cr(WORD *w) {
  UNUSED(w);
  printf("\n");
}
// primitive: .s (print stack size and it's elements)
void dot_stack(WORD *w) {
  UNUSED(w);
  printf("[STACK] <%llu> ", sp);
  for (u64 x = 0; x < sp; x += 1) {
    printf("%llu ", stack[x]);
  }
  printf("\n");
}
// primitive: +
void add(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small\n");
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a + b);
}
void substract(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small\n");
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a - b);
}
void multiply(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small\n");
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a * b);
}
void dup(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty \n");
    print_source_line();
    return;
  }
  spush(stack[sp - 1]);
}
void double_dup(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  spush(stack[sp - 2]);
  spush(stack[sp - 2]);
}
void swap(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  u64 last = spop();
  u64 over = spop();
  spush(last);
  spush(over);
}
void double_swap(WORD *w) {
  UNUSED(w);
  if (sp < 4) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  u64 d = spop();
  u64 c = spop();
  u64 b = spop();
  u64 a = spop();
  spush(d);
  spush(c);
  spush(b);
  spush(a);
}
void over(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  spush(stack[sp - 2]);
}
void double_over(WORD *w) {
  UNUSED(w);
  if (sp < 3) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  spush(stack[sp - 3]);
}
void slash_mod(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is to small \n");
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a % b);
  spush(a / b);
}
void rot(WORD *w) {
  UNUSED(w);
  if (sp < 3) {
    printf("[ERROR] Stack is to small \n");
    print_source_line();
    return;
  }
  u64 c = spop();
  u64 b = spop();
  u64 a = spop();
  spush(c);
  spush(a);
  spush(b);
}
void reverse_rot(WORD *w) {
  UNUSED(w);
  if (sp < 3) {
    printf("[ERROR] Stack is to small \n");
    print_source_line();
    return;
  }
  u64 c = spop();
  u64 b = spop();
  u64 a = spop();
  spush(b);
  spush(c);
  spush(a);
}
void depth(WORD *w) {
  UNUSED(w);
  spush(sp);
}
void ddepth(WORD *w) {
  UNUSED(w);
  spush(dp);
}
void drop(WORD *w) {
  UNUSED(w);
  spop();
}
void double_drop(WORD *w) {
  UNUSED(w);
  spop();
  spop();
}
void equals_zero(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty \n");
    print_source_line();
    return;
  }
  u64 a = spop();
  spush(a == 0);
}
void equals(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a == b);
}
void lessthan(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a < b);
}
void morethan(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a > b);
}
void morethanequal(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a >= b);
}
void morethanzero(WORD *w) {
  UNUSED(w);
  if (sp < 1) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  u64 val = spop();
  spush(val > 0);
}
void notzero(WORD *w) {
  UNUSED(w);
  if (sp < 1) {
    printf("[ERROR] Stack is too small \n");
    print_source_line();
    return;
  }
  u64 val = spop();
  spush(val > 0);
}
void minusone(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty \n");
    print_source_line();
    return;
  }
  spush(spop() - 1);
}
void c_at(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty \n");
    print_source_line();
    return;
  }
  u64 addr = spop();
  spush((u64)(*(unsigned char *)addr));
}
void c_store(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small\n");
    print_source_line();
    return;
  }
  u64 val = spop();
  u64 addr = spop();
  *(unsigned char *)addr = (unsigned char)val;
}
void add_bl(WORD *w) {
  UNUSED(w);
  spush(32);
}
void clear_stack_word(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty \n");
    print_source_line();
    return;
  }
  sp = 0;
}
void clear_data_word(WORD *w) {
  UNUSED(w);
  if (!data_space) {
    printf("[ERROR] There is no memory allocated in Data space to clear\n");
    print_source_line();
    return;
  }
  free(data_space);
  data_space = NULL;
  dp = 0;
  data_cap = 0;
}
void grow_data(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty\n");
    print_source_line();
    return;
  }
  u64 n = spop();
  void *new = realloc(data_space, (data_cap + n) * CELLSIZE);
  if (!new) {
    printf("[ERROR] Could not regrow Data memory\n");
    print_source_line();
    return;
  }
  data_cap += n;
  data_space = (u64 *)new;
}
// int c_next_token(char **out_addr, u64 *out_len);
int c_next_token(char **addr, u64 *len);

void here_data(WORD *w) {
  UNUSED(w);
  spush((u64)(data_space + dp));
}

void or_word(WORD *w) {
  UNUSED(w);
  u64 b = spop();
  u64 a = spop();
  spush(a | b);
}

void and_word(WORD *w) {
  UNUSED(w);
  u64 b = spop();
  u64 a = spop();
  spush(a && b);
}

void alloc_data(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty\n");
    print_source_line();
    return;
  }
  u64 num_cells = spop();
  if (num_cells + dp > data_cap) {
    printf("[ERROR] Number of cells to alloc exceed Data Area capacity");
    print_source_line();
    return;
  }
  dp += num_cells;
}

void at_ptr(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty\n");
    print_source_line();
    return;
  }
  u64 ptr = spop();
  spush(*(u64 *)ptr);
}
void write_ptr(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("[ERROR] Stack is too small\n");
    print_source_line();
    return;
  }
  u64 *addr = (u64 *)spop();
  if (!addr) {
    printf("[ERROR] Not a valid pointer given\n");
    print_source_line();
    return;
  }
  u64 val = spop();
  *addr = val;
}
void to_r(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty\n");
    print_source_line();
    return;
  }
  if (rsp >= STACK_SIZE) {
    printf("[ERROR] Return stack overflow\n");
    print_source_line();
    return;
  }
  rstack[rsp++] = spop();
}
void from_r(WORD *w) {
  UNUSED(w);
  if (rsp == 0) {
    printf("[ERROR] Return stack is empty\n");
    print_source_line();
    return;
  }
  if (sp >= STACK_SIZE) {
    printf("[ERROR] Stack is full and cant return value from Return stack to "
           "main stack\n");
    print_source_line();
    return;
  }
  spush(rstack[--rsp]);
}
void ensure_data(u64 cells);

void push_ptr_code(WORD *w) { spush((u64)w->data); }
void push_val_code(WORD *w) { spush(*(w->data)); }

void immediate(WORD *w) {
  UNUSED(w);
  dictionary[here - 1].flags |= IMMEDIATE;
}

void create_struct(WORD *ww) {
  UNUSED(ww);

  if (sp < 2) {
    printf("[ERROR] CREATE expects a name\n");
    print_source_line();
    return;
  }
  u64 len = spop();
  char *addr = (char *)spop();

  if (len == 0) {
    printf("[ERROR] CREATE expects a name\n");
    print_source_line();
    return;
  }

  char *name = strndup(addr, len);

  if (!name) {
    printf("[ERROR] CREATE: strdup failed \n");
    print_source_line();
    return;
  }

  if (here + 1 == (u64)MAX_WORDS) {
    printf("[ERROR] Max number of WORDS reached in dictionary area\n");
    print_source_line();
    return;
  }
  if (!data_space) {
    ensure_data(1);
  }

  WORD *w = &dictionary[here++];
  w->name = name;
  w->flags = 0;
  w->data = data_space + dp;
  // dp (data pointer) is not incremented. we simply are storing the pointer to
  // the start of the structure
  w->continuation = NULL;
  w->code = push_ptr_code;
  last_created = w;
}

void comma(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("[ERROR] Stack is empty\n");
    print_source_line();
    return;
  }
  u64 val = spop();
  ensure_data(1);
  data_space[dp++] = val;
}

void postpone(WORD *w) {
  UNUSED(w);
  execute(find_word("PARSE-NAME"));
  u64 len = spop();
  char *addr = (char *)spop();
  if (len == 0) {
    printf("[ERROR] POSTPONE expects a name\n");
    print_source_line();
    return;
  }

  char *name = strndup(addr, len);

  if (!name) {
    printf("[ERROR] POSTPONE expects a word name\n");
    print_source_line();
    return;
  }

  WORD *t = find_word(name);
  if (!t) {
    printf("[ERROR] Unknown word: %s\n", name);
    print_source_line();
    return;
  }

  *here_code++ = (u64)t;
}

void constant_var_word(WORD *w) {
  UNUSED(w);
  execute(find_word("PARSE-NAME"));
  u64 len = spop();
  char *addr = (char *)spop();
  if (len == 0) {
    printf("[ERROR] CREATE expects a name\n");
    print_source_line();
    return;
  }

  char *name = strndup(addr, len);

  if (!name) {
    printf("[ERROR] constant expects a name\n");
    print_source_line();
    return;
  }
  if (sp == 0) {
    printf("[ERROR] constant expects a value on stack\n");
    print_source_line();
    return;
  }

  if (sp == 0)
    return;

  u64 val = spop();
  ensure_data(1);

  WORD *nw = &dictionary[here++];
  nw->name = name;
  nw->flags = 0;
  nw->data = data_space + dp;
  data_space[dp++] = val;
  nw->code = push_val_code;
  nw->continuation = NULL;
}

void ensure_data(u64 cells) {
  if (dp + cells <= data_cap)
    return;
  u64 new_cap = data_cap ? data_cap : 16;
  while (new_cap < dp + cells)
    new_cap *= 2;
  void *new = realloc(data_space, new_cap * CELLSIZE);
  if (!new) {
    printf("[ERROR] Out of memory in data space\n");
    print_source_line();
    return;
  }
  data_space = (u64 *)new;
  data_cap = new_cap;
}

void bye(WORD *w) {
  UNUSED(w);
  exit(EXIT_SUCCESS);
}

// : (compile mode)
void colon(WORD *ww) {
  UNUSED(ww);
  if (cfsp != 0) {
    printf("[ERROR] Unresolved control structure\n");
    print_source_line();
    exit(EXIT_FAILURE);
  }

  execute(find_word("PARSE-NAME"));
  u64 len = spop();
  char *addr = (char *)spop();
  if (len == 0) {
    printf("[ERROR] Expected word name after ':'\n");
    print_source_line();
    return;
  }

  char *name = strndup(addr, len);

  if (!name) {
    printf("[ERROR] Expected word name after ':'\n");
    print_source_line();
    return;
  }
  WORD *nw = &dictionary[here++];
  // TODO: change this
  nw->name = name;
  nw->code = NULL;
  nw->continuation = here_code;
  current_def = nw;
  f_mode = COMPILE;
}
// ;(end compile mode)
void semicolon(WORD *w) {
  UNUSED(w);
  *here_code++ = (u64)NULL;
  f_mode = INTERPRET;
  current_def = NULL;
}
// if\else\then branching
void zero_branch(WORD *w) {
  UNUSED(w);
  u64 flag = spop();
  // conditional argument
  u64 target = *ip++;
  if (flag == 0)
    ip = (u64 *)target;
}
void branch(WORD *w) {
  UNUSED(w);
  u64 target = *ip++;
  ip = (u64 *)target;
}
void if_word(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("[ERROR] IF only valid in compile mode\n");
    print_source_line();
    return;
  }
  WORD *zb = find_word("0BRANCH");
  *here_code++ = (u64)zb;
  *here_code++ = 0;
  CFPUSH(here_code - 1);
}
void else_word(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("[ERROR] ELSE only valid in compile mode\n");
    print_source_line();
    return;
  }
  WORD *br = find_word("BRANCH");
  *here_code++ = (u64)br;
  *here_code++ = 0;
  u64 *if_placeholder = CFPOP();
  *(u64 *)if_placeholder = (u64)here_code;
  CFPUSH(here_code - 1);
}
void then_word(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("[ERROR] THEN only valid in compile mode\n");
    print_source_line();
    return;
  }
  u64 *placeholder = CFPOP();
  *(u64 *)placeholder = (u64)here_code;
}
// begin\while\repeat branching
void begin(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("[ERROR] BEGIN is only valid in compile mode\n");
    print_source_line();
    return;
  }
  CFPUSH(here_code);
}
void while_word(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("[ERROR] WHILE is only valid in compile mode\n");
    print_source_line();
    return;
  }
  WORD *zb = find_word("0BRANCH");
  *here_code++ = (u64)zb;
  *here_code++ = 0;
  CFPUSH(here_code - 1);
}

void repeat(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("[ERROR] REPEAT is only valid in compile mode\n");
    print_source_line();
    return;
  }
  u64 *while_placeholder = CFPOP();
  u64 *begin_addr = CFPOP();
  WORD *br = find_word("BRANCH");
  *here_code++ = (u64)br;
  *here_code++ = (u64)begin_addr;
  *(u64 *)while_placeholder = (u64)here_code;
}

void exit_word(WORD *w) {
  UNUSED(w);
  if (rsp == 0) {
    ip = NULL;
    return;
  }
  ip = (u64 *)rstack[--rsp];
}

void main_interpret_line(char *line);

void include_forth_file(WORD *w) {
  UNUSED(w);

  execute(find_word("PARSE-NAME"));
  u64 len = spop();
  char *addr = (char *)spop();
  if (len == 0) {
    printf("[ERROR] INCLUDE expects a name\n");
    print_source_line();
    return;
  }
  char *fname = strndup(addr, len);

  if (!fname) {
    printf("[ERROR] INCLUDE expects filename\n");
    print_source_line();
    return;
  }
  FILE *f = fopen(fname, "r");
  if (!f) {
    printf("[ERROR] Could not open %s\n", fname);
    print_source_line();
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    main_interpret_line(line);
  }
  fclose(f);
}

void interpret(WORD *ww) {
  UNUSED(ww);
  f_mode = INTERPRET;
}

void comptime(WORD *ww) {
  UNUSED(ww);
  f_mode = COMPILE;
}

void mode_show(WORD *ww) {
  UNUSED(ww);
  if (f_mode == INTERPRET)
    printf("INTERPRET\n");
  else
    printf("COMPILE\n");
}

void mode_get(WORD *ww) {
  UNUSED(ww);
  if (sp >= (u64)STACK_SIZE) {
    printf("[ERROR] Stack is full");
    print_source_line();
    return;
  }
  spush((u64)f_mode);
}

char *input_cursor_global = NULL;
char *next_token(char **cursor);

void interpret_token_word(WORD *ww) {
  UNUSED(ww);

  if (sp < 2) {
    printf("[ERROR] INTERPRET-TOKEN expects addr len\n");
    print_source_line();
    return;
  }

  u64 len = spop();
  char *addr = (char *)spop();

  char *token = strndup(addr, len);

  if (!token) {
    printf("[ERROR] Out of memory\n");
    print_source_line();
    return;
  }

  WORD *w = find_word(token);

  if (w) {
    if (f_mode == INTERPRET) {
      execute(w);
    } else {
      if (w->flags & IMMEDIATE)
        execute(w);
      else
        *here_code++ = (u64)w;
    }
  } else {
    char *end;
    char *tmp = strndup(addr, len);
    u64 n = strtoull(tmp, &end, 10);
    if (*end == '\0') {
      if (f_mode == INTERPRET)
        spush(n);
      else {
        WORD *litw = find_word("LIT");
        *here_code++ = (u64)litw;
        *here_code++ = n;
      }

    } else {
      printf("Unknown word: %s\n", token);
      print_source_line();
      free(tmp);
      free(token);
      return;
    }
    free(tmp);
  }

  free(token);
}

int c_next_token(char **addr, u64 *len) {
retrypoint:
  while (input_index < current_line_length &&
         isspace((unsigned char)current_line_buffer[input_index]))
    input_index++;

  if (input_index >= current_line_length)
    return 0;

  if (current_line_buffer[input_index] == '\\') {
    input_index = current_line_length;
    return 0;
  }

  if (current_line_buffer[input_index] == '(') {
    input_index++;
    while (input_index < current_line_length &&
           current_line_buffer[input_index] != ')')
      input_index++;

    if (input_index < current_line_length &&
        current_line_buffer[input_index] == ')')
      input_index++;

    // return c_next_token(addr, len);
    goto retrypoint;
  }

  char *start = current_line_buffer + input_index;

  while (input_index < current_line_length &&
         !isspace((unsigned char)current_line_buffer[input_index]))
    input_index++;

  *addr = start;
  *len = (current_line_buffer + input_index) - start;
  return 1;
}

void parse_name_word(WORD *w) {
  UNUSED(w);

  char *src = current_line_buffer;
  u64 len_src = current_line_length;
  u64 i = input_index;

  while (i < len_src && isspace((unsigned char)src[i]))
    i++;

  if (i >= len_src) {
    spush(0);
    spush(0);
    input_index = i;
    return;
  }

  char *start = src + i;

  while (i < len_src && !isspace((unsigned char)src[i]))
    i++;

  input_index = i;
  spush((u64)start);
  spush((u64)(src + i - start));
  // char *addr;
  // u64 len;
  //
  // // jump white space wholy hacky way
  //
  // int ok = c_next_token(&addr, &len);
  // if (!ok) {
  //   spush(0);
  //   spush(0);
  //   return;
  // }
  //
  // spush((u64)addr);
  // spush(len);
}

void interpret_line_c_word(WORD *w) {
  UNUSED(w);

  u64 len;
  char *addr;

  while (c_next_token(&addr, &len)) {

    if (len == 0)
      break;

    spush((u64)addr);
    spush(len);
    interpret_token_word(NULL);
  }
}

void init(void) {
  add_word("LIT", lit, NULL, 0);
  add_word("0BRANCH", zero_branch, NULL, 0);
  add_word("BRANCH", branch, NULL, 0);
  add_word("COMPTIME", comptime, NULL, 0);
  add_word("INTERPRET", interpret, NULL, 0);
  add_word("SOURCE", source_word, NULL, 0);
  add_word(">IN", in_word, NULL, 0);
  add_word("POSTPONE", postpone, NULL, IMMEDIATE);

  add_word(":", colon, NULL, IMMEDIATE);
  add_word(";", semicolon, NULL, IMMEDIATE);
  add_word(".", dot, NULL, 0);
  add_word(".mode", mode_show, NULL, 0);
  add_word("mode", mode_get, NULL, 0);
  add_word(".memstats", allstats, NULL, 0);
  add_word(".s", dot_stack, NULL, 0);
  add_word("cr", cr, NULL, 0);
  add_word("+", add, NULL, 0);
  add_word("-", substract, NULL, 0);
  add_word("or", or_word, NULL, 0);
  add_word("|", or_word, NULL, 0);
  add_word("and", and_word, NULL, 0);
  add_word("&&", and_word, NULL, 0);
  add_word("*", multiply, NULL, 0);
  add_word("/mod", slash_mod, NULL, 0);
  add_word("dup", dup, NULL, 0);
  add_word("2dup", double_dup, NULL, 0);
  add_word("drop", drop, NULL, 0);
  add_word("2drop", double_drop, NULL, 0);
  add_word("swap", swap, NULL, 0);
  add_word("2swap", double_swap, NULL, 0);
  add_word("over", over, NULL, 0);
  add_word("2over", double_over, NULL, 0);
  add_word("rot", rot, NULL, 0);
  add_word("-rot", reverse_rot, NULL, 0);
  add_word("depth", depth, NULL, 0);
  add_word("ddepth", ddepth, NULL, 0);
  add_word("clear.s", clear_stack_word, NULL, 0);
  add_word("clear.d", clear_data_word, NULL, IMMEDIATE);
  add_word("words", words, NULL, 0);
  add_word("1-", minusone, NULL, 0);
  add_word("0=", equals_zero, NULL, 0);
  add_word("=", equals, NULL, 0);
  add_word("<", lessthan, NULL, 0);
  add_word(">", morethan, NULL, 0);
  add_word(">=", morethanequal, NULL, 0);

  add_word("0>", morethanzero, NULL, 0);
  add_word("0<>", notzero, NULL, 0);
  add_word("IF", if_word, NULL, IMMEDIATE);
  add_word("ELSE", else_word, NULL, IMMEDIATE);
  add_word("THEN", then_word, NULL, IMMEDIATE);
  add_word("BEGIN", begin, NULL, IMMEDIATE);
  add_word("WHILE", while_word, NULL, IMMEDIATE);
  add_word("REPEAT", repeat, NULL, IMMEDIATE);
  add_word("EXIT", exit_word, NULL, 0);
  add_word("INCLUDE", include_forth_file, NULL, IMMEDIATE);
  add_word("@", at_ptr, NULL, 0);
  add_word("!", write_ptr, NULL, 0);
  add_word(">R", to_r, NULL, 0);
  add_word("R>", from_r, NULL, 0);
  add_word("c@", c_at, NULL, 0);
  add_word("c!", c_store, NULL, 0);
  add_word("bl", add_bl, NULL, 0);
  add_word("GROW", grow_data, NULL, IMMEDIATE);
  add_word("HERE", here_data, NULL, 0);
  add_word("ALLOC", alloc_data, NULL, 0);
  add_word("constvar:", constant_var_word, NULL, 0);
  add_word("CREATE", create_struct, NULL, 0);
  add_word("PARSE-NAME", parse_name_word, NULL, 0);
  add_word(",", comma, NULL, 0);
  add_word("INTERPRET-TOKEN", interpret_token_word, NULL, 0);
  add_word("IMMEDIATE", immediate, NULL, IMMEDIATE);

  add_word("bye", bye, NULL, 0);

  add_word("INTERPRET-LINE", interpret_line_c_word, NULL, 0);
  data_cap = 1024;
  data_space = malloc(data_cap * CELLSIZE);
  if (!data_space)
    printf("[ERROR] Could not dynamically allocate %llu bytes for the Data "
           "space",
           data_cap);
}

void execute(WORD *w) {
  u64 *saved_ip = ip;

  if (w->code)
    w->code(w);

  // primitive
  if (w->continuation == NULL) {
    ip = saved_ip;
    return;
  }

  // colon word
  if (ip == saved_ip) {
    if (ip)
      rstack[rsp++] = (u64)ip;
    ip = w->continuation;
  }

  while (ip) {
    WORD *cw = (WORD *)(*ip++);

    if (!cw) {
      if (rsp == 0) {
        ip = saved_ip;
        return;
      }
      ip = (u64 *)rstack[--rsp];
      continue;
    }

    u64 *saved_ip2 = ip;

    if (cw->code)
      cw->code(cw);

    if (cw->continuation && ip == saved_ip2) {
      if (ip)
        rstack[rsp++] = (u64)ip;
      ip = cw->continuation;
    }
  }
}

void main_interpret_line(char *line) {
  current_line_buffer = line;
  current_line_length = strlen(line);
  input_index = 0;

  // WORD *interp = find_word("INTERPRET-LINE");
  // if (!interp) {
  //   printf("[FATAL] INTERPRET-LINE not found\n");
  //   return;
  // }
  // execute(interp);
  interpret_line_c_word(NULL);
}

int main(void) {
  init();
  FILE *f = fopen("bootstrap.fs", "r");
  if (!f) {
    printf("bootstrap.fs not found\n");
    exit(EXIT_FAILURE);
  }
  char line[256];
  printf("Loading bootstrap.fs...\n\n");
  while (fgets(line, sizeof(line), f)) {
    main_interpret_line(line);
  }
  fclose(f);

  fflush(stdout);
  printf("Welcome to skforth :D \n");
  printf("skforth> ");
  while (fgets(line, sizeof(line), stdin)) {
    main_interpret_line(line);
    printf("skforth> ");
  }

  if (data_space)
    free(data_space);
  return 0;
}
