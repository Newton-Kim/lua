/*
** $Id: llex.c,v 2.93 2015/05/22 17:45:56 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#define llex_c
#define LUA_CORE

#include "lprefix.h"


#include <locale.h>
#include <string.h>

#include "lua.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"



/* ORDER RESERVED */
static const char *const luaX_tokens [] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
    "//", "..", "...", "==", ">=", "<=", "~=",
    "<<", ">>", "::", "<eof>",
    "<number>", "<integer>", "<name>", "<string>"
};


void LexState::save (int c) {
  Mbuffer *b = buff;
  if (b->len() + 1 > b->size()) {
    size_t newsize;
    if (b->size() >= MAX_SIZE/2)
      error("lexical element too long", 0);
    newsize = b->size() * 2;
    b->resize(L, newsize);
  }
  b->add(cast(char, c));
}


void luaX_init (lua_State *L) {
  int i;
  TString *e = luaS_newliteral(L, LUA_ENV);  /* create env name */
  luaC_fix(L, obj2gco(e));  /* never collect this name */
  for (i=0; i<NUM_RESERVED; i++) {
    TString *ts = luaS_new(L, luaX_tokens[i]);
    luaC_fix(L, obj2gco(ts));  /* reserved words are never collected */
    ts->extra = cast_byte(i+1);  /* reserved word */
  }
}


const char *LexState::token2str (int token) {
  if (token < FIRST_RESERVED) {  /* single-byte symbols? */
    lua_assert(token == cast_uchar(token));
    return luaO_pushfstring(L, "'%c'", token);
  }
  else {
    const char *s = luaX_tokens[token - FIRST_RESERVED];
    if (token < TK_EOS)  /* fixed format (symbols and reserved words)? */
      return luaO_pushfstring(L, "'%s'", s);
    else  /* names, strings, and numerals */
      return s;
  }
}


const char *LexState::txtToken (int token) {
  switch (token) {
    case TK_NAME: case TK_STRING:
    case TK_FLT: case TK_INT:
      save('\0');
      return luaO_pushfstring(L, "'%s'", buff->buffer());
    default:
      return token2str(token);
  }
}


l_noret LexState::error (const char *msg, int token) {
  msg = luaG_addinfo(L, msg, source, linenumber);
  if (token)
    luaO_pushfstring(L, "%s near %s", msg, txtToken(token));
  luaD_throw(L, LUA_ERRSYNTAX);
}


l_noret LexState::syntaxerror (const char *msg) {
  error(msg, t.token);
}


/*
** creates a new string and anchors it in scanner's table so that
** it will not be collected until the end of the compilation
** (by that time it should be anchored somewhere)
*/
TString *LexState::newstring (const char *str, size_t l) {
  TValue *o;  /* entry for 'str' */
  TString *ts = luaS_newlstr(L, str, l);  /* create new string */
  setsvalue2s(L, L->top++, ts);  /* temporarily anchor it in stack */
  o = luaH_set(L, h, L->top - 1);
  if (ttisnil(o)) {  /* not in use yet? */
    /* boolean value does not need GC barrier;
       table has no metatable, so it does not need to invalidate cache */
    setbvalue(o, 1);  /* t[string] = true */
    luaC_checkGC(L);
  }
  else {  /* string already present */
    ts = tsvalue(keyfromval(o));  /* re-use value previously stored */
  }
  L->top--;  /* remove string from stack */
  return ts;
}


/*
** increment line number and skips newline sequence (any of
** \n, \r, \n\r, or \r\n)
*/
void LexState::inclinenumber (void) {
  int old = m_current;
  lua_assert(currIsNewline());
  next();  /* skip '\n' or '\r' */
  if (currIsNewline() && m_current != old)
    next();  /* skip '\n\r' or '\r\n' */
  if (++linenumber >= MAX_INT)
    error("chunk has too many lines", 0);
}


