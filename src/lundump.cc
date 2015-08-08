/*
** $Id: lundump.c,v 2.41 2014/11/02 19:19:04 roberto Exp $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define lundump_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "lundump.h"
#include "lzio.h"


#if !defined(luai_verifycode)
#define luai_verifycode(L,b,f)  /* empty */
#endif


class LoadState {
 private:
  lua_State *m_L;
  ZIO *m_Z;
  Mbuffer *m_b;
  const char *m_name;

  l_noret error(const char *why);
  void LoadBlock (void *b, size_t size);
  lu_byte LoadByte (void);
  int LoadInt (void);
  lua_Number LoadNumber (void);
  lua_Integer LoadInteger (void);
  TString *LoadString (void);
  void LoadCode (Proto *f);
  void LoadConstants (Proto *f);
  void LoadProtos (Proto *f);
  void LoadUpvalues (Proto *f);
  void LoadDebug (Proto *f);
  void LoadFunction (Proto *f, TString *psource);
  void checkliteral (const char *s, const char *msg);
  void fchecksize (size_t size, const char *tname);
  void checkHeader (void);
 public:
  LoadState(lua_State *L, ZIO *Z, Mbuffer *buff, const char *name);
  LClosure *undump(void);
};

LoadState::LoadState(lua_State *L, ZIO *Z, Mbuffer *buff,
                     const char *name) {
  if (*name == '@' || *name == '=')
    m_name = name + 1;
  else if (*name == LUA_SIGNATURE[0])
    m_name = "binary string";
  else
    m_name = name;
  m_L = L;
  m_Z = Z;
  m_b = buff;
}

LClosure *LoadState::undump(void) {
  LClosure *cl;
  checkHeader();
  cl = luaF_newLclosure(m_L, LoadByte());
  setclLvalue(m_L, m_L->top, cl);
  incr_top(m_L);
  cl->p = luaF_newproto(m_L);
  LoadFunction(cl->p, NULL);
  lua_assert(cl->nupvalues == cl->p->sizeupvalues);
  luai_verifycode(m_L, m_buff, cl->p);
  return cl;
}

l_noret LoadState::error(const char *why) {
  luaO_pushfstring(m_L, "%s: %s precompiled chunk", m_name, why);
  luaD_throw(m_L, LUA_ERRSYNTAX);
}


/*
** All high-level loads go through LoadVector; you can change it to
** adapt to the endianness of the input
*/
#define LoadVector(b,n)	LoadBlock(b,(n)*sizeof((b)[0]))

void LoadState::LoadBlock (void *b, size_t size) {
  if (m_Z->read(b, size) != 0)
    error("truncated");
}


#define LoadVar(x)		LoadVector(&x,1)


lu_byte LoadState::LoadByte (void) {
  lu_byte x;
  LoadVar(x);
  return x;
}


int LoadState::LoadInt (void) {
  int x;
  LoadVar(x);
  return x;
}


lua_Number LoadState::LoadNumber (void) {
  lua_Number x;
  LoadVar(x);
  return x;
}


lua_Integer LoadState::LoadInteger (void) {
  lua_Integer x;
  LoadVar(x);
  return x;
}


TString *LoadState::LoadString (void) {
  size_t size = LoadByte();
  if (size == 0xFF)
    LoadVar(size);
  if (size == 0)
    return NULL;
  else {
    char *s = m_b->openspace(m_L, --size);
    LoadVector(s, size);
    return luaS_newlstr(m_L, s, size);
  }
}


void LoadState::LoadCode (Proto *f) {
  int n = LoadInt();
  f->code = luaM_newvector(m_L, n, Instruction);
  f->sizecode = n;
  LoadVector(f->code, n);
}


