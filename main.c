#define _GNU_SOURCE
#include <asm-generic/errno-base.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// blocks sintax
// BLOCK    ( u -- addr )
// BUFFER   ( u -- addr )
// UPDATE   ( -- )
// FLUSH    ( -- )
// #BLOCKS  ( -- u)

#define SOURCEINFO 0

#if defined(SOURCEINFO) && SOURCEINFO == 1
#define print_source_line(void)                                                \
  { printf("[SOURCELINE] %d\n[FUNC]%s\n", __LINE__, __func__); }
#else
void print_source_line(void) {}
#endif

typedef unsigned long long u64;

typedef long long i64;

u64 BLOCK_SIZE;
u64 NUM_BLOCKS;
u64 STACK_SIZE;
u64 MAX_WORDS;
u64 MAX_CODE_SPACE;
u64 CF_STACK;
u64 DATA_SIZE;
u64 MAX_BYTES_SPACE;

#define CONFIG_STACK_SIZE 200
#define CONFIG_DIC_SIZE 2

#define UNUSED(x) (void)(x)

#define SETREDCOLOR "\033[31m"
#define SETGREENCOLOR "\033[32m"
#define SETYELLOWCOLOR "\033[33m"
#define RESETALLSTYLES "\033[0m"

typedef enum mode { INTERPRET = 1, COMPILE = 0 } MODE;

#define IMMEDIATE 0x01

typedef struct word WORD;

typedef struct word {
  const char *name;

  void (*code)(WORD *);

  u64 *continuation;
  // list of words u64 flags;
  u64 flags;
  u64 *data;
} WORD;

//  main stack
u64 *stack = NULL;
u64 sp = 0;

// dictionary stack
WORD *dictionary = NULL;
u64 here = 0;

// memory for word definitions
WORD *current_def = NULL;
WORD *last_created = NULL;
u64 *code_space = NULL;
u64 code_idx = 0;

// return stack
u64 *rstack = NULL;
u64 rsp = 0;

// control flow stack (IF/ELSE/THEN BEGIN/WHILE/REPEAT etc..)
u64 **cfstack = NULL;
u64 cfsp = 0;

char *bytes_space;
u64 bytes_p;

#define CFPUSH(x)                                                              \
  cfsp >= (u64)CF_STACK ? (printf("%s[ERROR] Control Flow overflow%s\n",       \
                                  SETREDCOLOR, RESETALLSTYLES),                \
                           NULL)                                               \
                        : (cfstack[cfsp++] = (x))
#define CFPOP()                                                                \
  (cfsp == (u64)0 ? (printf("%s[ERROR] Control Flow underflow%s\n",            \
                            SETREDCOLOR, RESETALLSTYLES),                      \
                     NULL)                                                     \
                  : cfstack[--cfsp])

// dynamic heap allocated memory
u64 *data_space = NULL;
u64 dp = 0;

// u64 *blocks_base = NULL;
u_int8_t *blocks_base = NULL;

int tmp_block_editor_fd;
u64 curr_block_num = -1;
u64 editor_dirty = 0;

u64 num_base = 10;

// instruction pointer
u64 *ip = NULL;
MODE f_mode = INTERPRET;

char *current_line_buffer = NULL;
u64 current_line_length = 256;
u64 input_index = 0;
#define CELLSIZE sizeof(u64)

void execute(WORD *w);

void allstats(WORD *w) {
  UNUSED(w);
  printf("skforth stats: \n NAME | MAX_BYTES | CURRENT_BYTES | MAX_CELLS | "
         "CURRENT_CELLS\n"
         "[STACK] %llu %llu %llu %llu\n[DATA] %llu %llu %llu %llu\n[CODE] %llu "
         " %s %llu %s\n[RSTACK] %llu %llu %llu %llu\n[CFSTACK] %llu %llu %llu "
         "%llu\n[BYTESSPACE] %llu %llu \n",
         ((u64)STACK_SIZE * CELLSIZE), sp * CELLSIZE, (u64)STACK_SIZE, sp,
         DATA_SIZE * CELLSIZE, dp * CELLSIZE, DATA_SIZE, dp,
         ((u64)MAX_CODE_SPACE * CELLSIZE), "<no info>", (u64)MAX_CODE_SPACE,
         "<no info>", ((u64)STACK_SIZE * CELLSIZE), rsp * CELLSIZE,
         (u64)STACK_SIZE, rsp, ((u64)CF_STACK * CELLSIZE), cfsp * CELLSIZE,
         (u64)CF_STACK, (u64)cfsp, MAX_BYTES_SPACE, bytes_p);
}

int spush(u64 v) {
  if (sp == STACK_SIZE) {
    printf("%s[ERROR] Stack is full\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return 0;
  }
  stack[sp++] = v;
  return -1;
}
u64 spop(void) {
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return 0xDEADBEEF;
  }
  return stack[--sp];
}