void LexState::setinput (lua_State *a_L, ZIO *a_z, TString *a_source,
                    int a_firstchar) {
  t.token = 0;
  m_decpoint = '.';
  L = a_L;
  m_current = a_firstchar;
  m_lookahead.token = TK_EOS;  /* no look-ahead token */
  z = a_z;
  fs = NULL;
  linenumber = 1;
  lastline = 1;
  source = a_source;
  envn = luaS_newliteral(L, LUA_ENV);  /* get env name */
  buff->resize(L, LUA_MINBUFFER);  /* initialize buffer */
}



/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/


int LexState::check_next1 (int c) {
  if (m_current == c) {
    next();
    return 1;
  }
  else return 0;
}


/*
** Check whether current char is in set 'set' (with two chars) and
** saves it
*/
int LexState::check_next2 (const char *set) {
  lua_assert(set[2] == '\0');
  if (m_current == set[0] || m_current == set[1]) {
    save_and_next();
    return 1;
  }
  else return 0;
}


/*
** change all characters 'from' in buffer to 'to'
*/
void LexState::buffreplace (char from, char to) {
  if (from != to) {
    size_t n = buff->len();
    char *p = buff->buffer();
    while (n--)
      if (p[n] == from) p[n] = to;
  }
}


#define buff2num(b,o)	(luaO_str2num(b->buffer(), o) != 0)

/*
** in case of format error, try to change decimal point separator to
** the one defined in the current locale and check again
*/
void LexState::trydecpoint (TValue *o) {
  char old = m_decpoint;
  m_decpoint = lua_getlocaledecpoint();
  buffreplace(old, m_decpoint);  /* try new decimal separator */
  if (!buff2num(buff, o)) {
    /* format error with correct decimal point: no more options */
    buffreplace(m_decpoint, '.');  /* undo change (for error message) */
    error("malformed number", TK_FLT);
  }
}


/* LUA_NUMBER */
/*
** this function is quite liberal in what it accepts, as 'luaO_str2num'
** will reject ill-formed numerals.
*/
int LexState::read_numeral (SemInfo *seminfo) {
  TValue obj;
  const char *expo = "Ee";
  int first = m_current;
  lua_assert(lisdigit(m_current));
  save_and_next();
  if (first == '0' && check_next2("xX"))  /* hexadecimal? */
    expo = "Pp";
  for (;;) {
    if (check_next2(expo))  /* exponent part? */
      check_next2("-+");  /* optional exponent sign */
    if (lisxdigit(m_current))
      save_and_next();
    else if (m_current == '.')
      save_and_next();
    else break;
  }
  save('\0');
  buffreplace('.', m_decpoint);  /* follow locale for decimal point */
  if (!buff2num(buff, &obj))  /* format error? */
    trydecpoint(&obj); /* try to update decimal point separator */
  if (ttisinteger(&obj)) {
    seminfo->i = ivalue(&obj);
    return TK_INT;
  }
  else {
    lua_assert(ttisfloat(&obj));
    seminfo->r = fltvalue(&obj);
    return TK_FLT;
  }
}


/*
** skip a sequence '[=*[' or ']=*]'; if sequence is wellformed, return
** its number of '='s; otherwise, return a negative number (-1 iff there
** are no '='s after initial bracket)
*/
int LexState::skip_sep (void) {
  int count = 0;
  int s = m_current;
  lua_assert(s == '[' || s == ']');
  save_and_next();
  while (m_current == '=') {
    save_and_next();
    count++;
  }
  return (m_current == s) ? count : (-count) - 1;
}