void LoadState::LoadConstants (Proto *f) {
  int i;
  int n = LoadInt();
  f->k = luaM_newvector(m_L, n, TValue);
  f->sizek = n;
  for (i = 0; i < n; i++)
    setnilvalue(&f->k[i]);
  for (i = 0; i < n; i++) {
    TValue *o = &f->k[i];
    int t = LoadByte();
    switch (t) {
    case LUA_TNIL:
      setnilvalue(o);
      break;
    case LUA_TBOOLEAN:
      setbvalue(o, LoadByte());
      break;
    case LUA_TNUMFLT:
      setfltvalue(o, LoadNumber());
      break;
    case LUA_TNUMINT:
      setivalue(o, LoadInteger());
      break;
    case LUA_TSHRSTR:
    case LUA_TLNGSTR:
      setsvalue2n(m_L, o, LoadString());
      break;
    default:
      lua_assert(0);
    }
  }
}


void LoadState::LoadProtos (Proto *f) {
  int i;
  int n = LoadInt();
  f->p = luaM_newvector(m_L, n, Proto *);
  f->sizep = n;
  for (i = 0; i < n; i++)
    f->p[i] = NULL;
  for (i = 0; i < n; i++) {
    f->p[i] = luaF_newproto(m_L);
    LoadFunction(f->p[i], f->source);
  }
}


void LoadState::LoadUpvalues (Proto *f) {
  int i, n;
  n = LoadInt();
  f->upvalues = luaM_newvector(m_L, n, Upvaldesc);
  f->sizeupvalues = n;
  for (i = 0; i < n; i++)
    f->upvalues[i].name = NULL;
  for (i = 0; i < n; i++) {
    f->upvalues[i].instack = LoadByte();
    f->upvalues[i].idx = LoadByte();
  }
}


void LoadState::LoadDebug (Proto *f) {
  int i, n;
  n = LoadInt();
  f->lineinfo = luaM_newvector(m_L, n, int);
  f->sizelineinfo = n;
  LoadVector(f->lineinfo, n);
  n = LoadInt();
  f->locvars = luaM_newvector(m_L, n, LocVar);
  f->sizelocvars = n;
  for (i = 0; i < n; i++)
    f->locvars[i].varname = NULL;
  for (i = 0; i < n; i++) {
    f->locvars[i].varname = LoadString();
    f->locvars[i].startpc = LoadInt();
    f->locvars[i].endpc = LoadInt();
  }
  n = LoadInt();
  for (i = 0; i < n; i++)
    f->upvalues[i].name = LoadString();
}


void LoadState::LoadFunction (Proto *f, TString *psource) {
  f->source = LoadString();
  if (f->source == NULL)  /* no source in dump? */
    f->source = psource;  /* reuse parent's source */
  f->linedefined = LoadInt();
  f->lastlinedefined = LoadInt();
  f->numparams = LoadByte();
  f->is_vararg = LoadByte();
  f->maxstacksize = LoadByte();
  LoadCode(f);
  LoadConstants(f);
  LoadUpvalues(f);
  LoadProtos(f);
  LoadDebug(f);
}


void LoadState::checkliteral (const char *s, const char *msg) {
  char buff[sizeof(LUA_SIGNATURE) + sizeof(LUAC_DATA)]; /* larger than both */
  size_t len = strlen(s);
  LoadVector(buff, len);
  if (memcmp(s, buff, len) != 0)
    error(msg);
}


void LoadState::fchecksize (size_t size, const char *tname) {
  if (LoadByte() != size)
    error(luaO_pushfstring(m_L, "%s size mismatch in", tname));
}


#define checksize(t)	fchecksize(sizeof(t),#t)

void LoadState::checkHeader (void) {
  checkliteral(LUA_SIGNATURE + 1, "not a");  /* 1st char already checked */
  if (LoadByte() != LUAC_VERSION)
    error("version mismatch in");
  if (LoadByte() != LUAC_FORMAT)
    error("format mismatch in");
  checkliteral(LUAC_DATA, "corrupted");
  checksize(int);
  checksize(size_t);
  checksize(Instruction);
  checksize(lua_Integer);
  checksize(lua_Number);
  if (LoadInteger() != LUAC_INT)
    error("endianness mismatch in");
  if (LoadNumber() != LUAC_NUM)
    error("float format mismatch in");
}


/*
** load precompiled chunk
*/
LClosure *luaU_undump(lua_State *L, ZIO *Z, Mbuffer *buff,
                      const char *name) {
  LoadState S(L, Z, buff, name);
  return S.undump();
}

