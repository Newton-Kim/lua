/*
** $Id: llex.h,v 1.78 2014/10/29 15:38:24 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include "lobject.h"
#include "lzio.h"


#define FIRST_RESERVED	257


#if !defined(LUA_ENV)
#define LUA_ENV		"_ENV"
#endif


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
enum RESERVED {
  /* terminal symbols denoted by reserved words */
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
  TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  /* other terminal symbols */
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR,
  TK_DBCOLON, TK_EOS,
  TK_FLT, TK_INT, TK_NAME, TK_STRING
};

/* number of reserved words */
#define NUM_RESERVED	(cast(int, TK_WHILE-FIRST_RESERVED+1))


typedef union {
  lua_Number r;
  lua_Integer i;
  TString *ts;
} SemInfo;  /* semantics information */


typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;


/* state of the lexer plus state of the parser when shared by all
   functions */
class LexState {
 public:
  int current;  /* current character (charint) */
  int linenumber;  /* input line counter */
  int lastline;  /* line of last token 'consumed' */
  Token t;  /* current token */
 private:
  Token m_lookahead;  /* look ahead token */
 public:
  struct FuncState *fs;  /* current function (parser) */
  struct lua_State *L;
  ZIO *z;  /* input stream */
  Mbuffer *buff;  /* buffer for tokens */
  Table *h;  /* to avoid collection/reuse strings */
  struct Dyndata *dyd;  /* dynamic structures used by the parser */
  TString *source;  /* current source name */
  TString *envn;  /* environment variable name */
 private:
  char m_decpoint;  /* locale decimal point */

 private:
  inline void next (void) {current = z->getc();}
  inline bool currIsNewline(void) {return current == '\n' || current == '\r';}
  inline void save_and_next(void) {save(current); next();}
  l_noret error (const char *msg, int token);
  void save (int c);
  const char *txtToken (int token);
  int llex (SemInfo *seminfo);
  int read_numeral (SemInfo *seminfo);
  int check_next1 (int c);
  void read_string (int del, SemInfo *seminfo);
  int skip_sep (void);
  void read_long_string (SemInfo *seminfo, int sep);
  void inclinenumber (void);
  int readdecesc (void);
  void esccheck (int c, const char *msg);
  void utf8esc (void);
  int readhexaesc (void);
  unsigned long readutf8esc (void);
  int gethexa (void);
  void trydecpoint (TValue *o);
  void buffreplace (char from, char to);
  int check_next2 (const char *set);
 public:
  void setinput (lua_State *L, ZIO *z, TString *source,
                      int firstchar);
  TString *newstring (const char *str, size_t l);
  void nextt (void);
  int lookahead (void);
  l_noret syntaxerror (const char *s);
  const char *token2str (int token);
};


LUAI_FUNC void luaX_init (lua_State *L);

#endif