void memcpy_cells(WORD *w) {
  UNUSED(w);
  if (sp < 3) {
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
  }
  u64 *dest = (u64 *)spop();
  u64 *src = (u64 *)spop();
  u64 len = spop();
  if (!src) {
    printf("%s[ERROR] Invalid address source to copy from\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
  }
  u64 *dest_ptr = memcpy(dest, src, (ssize_t)len * CELLSIZE);
  if (!dest_ptr) {
    printf("%s[ERROR] Invalid address source to copy from\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    spush(0);
  }
}

void memcpy_bytes(WORD *w) {
  UNUSED(w);
  if (sp < 3) {
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
  }
  u64 len = spop();
  unsigned char *dest = (unsigned char *)spop();
  unsigned char *src = (unsigned char *)spop();
  if (!src) {
    printf("%s[ERROR] Invalid address source to copy from\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
  }
  u64 *dest_ptr = memcpy(dest, src, (ssize_t)len);
  if (!dest_ptr) {
    printf("%s[ERROR] Invalid address source to copy from\n%s", SETREDCOLOR,
           RESETALLSTYLES);
  }
}
WORD *find_word(const char *name, u64 len);

void lit(WORD *w) {
  UNUSED(w);
  u64 value = *ip++;
  spush(value);
}
void literal(WORD *w) {
  UNUSED(w);

  if (f_mode == INTERPRET) {
    printf("%s[ERROR] LITERAL only valid in compile mode%s\n", SETREDCOLOR,
           RESETALLSTYLES);
    return;
  }

  if (sp == 0) {
    printf("%s[ERROR] LITERAL expects value on stack%s\n", SETREDCOLOR,
           RESETALLSTYLES);
    return;
  }

  u64 val = spop();
  WORD *lit = find_word("LIT", 3);

  code_space[code_idx++] = (u64)lit;
  code_space[code_idx++] = val;
}

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
    printf("%s[ERROR] Max number of WORDS reached%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  WORD *w = &dictionary[here++];
  w->name = name;
  w->code = code;
  w->continuation = (u64 *)continuation_wordlist;
  w->flags = flags;
}

int streq_len(const char *a, const char *b, u64 len) {
  for (u64 x = 0; x < len; x += 1)
    if (a[x] != b[x])
      return 0;
  return b[len] == '\0';
}

WORD *find_word(const char *name, u64 len) {
  for (i64 x = here - 1; x >= 0; x -= 1) {
    if (streq_len(name, dictionary[x].name, len))
      return &dictionary[x];
  }

  return NULL;
}

// primitive: . (print top of stack)
void dot(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty.\n%s", SETREDCOLOR, RESETALLSTYLES);
    return;
  }
  u64 val = spop();
  switch (num_base) {
  case 10:
    printf("%llu ", val);
    break;
  case 16:
    printf("%llX ", val);
    break;
  case 8:
    printf("%llo ", val);
    break;
  default:
    break;
  }
}
void words(WORD *w) {
  UNUSED(w);
  for (i64 x = here - 1; x >= 0; x -= 1) {
    printf("%s ", dictionary[x].name);
  }
  printf("\n");
}

void type(WORD *w) {
  UNUSED(w);
  if (f_mode == COMPILE) {
    WORD *word = find_word("TYPE", 4);
    code_space[code_idx++] = (u64)word;
    return;
  }
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small.\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 len = spop();
  u64 addr = spop();

  unsigned char *p = (unsigned char *)addr;

  for (u64 x = 0; x < len; x += 1) {
    putchar(p[x]);
  }
}

void cr(WORD *w) {
  UNUSED(w);
  putchar('\n');
}
// primitive: .s (print stack size and it's elements)
void dot_stack(WORD *w) {
  UNUSED(w);
  printf("[STACK] <%llu> ", sp);
  for (u64 x = 0; x < sp; x += 1) {
    switch (num_base) {
    case 10:
      printf("%llu ", stack[x]);
      break;
    case 16:
      printf("%llX ", stack[x]);
      break;
    case 8:
      printf("%llo ", stack[x]);
      break;
    default:
      break;
    }
  }
  printf("\n");
}
void lshift_word(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a << b);
}
void rshift_word(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a >> b);
}
// primitive: +
void add(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 b = spop();
  u64 a = spop();
  spush(a * b);
}
void dup_word(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush(stack[sp - 1]);
}
void swap(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush(stack[sp - 2]);
}
void double_over(WORD *w) {
  UNUSED(w);
  if (sp < 3) {
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush(stack[sp - 3]);
}
void slash_mod(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is to small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is to small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is to small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is empty \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 a = spop();
  spush(a == 0);
}
void equals(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 val = spop();
  spush(val > 0);
}
void notzero(WORD *w) {
  UNUSED(w);
  if (sp < 1) {
    printf("%s[ERROR] Stack is too small \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 val = spop();
  spush(val != 0);
}
void minusone(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush(spop() - 1);
}
void b_at(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 addr = spop();
  spush((u64)(*(unsigned char *)addr));
}
void b_store(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is empty \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  sp = 0;
}
void clear_data_word(WORD *w) {
  UNUSED(w);
  if (!data_space) {
    printf("%s[ERROR] There is no memory allocated in Data space to clear\n%s",
           SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  munmap(data_space, DATA_SIZE * CELLSIZE);
  data_space = NULL;
  dp = 0;
  DATA_SIZE = 0;
}
void grow_data(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 n = spop();
  void *new = mremap(data_space, DATA_SIZE * CELLSIZE,
                     (DATA_SIZE + n) * CELLSIZE, MREMAP_MAYMOVE);
  if (new == MAP_FAILED) {
    printf("%s[ERROR] Could not regrow Data memory\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    printf("%s[ERROR] MREMAP failed to regrow to %llu CELLS in "
           "virtual memory for "
           "data space (old size: %llu )\n[SYS MSG] %s%s\n",
           SETREDCOLOR, (u64)DATA_SIZE + n, DATA_SIZE, strerror(errno),
           RESETALLSTYLES);

    print_source_line();
    return;
  }
  DATA_SIZE += n;
  data_space = (u64 *)new;
}
int c_next_token(char **addr, u64 *len);

void here_data(WORD *w) {
  UNUSED(w);
  spush((u64)(data_space + dp));
}

void here_code_word(WORD *w) {
  UNUSED(w);
  spush((u64)&code_space[code_idx]);
}

void alloc_code_word(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
  }
  u64 increment = spop();

  code_idx += increment;
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
  spush(a & b);
}

void alloc_data(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 num_cells = spop();
  if (num_cells + dp > DATA_SIZE) {
    printf("%s[ERROR] Number of cells to alloc exceed Data Area capacity\n%s",
           SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  dp += num_cells;
}

void at_ptr(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 ptr = spop();
  spush(*(u64 *)ptr);
}
void write_ptr(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 *addr = (u64 *)spop();
  if (!addr) {
    printf("%s[ERROR] Not a valid pointer given\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 val = spop();
  *addr = val;
}
void to_r(WORD *w) {
  UNUSED(w);
  if (sp == 0) {
    printf("%s[ERROR] Stack is empty\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  if (rsp >= STACK_SIZE) {
    printf("%s[ERROR] Return stack overflow\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  rstack[rsp++] = spop();
}
void from_r(WORD *w) {
  UNUSED(w);
  if (rsp == 0) {
    printf("%s[ERROR] Return stack is empty\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  if (sp >= STACK_SIZE) {
    printf("%s[ERROR] Stack is full and cant return value from Return stack to "
           "main stack\n%s",
           SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush(rstack[--rsp]);
}

void ensure_chars(u64 chars);

char *save_string(const char *src, u64 len) {
  ensure_chars(len);
  char *dst = bytes_space + bytes_p;
  memcpy(dst, src, len);
  dst[len] = '\0';
  bytes_p += len + 1;
  return dst;
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
    printf("%s[ERROR] CREATE expects a name\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 len = spop();
  char *addr = (char *)spop();

  if (len == 0) {
    printf("%s[ERROR] CREATE expects a name\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }

  char *name = save_string(addr, len);

  if (!name) {
    printf("%s[ERROR] CREATE: strdup failed \n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }

  if (here + 1 == (u64)MAX_WORDS) {
    printf("%s[ERROR] Max number of WORDS reached in dictionary area\n%s",
           SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is empty\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 val = spop();
  ensure_data(1);
  data_space[dp++] = val;
}

void see_word(WORD *w) {
  UNUSED(w);

  execute(find_word("PARSE-NAME", 10));
  u64 len = spop();
  char *addr = (char *)spop();

  if (len == 0) {
    printf("%s[ERROR] see expects a name\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }

  WORD *w_tosee = find_word(addr, len);

  if (!w_tosee) {
    printf("%s[ERROR] Unknown word to see: %.*s%s", SETREDCOLOR, (int)len, addr,
           RESETALLSTYLES);
    return;
  }

  printf(": %s", w_tosee->name);
  printf("\n");

  // TODO: ->code & ->continuantion=NULL is a primitive implementation
  if (!w_tosee->continuation) {
    printf(" <primitive>\n;\n");
    return;
  }

  u64 *p = w_tosee->continuation;

  while (*p) {
    WORD *cw = (WORD *)*p++;

    if (cw == find_word("LIT", 3)) {
      u64 val = *p++;
      printf("  LIT %llu\n", val);
    } else {
      printf("  %s\n", cw->name);
    }
  }

  printf(";\n");
}

void constant_var_word(WORD *w) {
  UNUSED(w);
  execute(find_word("PARSE-NAME", 10));
  u64 len = spop();
  char *addr = (char *)spop();
  if (len == 0) {
    printf("%s[ERROR] constvar: expects a name\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }

  char *name = save_string(addr, len);

  if (!name) {
    printf("%s[ERROR] constant expects a name\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  if (sp == 0) {
    printf("%s[ERROR] constant expects a value on stack\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }

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
  if (dp + cells <= DATA_SIZE)
    return;
  u64 new_cap = DATA_SIZE ? DATA_SIZE : 16;
  while (new_cap < dp + cells)
    new_cap *= 2;

  void *new = mremap(data_space, DATA_SIZE * CELLSIZE, new_cap * CELLSIZE,
                     MREMAP_MAYMOVE);
  if (new == MAP_FAILED) {
    printf("%s[ERROR] MREMAP failed to regrow to %llu CELLS in "
           "virtual memory for "
           "data space (old size: %llu )\n[SYS MSG] %s%s\n",
           SETREDCOLOR, (u64)new_cap, DATA_SIZE, strerror(errno),
           RESETALLSTYLES);

    print_source_line();
    return;
  }
  data_space = (u64 *)new;
  DATA_SIZE = new_cap;
}

void ensure_chars(u64 chars) {
  if (bytes_p + chars <= MAX_BYTES_SPACE)
    return;
  u64 new_cap = MAX_BYTES_SPACE ? MAX_BYTES_SPACE : 256;
  while (new_cap < bytes_p + chars)
    new_cap *= 2;

  void *new = mremap(bytes_space, MAX_BYTES_SPACE, new_cap, MREMAP_MAYMOVE);
  if (new == MAP_FAILED) {
    printf("%s[ERROR] MREMAP failed to regrow to %llu bytes in "
           "virtual memory for "
           "char space (old size: %llu )\n[SYS MSG] %s%s\n",
           SETREDCOLOR, (u64)new_cap, MAX_BYTES_SPACE, strerror(errno),
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  bytes_space = (char *)new;
  MAX_BYTES_SPACE = new_cap;
}

void bye(WORD *w) {
  UNUSED(w);
  exit(EXIT_SUCCESS);
}

// : (compile mode)
void colon(WORD *ww) {
  UNUSED(ww);
  if (cfsp != 0) {
    printf("%s[ERROR] Unresolved control structure\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    exit(EXIT_FAILURE);
  }

  execute(find_word("PARSE-NAME", 10));
  u64 len = spop();
  char *addr = (char *)spop();
  if (len == 0) {
    printf("%s[ERROR] Expected word name after ':'\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }

  char *name = save_string(addr, len);

  if (!name) {
    printf("%s[ERROR] Expected word name after ':'\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  WORD *nw = &dictionary[here++];
  nw->name = name;
  nw->code = NULL;
  nw->continuation = &code_space[code_idx];
  current_def = nw;
  f_mode = COMPILE;
}
// ;(end compile mode)
void semicolon(WORD *w) {
  UNUSED(w);
  code_space[code_idx++] = (u64)NULL;
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
    printf("%s[ERROR] IF only valid in compile mode\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  WORD *zb = find_word("0BRANCH", 7);
  code_space[code_idx++] = (u64)zb;
  code_space[code_idx++] = 0;
  CFPUSH(&code_space[code_idx - 1]);
}
void else_word(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("%s[ERROR] ELSE only valid in compile mode\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  WORD *br = find_word("BRANCH", 6);
  code_space[code_idx++] = (u64)br;
  code_space[code_idx++] = 0;
  u64 *if_placeholder = CFPOP();
  *if_placeholder = (u64)&code_space[code_idx];
  CFPUSH(&code_space[code_idx - 1]);
}
void then_word(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("%s[ERROR] THEN only valid in compile mode\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 *placeholder = CFPOP();
  *placeholder = (u64)&code_space[code_idx];
}
// begin\while\repeat branching
void begin(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("%s[ERROR] BEGIN is only valid in compile mode\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  CFPUSH(&code_space[code_idx]);
}
void while_word(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("%s[ERROR] WHILE is only valid in compile mode\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  WORD *zb = find_word("0BRANCH", 7);
  code_space[code_idx++] = (u64)zb;
  code_space[code_idx++] = 0;
  CFPUSH(&code_space[code_idx - 1]);
}

void repeat(WORD *w) {
  UNUSED(w);
  if (f_mode != COMPILE) {
    printf("%s[ERROR] REPEAT is only valid in compile mode\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 *while_placeholder = CFPOP();
  u64 *begin_addr = CFPOP();
  WORD *br = find_word("BRANCH", 6);
  code_space[code_idx++] = (u64)br;
  code_space[code_idx++] = (u64)begin_addr;
  *(u64 *)while_placeholder = (u64)&code_space[code_idx];
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

  execute(find_word("PARSE-NAME", 10));
  u64 len = spop();
  char *addr = (char *)spop();
  if (len == 0) {
    printf("%s[ERROR] INCLUDE expects a name\n%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  char *fname = save_string(addr, len);

  if (!fname) {
    printf("%s[ERROR] INCLUDE expects filename\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  FILE *f = fopen(fname, "r");
  if (!f) {
    printf("%s[ERROR] Could not open %s\n%s", SETREDCOLOR, fname,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    main_interpret_line(line);
  }
  fclose(f);
  printf("%sDONE\n%s", SETGREENCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] Stack is full%s", SETREDCOLOR, RESETALLSTYLES);
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
    printf("%s[ERROR] INTERPRET-TOKEN expects addr len\n%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }

  u64 len = spop();
  char *addr = (char *)spop();

  WORD *w = find_word(addr, len);

  if (w) {
    if (f_mode == INTERPRET) {
      execute(w);
    } else {
      if (w->flags & IMMEDIATE)
        execute(w);
      else
        code_space[code_idx++] = (u64)w;
    }
  } else {
    char tmp[64];
    char *end;

    if (sizeof(tmp) < len) {
      printf("%sToken to long: %.*s\n%s", SETREDCOLOR, (int)len, addr,
             RESETALLSTYLES);
      exit(EXIT_FAILURE);
    }

    memcpy(tmp, addr, len);
    tmp[len] = '\0';
    u64 n = strtoull(tmp, &end, num_base);
    if (*end == '\0') {
      if (f_mode == INTERPRET)
        spush(n);
      else {
        WORD *litw = find_word("LIT", 3);
        code_space[code_idx++] = (u64)litw;
        code_space[code_idx++] = n;
      }

    } else {
      printf("%sUnknown word: %.*s\n%s", SETREDCOLOR, (int)len, addr,
             RESETALLSTYLES);
      print_source_line();
      return;
    }
  }
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
}

void parse_string_word(WORD *w) {
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

  while (i < len_src && (unsigned char)src[i] != '"')
    i++;

  input_index = i + 1;
  spush((u64)start);
  spush((u64)(src + i - start));
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

void system_word(WORD *w) {
  UNUSED(w);
  if (f_mode == COMPILE) {
    WORD *word = find_word("SHELL-CMD", 9);
    code_space[code_idx++] = (u64)word;
    return;
  }
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 len = spop();
  char *start = (char *)spop();
  char *command = save_string(start, len);
  system(command);
}

void blocks_base_word(WORD *w) {
  UNUSED(w);
  if (sp >= STACK_SIZE) {
    printf("%s[ERROR] Stack is full%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush((u64)blocks_base);
}
void block_size_word(WORD *w) {
  UNUSED(w);
  if (sp >= STACK_SIZE) {
    printf("%s[ERROR] Stack is full%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush(BLOCK_SIZE);
}
void interpret_block_word(WORD *w) {
  UNUSED(w);
  if (sp < 2) {
    printf("%s[ERROR] Stack is too small%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 blk_len = spop();
  char *base = (char *)spop();

  char *p = base;
  char *end = base + blk_len;

  while (p < end) {
    char *line_start = p;

    while (p < end && *p != '\n')
      p++;

    char nl_saved = *p;
    *p = '\0';

    main_interpret_line(line_start);

    *p = nl_saved;

    if (p < end)
      p++;
  }
}
void load_external_editor_buffer(WORD *w) {
  if (sp < 2) {
    UNUSED(w);
    printf("%s[ERROR] Stack is too small%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 blk_len = spop();
  char *base = (char *)spop();

  // We close and reopen the file to force state re-synchronization.
  // After an external editor (fork+exec via system()), the file descriptor
  // state (offsets, cache associations) can become incoherent when mixing
  // mmap and stdio I/O.
  // Closing and reopening forces inode and page-cache revalidation.
  //
  //  NOTE: This is not a data sync issue (msync/fsync are insufficient here),
  // but a file-descriptor state issue after external modification. I assume
  close(tmp_block_editor_fd);
  char line[256];
  char *home = getenv("HOME");
  if (home == NULL) {
    fprintf(stderr, "%sError: HOME environment variable not found.%s\n",
            SETREDCOLOR, RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  sprintf(line, "%s/.config/skforth/block_editor.fs", home);

  tmp_block_editor_fd = open(line, O_RDWR);

  lseek(tmp_block_editor_fd, 0, SEEK_SET);
  ftruncate(tmp_block_editor_fd, BLOCK_SIZE);
  int res = write(tmp_block_editor_fd, base, blk_len * sizeof(char));
  if (res == -1) {
    printf("%s[ERROR] Could not load BLOCK to tmp editor buffer%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
}
void save_external_editor_buffer(WORD *w) {
  UNUSED(w);
  if (sp < 1) {
    printf("%s[ERROR] Stack is too small%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 blk_idx = spop();

  // We close and reopen the file to force state re-synchronization.
  // After an external editor (fork+exec via system()), the file descriptor
  // state (offsets, cache associations) can become incoherent when mixing
  // mmap and stdio I/O.
  // Closing and reopening forces inode and page-cache revalidation.
  //
  //  NOTE: This is not a data sync issue (msync/fsync are insufficient here),
  // but a file-descriptor state issue after external modification. I assume
  close(tmp_block_editor_fd);
  char line[256];
  char *home = getenv("HOME");
  if (home == NULL) {
    fprintf(stderr, "%sError: HOME environment variable not found.%s\n",
            SETREDCOLOR, RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  sprintf(line, "%s/.config/skforth/block_editor.fs", home);

  tmp_block_editor_fd = open(line, O_RDWR);

  if (blk_idx >= NUM_BLOCKS) {
    printf("%s[ERROR] Invalid block index: %llu %s", SETREDCOLOR, blk_idx,
           RESETALLSTYLES);
    print_source_line();
    return;
  }

  unsigned char *blk_addr =
      ((unsigned char *)blocks_base) + (blk_idx * BLOCK_SIZE);

  lseek(tmp_block_editor_fd, 0, SEEK_SET);

  int r = read(tmp_block_editor_fd, blk_addr, BLOCK_SIZE);
  if (r == -1) {
    printf("%s\n", strerror(errno));
    printf("%s[ERROR] Could not load BLOCK to tmp editor buffer%s", SETREDCOLOR,
           RESETALLSTYLES);
    print_source_line();
    return;
  }
}

void blk_word(WORD *w) {
  UNUSED(w);
  if (sp >= STACK_SIZE) {
    printf("%s[ERROR] Stack is full%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }

  spush(curr_block_num);
}
void blk_change_word(WORD *w) {
  UNUSED(w);
  if (sp < 1) {
    printf("%s[ERROR] Stack is too small%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  u64 curr_blk = spop();
  curr_block_num = curr_blk;
}
void num_blocks_word(WORD *w) {
  UNUSED(w);
  if (sp >= STACK_SIZE) {
    printf("%s[ERROR] Stack is full%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush(NUM_BLOCKS);
}
void editor_dirty_word(WORD *w) {
  UNUSED(w);
  if (sp >= STACK_SIZE) {
    printf("%s[ERROR] Stack is full%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush((u64)&editor_dirty);
}
void editor_block_word(WORD *w) {
  UNUSED(w);
  if (sp >= STACK_SIZE) {
    printf("%s[ERROR] Stack is full%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush((u64)&curr_block_num);
}

void number_base_ptr_word(WORD *w) {
  UNUSED(w);
  if (sp >= STACK_SIZE) {
    printf("%s[ERROR] Stack is full%s", SETREDCOLOR, RESETALLSTYLES);
    print_source_line();
    return;
  }
  spush((u64)&num_base);
}

void fill_word(WORD *w) {
  UNUSED(w);

  u64 val = spop();
  u64 size = spop();
  u64 *addr = (u64 *)spop();
  memset(addr, val, size);
}

void exec_code(WORD *w) {
  u64 payload = spop();
  u64 instr = spop();

  char len = instr >> 56;
  u64 bytes = instr & 0x00FFFFFFFFFFFFFF;

  char *buf = &bytes_space[bytes_p];

  memcpy(buf, &bytes, len);
  u64 start = bytes_p;
  bytes_p += len;

  memcpy(&bytes_space[bytes_p], &payload, CELLSIZE);
  bytes_p += CELLSIZE;

  void (*fn)(void) = (void *)buf;
  fn();

  bytes_p = start;
}

void main_stack_address(WORD *w) {
  UNUSED(w);
  spush((u64)stack);
}
void main_stack_p_address(WORD *w) {
  UNUSED(w);
  spush((u64)&sp);
}
void return_stack_address(WORD *w) {
  UNUSED(w);
  spush((u64)rstack);
}
void return_stack_p_address(WORD *w) {
  UNUSED(w);
  spush((u64)&rsp);
}

void init(void) {
  add_word("LIT", lit, NULL, 0);
  add_word("0BRANCH", zero_branch, NULL, 0);
  add_word("BRANCH", branch, NULL, 0);
  add_word("COMPTIME", comptime, NULL, 0);
  add_word("INTERPRET", interpret, NULL, 0);
  add_word("SOURCE", source_word, NULL, 0);
  add_word(">IN", in_word, NULL, 0);
  add_word("EXEC-CODE", exec_code, NULL, 0);
  add_word("_stack", main_stack_address, NULL, 0);
  add_word("_sp", main_stack_p_address, NULL, 0);
  add_word("_rstack", return_stack_address, NULL, 0);
  add_word("_rsp", return_stack_p_address, NULL, 0);
  add_word("NUMBASE", number_base_ptr_word, NULL, 0);

  add_word("SHELL-CMD", system_word, NULL, 0);
  add_word("BLOCKS-BASE", blocks_base_word, NULL, 0);
  add_word("BLOCK-SIZE", block_size_word, NULL, 0);
  add_word("INTERPRET-BLOCK", interpret_block_word, NULL, 0);
  add_word("LOAD-EXTRN-EDITBUFF", load_external_editor_buffer, NULL, 0);
  add_word("SAVE-EXTRN-EDITBUFF", save_external_editor_buffer, NULL, 0);
  add_word("#BLOCKS", num_blocks_word, NULL, 0);
  add_word("EDITOR-DIRTY", editor_dirty_word, NULL, 0);
  add_word("EDITOR-BLOCK", editor_block_word, NULL, 0);
  add_word("BLK", blk_word, NULL, 0);
  add_word("BLK!", blk_change_word, NULL, 0);

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
  add_word("&", and_word, NULL, 0);
  add_word("*", multiply, NULL, 0);
  add_word("/mod", slash_mod, NULL, 0);
  add_word("dup", dup_word, NULL, 0);
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
  add_word("lshift", lshift_word, NULL, 0);
  add_word("rshift", rshift_word, NULL, 0);
  add_word("<<", lshift_word, NULL, 0);
  add_word(">>", rshift_word, NULL, 0);

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
  add_word("b@", b_at, NULL, 0);
  add_word("b!", b_store, NULL, 0);
  add_word("bl", add_bl, NULL, 0);
  add_word("GROW", grow_data, NULL, IMMEDIATE);
  add_word("HERE", here_data, NULL, 0);
  add_word("ALLOC", alloc_data, NULL, 0);
  add_word("COPY-CELLS", memcpy_cells, NULL, 0);
  add_word("COPY-BYTES", memcpy_bytes, NULL, 0);
  add_word("TYPE", type, NULL, 0);
  add_word("FILL", fill_word, NULL, 0);
  add_word("LITERAL", literal, NULL, 0);
  add_word("constvar:", constant_var_word, NULL, 0);
  add_word("CREATE", create_struct, NULL, 0);
  add_word("PARSE-NAME", parse_name_word, NULL, 0);
  add_word("PARSE-STRING", parse_string_word, NULL, 0);
  add_word("HERE-CODE", here_code_word, NULL, 0);
  add_word("ALLOC-CODE", alloc_code_word, NULL, 0);
  add_word(",", comma, NULL, 0);
  add_word("INTERPRET-TOKEN", interpret_token_word, NULL, 0);
  add_word("IMMEDIATE", immediate, NULL, IMMEDIATE);

  add_word("see", see_word, NULL, 0);
  add_word("bye", bye, NULL, 0);

  add_word("INTERPRET-LINE", interpret_line_c_word, NULL, 0);
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

  interpret_line_c_word(NULL);
}

void init_config_file(char *home) {
  char configpath[256];
  snprintf(configpath, sizeof(configpath), "%s/.config/skforth", home);
  switch (mkdir(configpath, 0755)) {
  case 0: // created
    printf("%s$HOME/.config/skforth/  directory not found... creating it with "
           "default values\n%s",
           SETGREENCOLOR, RESETALLSTYLES);
    memset(configpath, 0, sizeof(configpath));
    snprintf(configpath, sizeof(configpath), "%s/.config/skforth/config.fs",
             home);
    FILE *config = fopen(configpath, "w+");

    // create BLOCKS file
    {

      printf("%sCreating BLOCKS.blk ...%s", SETGREENCOLOR, RESETALLSTYLES);
      char block_path[256];
      snprintf(block_path, sizeof(block_path), "%s/.config/skforth/BLOCKS.blk",
               home);
      FILE *blocks = fopen(block_path, "w+");
      if (!blocks) {
        printf("%s[ERROR] Failed to create BLOCKS file at "
               "$HOME/.config/skforth/\n%s",
               SETREDCOLOR, RESETALLSTYLES);
      } else {
        fclose(blocks);
      }

      printf("%sDone\n%s", SETGREENCOLOR, RESETALLSTYLES);
    }

    long len = ftell(config);
    if (len == 0) {
      fprintf(
          config,
          "1024        ( BLOCK_SIZE ) \n"
          "64          ( NUM_BLOCKS ) \n"
          "32          ( STACK_SIZE ) \n"
          "5000        ( MAX_WORDS ) \n"
          "1024 64 *   ( MAX_CODE_SPACE ) \n"
          "256         ( CF_STACK -- This is for the control flow stack ) \n"
          "1024        ( DATA_SIZE ) \n"
          "1024 64 *   ( MAX_BYTES_SPACE )");
    }
    fclose(config);
    break;
  case -1:
    switch (errno) {
    case EACCES:
    case EPERM:
      printf("%s[ERROR] No permissions no create config file on "
             "$HOME/.config/skforth/\n%s",
             SETREDCOLOR, RESETALLSTYLES);
      exit(EXIT_FAILURE);
      break;
    case EIO:
      printf("%s[ERROR] IO error trying to create config file on "
             "$HOME/.config/skforth/\n%s",
             SETREDCOLOR, RESETALLSTYLES);
      exit(EXIT_FAILURE);
      break;
    case ENOSPC:
      printf("%s[ERROR] No memory available to create config file on "
             "$HOME/.config/skforth/\n%s",
             SETREDCOLOR, RESETALLSTYLES);
      exit(EXIT_FAILURE);
      break;
    case EEXIST:
      memset(configpath, 0, sizeof(configpath));
      snprintf(configpath, sizeof(configpath), "%s/.config/skforth/config.fs",
               home);
      FILE *config = fopen(configpath, "r+");
      if (!config) {
        config = fopen(configpath, "w+");
      }
      // check if BLOCKS file exits
      {
        char block_path[256];
        snprintf(block_path, sizeof(block_path),
                 "%s/.config/skforth/BLOCKS.blk", home);
        FILE *blocks = fopen(block_path, "r+");
        if (!blocks) {
          blocks = fopen(block_path, "w+");
        }
        fclose(blocks);
      }

      FILE *end = config;
      fseek(end, 0, SEEK_END);

      ftell(config);
      if (ftell(end) == 0) {
        fprintf(
            config,
            "1024        ( BLOCK_SIZE ) \n"
            "64          ( NUM_BLOCKS ) \n"
            "32          ( STACK_SIZE ) \n"
            "5000        ( MAX_WORDS ) \n"
            "1024 64 *   ( MAX_CODE_SPACE ) \n"
            "256         ( CF_STACK -- This is for the control flow stack ) \n"
            "1024        ( DATA_SIZE ) \n"
            "1024 64 *   ( MAX_BYTES_SPACE )");
      }
      fclose(config);

      break;
    }
    break;
  default:
    printf("%s[ERROR] Something happened creating config file on "
           "$HOME/.config/skforth/\n[SYS MESSAGE] %s ERR number:%d %s",
           SETREDCOLOR, strerror(errno), errno, RESETALLSTYLES);
    return;
    break;
  }
}

int main(void) {
  char line[256];
  char *home = getenv("HOME");
  if (home == NULL) {
    fprintf(stderr, "%sError: HOME environment variable not found.%s\n",
            SETREDCOLOR, RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }

  // init memory settings from config file
  {
    u64 config_stack[CONFIG_STACK_SIZE];
    WORD config_dic[CONFIG_DIC_SIZE];
    stack = config_stack;
    dictionary = config_dic;
    MAX_WORDS = CONFIG_DIC_SIZE;
    STACK_SIZE = CONFIG_STACK_SIZE;

    add_word("INTERPRET-TOKEN", interpret_token_word, NULL, 0);
    add_word("*", multiply, NULL, 0);

    init_config_file(home);

    snprintf(line, sizeof(line), "%s/.config/skforth/config.fs", home);

    FILE *f_config = fopen(line, "r");
    if (!f_config) {
      printf("%s Config not found\n%s", SETREDCOLOR, RESETALLSTYLES);
      exit(EXIT_FAILURE);
    }
    printf("%sLoading $HOME/.config/skforth/config.fs...%s", SETGREENCOLOR,
           RESETALLSTYLES);
    while (fgets(line, sizeof(line), f_config)) {
      main_interpret_line(line);
    }
    fclose(f_config);
    printf("%sDONE\n\n%s", SETGREENCOLOR, RESETALLSTYLES);

    if (sp < 7) {
      printf(
          "%s[ERROR] Not enough values for memory settings on "
          "$HOME/.config/skforth/\n%s[TIP] You can delete the current config "
          "file and rerun skforth and a fresh config will be created! %s\n",
          SETREDCOLOR, SETYELLOWCOLOR, RESETALLSTYLES);
      exit(EXIT_FAILURE);
    }
    MAX_BYTES_SPACE = spop();
    DATA_SIZE = spop();
    CF_STACK = spop();
    MAX_CODE_SPACE = spop();
    MAX_WORDS = spop();
    STACK_SIZE = spop();
    NUM_BLOCKS = spop();
    BLOCK_SIZE = spop();
  }
  // set virtual memory with mmap with desired sizes

  stack = mmap(NULL, STACK_SIZE * CELLSIZE, PROT_READ | PROT_WRITE,
               MAP_ANONYMOUS | MAP_SHARED, -1, 0);

  if (stack == MAP_FAILED) {
    printf("%s[ERROR] MMAP failed to reserve %llu CELLS in virtual memory for "
           "the main stack\n[SYS MSG] %s%s\n",
           SETREDCOLOR, (u64)STACK_SIZE, strerror(errno), RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  sp = 0;

  bytes_space = mmap(NULL, MAX_BYTES_SPACE * sizeof(char),
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (bytes_space == MAP_FAILED) {
    printf("%s[ERROR] MMAP failed to reserve %llu BYTES in "
           "virtual memory for "
           "the bytes space\n[SYS MSG] %s%s\n",
           SETREDCOLOR, ((u64)MAX_WORDS * sizeof(WORD)), strerror(errno),
           RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  bytes_p = 0;

  dictionary = mmap(NULL, MAX_WORDS * sizeof(WORD), PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (dictionary == MAP_FAILED) {
    printf("%s[ERROR] MMAP failed to reserve %llu CELLS (%llu WORDS) in "
           "virtual memory for "
           "the dictionary\n[SYS MSG] %s%s\n",
           SETREDCOLOR, ((u64)MAX_WORDS * sizeof(WORD)), (u64)MAX_WORDS,
           strerror(errno), RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  here = 0;

  current_def = NULL;
  last_created = NULL;

  code_space =
      mmap(NULL, MAX_CODE_SPACE * CELLSIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
           MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (code_space == MAP_FAILED) {
    printf("%s[ERROR] MMAP failed to reserve %llu CELLS in "
           "virtual memory for "
           "the code_space\n[SYS MSG] %s%s\n",
           SETREDCOLOR, MAX_CODE_SPACE, strerror(errno), RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  code_idx = 0;

  rstack = mmap(NULL, STACK_SIZE * CELLSIZE, PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (rstack == MAP_FAILED) {
    printf("%s[ERROR] MMAP failed to reserve %llu CELLS in "
           "virtual memory for "
           "the return_stack\n[SYS MSG] %s%s\n",
           SETREDCOLOR, (u64)STACK_SIZE, strerror(errno), RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  rsp = 0;

  cfstack = mmap(NULL, CF_STACK * sizeof(u64 *), PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (cfstack == MAP_FAILED) {
    printf("%s[ERROR] MMAP failed to reserve %llu CELLS in "
           "virtual memory for "
           "the return_stack\n[SYS MSG] %s%s\n",
           SETREDCOLOR, (u64)CF_STACK, strerror(errno), RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  cfsp = 0;

  data_space = mmap(NULL, DATA_SIZE * CELLSIZE, PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (data_space == MAP_FAILED) {
    printf("%s[ERROR] MMAP failed to reserve %llu CELLS in "
           "virtual memory for "
           "data space\n[SYS MSG] %s%s\n",
           SETREDCOLOR, (u64)DATA_SIZE, strerror(errno), RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }

  // BLOCKS
  printf("%sPreparing BLOCKS.blk...%s\n", SETGREENCOLOR, RESETALLSTYLES);
  char block_path[256];
  snprintf(block_path, sizeof(block_path), "%s/.config/skforth/BLOCKS.blk",
           home);
  int block_fd = open(block_path, O_RDWR);

  if (block_fd == -1) {
    printf("%sBLOCKS.blk not found\n%s", SETREDCOLOR, RESETALLSTYLES);
  } else {
    if (ftruncate(block_fd, BLOCK_SIZE * NUM_BLOCKS) == -1) {
      printf("%s[ERROR] Failed to truncate BLOCKS.blk%s\n", SETREDCOLOR,
             RESETALLSTYLES);
    } else {
      blocks_base = mmap(NULL, BLOCK_SIZE * NUM_BLOCKS, PROT_READ | PROT_WRITE,
                         MAP_SHARED, block_fd, 0);

      if (blocks_base == MAP_FAILED) {
        printf("%s[ERROR] MMAP failed to reserve %llu BLOCKS in "
               "virtual memory.\n[SYS MSG] %s%s\n",
               SETREDCOLOR, (u64)NUM_BLOCKS, strerror(errno), RESETALLSTYLES);
        goto skipblocks;
      }

      memset(block_path, 0, sizeof(block_path)); // reuse the buffer
      printf("%sCreating a temporary block editor file... \n%s", SETGREENCOLOR,
             RESETALLSTYLES);
      sprintf(block_path, "%s/.config/skforth/block_editor.fs", home);
      tmp_block_editor_fd = creat(block_path, S_IRUSR | S_IWUSR);
      if (tmp_block_editor_fd)
        close(tmp_block_editor_fd);

      tmp_block_editor_fd = open(block_path, O_RDWR);
      if (tmp_block_editor_fd == -1) {
        printf("%s[ERROR] Could not create temporary block editor file%s\n",
               SETREDCOLOR, RESETALLSTYLES);
        goto skipblocks;
      }

      printf("%sDone \n%s", SETGREENCOLOR, RESETALLSTYLES);
    }
  }
skipblocks:

  // setup words
  init();

  memset(line, 0, sizeof(line));
  // load bootstrap file
  FILE *f = fopen("bootstrap.fs", "r");
  if (!f) {
    printf("%sbootstrap.fs not found\n%s", SETREDCOLOR, RESETALLSTYLES);
    exit(EXIT_FAILURE);
  }
  printf("%sLoading bootstrap.fs...\n%s", SETGREENCOLOR, RESETALLSTYLES);
  while (fgets(line, sizeof(line), f)) {
    main_interpret_line(line);
  }
  fclose(f);
  printf("%s$HOME/.config/skforth/config.fs DONE\n\n%s", SETGREENCOLOR,
         RESETALLSTYLES);

  memset(line, 0, sizeof(line));

  // runtime
  fflush(stdout);
  printf("%sWelcome to skforth :D \n%s", SETGREENCOLOR, RESETALLSTYLES);
  printf("%sskforth> %s", SETGREENCOLOR, RESETALLSTYLES);
  while (fgets(line, sizeof(line), stdin)) {
    main_interpret_line(line);
    printf("%sskforth> %s", SETGREENCOLOR, RESETALLSTYLES);
  }

  if (block_fd != -1) {
    close(block_fd);
    munmap(blocks_base, BLOCK_SIZE * NUM_BLOCKS);
    close(tmp_block_editor_fd);
  }

  munmap(bytes_space, MAX_BYTES_SPACE);
  munmap(stack, STACK_SIZE * CELLSIZE);
  munmap(dictionary, MAX_WORDS * sizeof(WORD));
  munmap(code_space, MAX_CODE_SPACE * CELLSIZE);
  munmap(rstack, STACK_SIZE * CELLSIZE);
  munmap(cfstack, CF_STACK * sizeof(u64 *));
  munmap(data_space, DATA_SIZE * CELLSIZE);

  return 0;
}