void LexState::read_long_string (SemInfo *seminfo, int sep) {
  int line = linenumber;  /* initial line (for error message) */
  save_and_next();  /* skip 2nd '[' */
  if (currIsNewline())  /* string starts with a newline? */
    inclinenumber();  /* skip it */
  for (;;) {
    switch (m_current) {
      case EOZ: {  /* error */
        const char *what = (seminfo ? "string" : "comment");
        const char *msg = luaO_pushfstring(L,
                     "unfinished long %s (starting at line %d)", what, line);
        error(msg, TK_EOS);
        break;  /* to avoid warnings */
      }
      case ']': {
        if (skip_sep() == sep) {
          save_and_next();  /* skip 2nd ']' */
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save('\n');
        inclinenumber();
        if (!seminfo) buff->reset();  /* avoid wasting space */
        break;
      }
      default: {
        if (seminfo) save_and_next();
        else next();
      }
    }
  } endloop:
  if (seminfo)
    seminfo->ts = newstring(buff->buffer() + (2 + sep),
                            buff->len() - 2*(2 + sep));
}


void LexState::esccheck (int c, const char *msg) {
  if (!c) {
    if (m_current != EOZ)
      save_and_next();  /* add current to buffer for error message */
    error(msg, TK_STRING);
  }
}


int LexState::gethexa (void) {
  save_and_next();
  esccheck (lisxdigit(m_current), "hexadecimal digit expected");
  return luaO_hexavalue(m_current);
}


int LexState::readhexaesc (void) {
  int r = gethexa();
  r = (r << 4) + gethexa();
  buff->remove(2);  /* remove saved chars from buffer */
  return r;
}


unsigned long LexState::readutf8esc (void) {
  unsigned long r;
  int i = 4;  /* chars to be removed: '\', 'u', '{', and first digit */
  save_and_next();  /* skip 'u' */
  esccheck(m_current == '{', "missing '{'");
  r = gethexa();  /* must have at least one digit */
  while ((save_and_next(), lisxdigit(m_current))) {
    i++;
    r = (r << 4) + luaO_hexavalue(m_current);
    esccheck(r <= 0x10FFFF, "UTF-8 value too large");
  }
  esccheck(m_current == '}', "missing '}'");
  next();  /* skip '}' */
  buff->remove(i);  /* remove saved chars from buffer */
  return r;
}


void LexState::utf8esc (void) {
  char buff[UTF8BUFFSZ];
  int n = luaO_utf8esc(buff, readutf8esc());
  for (; n > 0; n--)  /* add 'buff' to string */
    save(buff[UTF8BUFFSZ - n]);
}


int LexState::readdecesc (void) {
  int i;
  int r = 0;  /* result accumulator */
  for (i = 0; i < 3 && lisdigit(m_current); i++) {  /* read up to 3 digits */
    r = 10*r + m_current - '0';
    save_and_next();
  }
  esccheck(r <= UCHAR_MAX, "decimal escape too large");
  buff->remove(i);  /* remove read digits from buffer */
  return r;
}


void LexState::read_string (int del, SemInfo *seminfo) {
  save_and_next();  /* keep delimiter (for error messages) */
  while (m_current != del) {
    switch (m_current) {
      case EOZ:
        error("unfinished string", TK_EOS);
        break;  /* to avoid warnings */
      case '\n':
      case '\r':
        error("unfinished string", TK_STRING);
        break;  /* to avoid warnings */
      case '\\': {  /* escape sequences */
        int c;  /* final character to be saved */
        save_and_next();  /* keep '\\' for error messages */
        switch (m_current) {
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': c = readhexaesc(); goto read_save;
          case 'u': utf8esc();  goto no_save;
          case '\n': case '\r':
            inclinenumber(); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = m_current; goto read_save;
          case EOZ: goto no_save;  /* will raise an error next loop */
          case 'z': {  /* zap following span of spaces */
            buff->remove(1);  /* remove '\\' */
            next();  /* skip the 'z' */
            while (lisspace(m_current)) {
              if (currIsNewline()) inclinenumber();
              else next();
            }
            goto no_save;
          }
          default: {
            esccheck(lisdigit(m_current), "invalid escape sequence");
            c = readdecesc();  /* digital escape '\ddd' */
            goto only_save;
          }
        }
       read_save:
         next();
         /* go through */
       only_save:
         buff->remove(1);  /* remove '\\' */
         save(c);
         /* go through */
       no_save: break;
      }
      default:
        save_and_next();
    }
  }
  save_and_next();  /* skip delimiter */
  seminfo->ts = newstring(buff->buffer() + 1, buff->len() - 2);
}


int LexState::llex (SemInfo *seminfo) {
  buff->reset();
  for (;;) {
    switch (m_current) {
      case '\n': case '\r': {  /* line breaks */
        inclinenumber();
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {  /* spaces */
        next();
        break;
      }
      case '-': {  /* '-' or '--' (comment) */
        next();
        if (m_current != '-') return '-';
        /* else is a comment */
        next();
        if (m_current == '[') {  /* long comment? */
          int sep = skip_sep();
          buff->reset();  /* 'skip_sep' may dirty the buffer */
          if (sep >= 0) {
            read_long_string(NULL, sep);  /* skip long comment */
            buff->reset();  /* previous call may dirty the buff. */
            break;
          }
        }
        /* else short comment */
        while (!currIsNewline() && m_current != EOZ)
          next();  /* skip until end of line (or end of file) */
        break;
      }
      case '[': {  /* long string or simply '[' */
        int sep = skip_sep();
        if (sep >= 0) {
          read_long_string(seminfo, sep);
          return TK_STRING;
        }
        else if (sep != -1)  /* '[=...' missing second bracket */
          error("invalid long string delimiter", TK_STRING);
        return '[';
      }
      case '=': {
        next();
        if (check_next1('=')) return TK_EQ;
        else return '=';
      }
      case '<': {
        next();
        if (check_next1('=')) return TK_LE;
        else if (check_next1('<')) return TK_SHL;
        else return '<';
      }
      case '>': {
        next();
        if (check_next1('=')) return TK_GE;
        else if (check_next1('>')) return TK_SHR;
        else return '>';
      }
      case '/': {
        next();
        if (check_next1('/')) return TK_IDIV;
        else return '/';
      }
      case '~': {
        next();
        if (check_next1('=')) return TK_NE;
        else return '~';
      }
      case ':': {
        next();
        if (check_next1(':')) return TK_DBCOLON;
        else return ':';
      }
      case '"': case '\'': {  /* short literal strings */
        read_string(m_current, seminfo);
        return TK_STRING;
      }
      case '.': {  /* '.', '..', '...', or number */
        save_and_next();
        if (check_next1('.')) {
          if (check_next1('.'))
            return TK_DOTS;   /* '...' */
          else return TK_CONCAT;   /* '..' */
        }
        else if (!lisdigit(m_current)) return '.';
        else return read_numeral(seminfo);
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        return read_numeral(seminfo);
      }
      case EOZ: {
        return TK_EOS;
      }
      default: {
        if (lislalpha(m_current)) {  /* identifier or reserved word? */
          TString *ts;
          do {
            save_and_next();
          } while (lislalnum(m_current));
          ts = newstring(buff->buffer(), buff->len());
          seminfo->ts = ts;
          if (isreserved(ts))  /* reserved word? */
            return ts->extra - 1 + FIRST_RESERVED;
          else {
            return TK_NAME;
          }
        }
        else {  /* single-char tokens (+ - / ...) */
          int c = m_current;
          next();
          return c;
        }
      }
    }
  }
}


void LexState::nextt (void) {
  lastline = linenumber;
  if (m_lookahead.token != TK_EOS) {  /* is there a look-ahead token? */
    t = m_lookahead;  /* use this one */
    m_lookahead.token = TK_EOS;  /* and discharge it */
  }
  else
    t.token = llex(&t.seminfo);  /* read next token */
}


int LexState::lookahead (void) {
  lua_assert(m_lookahead.token == TK_EOS);
  m_lookahead.token = llex(&m_lookahead.seminfo);
  return m_lookahead.token;
}

