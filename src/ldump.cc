/*
** $Id: ldump.c,v 2.36 2015/03/30 15:43:51 roberto Exp $
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define ldump_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"


class DumpState {
 private:
  lua_State *m_L;
  lua_Writer m_writer;
  void *m_data;
  int m_strip;
  int m_status;

  void DumpBlock (const void *b, size_t size);
  void DumpInt (int x);
  void DumpNumber (lua_Number x);
  void DumpInteger (lua_Integer x);
  void DumpString (const TString *s);
  void DumpCode (const Proto *f);
  void DumpConstants (const Proto *f);
  void DumpProtos (const Proto *f);
  void DumpUpvalues (const Proto *f);
  void DumpDebug (const Proto *f);
 public:
  DumpState(lua_State *L, lua_Writer w, void *data, int strip);
  void DumpHeader (void);
  void DumpByte (int y);
  void DumpFunction (const Proto *f, TString *psource);
  inline int status(void) {return m_status;}
};

DumpState::DumpState(lua_State *L, lua_Writer w, void *data,
                     int strip){
  m_L = L;
  m_writer = w;
  m_data = data;
  m_strip = strip;
  m_status = 0;
}

/*
** All high-level dumps go through DumpVector; you can change it to
** change the endianness of the result
*/
#define DumpVector(v,n)	DumpBlock(v,(n)*sizeof((v)[0]))

#define DumpLiteral(s)	DumpBlock(s, sizeof(s) - sizeof(char))


void DumpState::DumpBlock (const void *b, size_t size) {
  if (m_status == 0) {
    lua_unlock(m_L);
    m_status = (*m_writer)(m_L, b, size, m_data);
    lua_lock(m_L);
  }
}


#define DumpVar(x)		DumpVector(&x,1)


void DumpState::DumpByte (int y) {
  lu_byte x = (lu_byte)y;
  DumpVar(x);
}


void DumpState::DumpInt (int x) {
  DumpVar(x);
}


void DumpState::DumpNumber (lua_Number x) {
  DumpVar(x);
}


void DumpState::DumpInteger (lua_Integer x) {
  DumpVar(x);
}


void DumpState::DumpString (const TString *s) {
  if (s == NULL)
    DumpByte(0);
  else {
    size_t size = tsslen(s) + 1;  /* include trailing '\0' */
    const char *str = getstr(s);
    if (size < 0xFF)
      DumpByte(cast_int(size));
    else {
      DumpByte(0xFF);
      DumpVar(size);
    }
    DumpVector(str, size - 1);  /* no need to save '\0' */
  }
}


void DumpState::DumpCode (const Proto *f) {
  DumpInt(f->sizecode);
  DumpVector(f->code, f->sizecode);
}


void DumpState::DumpConstants (const Proto *f) {
  int i;
  int n = f->sizek;
  DumpInt(n);
  for (i = 0; i < n; i++) {
    const TValue *o = &f->k[i];
    DumpByte(ttype(o));
    switch (ttype(o)) {
    case LUA_TNIL:
      break;
    case LUA_TBOOLEAN:
      DumpByte(bvalue(o));
      break;
    case LUA_TNUMFLT:
      DumpNumber(fltvalue(o));
      break;
    case LUA_TNUMINT:
      DumpInteger(ivalue(o));
      break;
    case LUA_TSHRSTR:
    case LUA_TLNGSTR:
      DumpString(tsvalue(o));
      break;
    default:
      lua_assert(0);
    }
  }
}


void DumpState::DumpProtos (const Proto *f) {
  int i;
  int n = f->sizep;
  DumpInt(n);
  for (i = 0; i < n; i++)
    DumpFunction(f->p[i], f->source);
}


void DumpState::DumpUpvalues (const Proto *f) {
  int i, n = f->sizeupvalues;
  DumpInt(n);
  for (i = 0; i < n; i++) {
    DumpByte(f->upvalues[i].instack);
    DumpByte(f->upvalues[i].idx);
  }
}


void DumpState::DumpDebug (const Proto *f) {
  int i, n;
  n = (m_strip) ? 0 : f->sizelineinfo;
  DumpInt(n);
  DumpVector(f->lineinfo, n);
  n = (m_strip) ? 0 : f->sizelocvars;
  DumpInt(n);
  for (i = 0; i < n; i++) {
    DumpString(f->locvars[i].varname);
    DumpInt(f->locvars[i].startpc);
    DumpInt(f->locvars[i].endpc);
  }
  n = (m_strip) ? 0 : f->sizeupvalues;
  DumpInt(n);
  for (i = 0; i < n; i++)
    DumpString(f->upvalues[i].name);
}


void DumpState::DumpFunction (const Proto *f, TString *psource) {
  if (m_strip || f->source == psource)
    DumpString(NULL);  /* no debug info or same source as its parent */
  else
    DumpString(f->source);
  DumpInt(f->linedefined);
  DumpInt(f->lastlinedefined);
  DumpByte(f->numparams);
  DumpByte(f->is_vararg);
  DumpByte(f->maxstacksize);
  DumpCode(f);
  DumpConstants(f);
  DumpUpvalues(f);
  DumpProtos(f);
  DumpDebug(f);
}


void DumpState::DumpHeader (void) {
  DumpLiteral(LUA_SIGNATURE);
  DumpByte(LUAC_VERSION);
  DumpByte(LUAC_FORMAT);
  DumpLiteral(LUAC_DATA);
  DumpByte(sizeof(int));
  DumpByte(sizeof(size_t));
  DumpByte(sizeof(Instruction));
  DumpByte(sizeof(lua_Integer));
  DumpByte(sizeof(lua_Number));
  DumpInteger(LUAC_INT);
  DumpNumber(LUAC_NUM);
}


/*
** dump Lua function as precompiled chunk
*/
int luaU_dump(lua_State *L, const Proto *f, lua_Writer w, void *data,
              int strip) {
  DumpState D(L, w, data, strip);
  D.DumpHeader();
  D.DumpByte(f->sizeupvalues);
  D.DumpFunction(f, NULL);
  return D.status();
}

