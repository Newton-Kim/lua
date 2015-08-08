/*
** $Id: lzio.c,v 1.36 2014/11/02 19:19:04 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/

#define lzio_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "llimits.h"
#include "lmem.h"
#include "lstate.h"
#include "lzio.h"


int luaZ_fill (ZIO *z) {
  return z->fill();
}

int ZIO::fill (void) {
  size_t size;
  const char *buff;
  lua_unlock(m_L);
  buff = m_reader(m_L, m_data, &size);
  lua_lock(m_L);
  if (buff == NULL || size == 0)
    return EOZ;
  m_n = size - 1;  /* discount char being returned */
  m_p = buff;
  return cast_uchar(*(m_p++));
}


void luaZ_init (lua_State *L, ZIO *z, lua_Reader reader, void *data) {
  z->init(L, reader, data);
}
void ZIO::init (lua_State *L, lua_Reader reader, void *data) {
  m_L = L;
  m_reader = reader;
  m_data = data;
  m_n = 0;
  m_p = NULL;
}


/* --------------------------------------------------------------- read --- */
size_t luaZ_read (ZIO *z, void *b, size_t n) {
  return z->read(b, n);
}

size_t ZIO::read (void *b, size_t n) {
  while (n) {
    size_t m;
    if (m_n == 0) {  /* no bytes in buffer? */
      if (fill() == EOZ)  /* try to read more */
        return n;  /* no more input; return number of missing bytes */
      else {
        m_n++;  /* luaZ_fill consumed first byte; put it back */
        m_p--;
      }
    }
    m = (n <= m_n) ? n : m_n;  /* min. between n and z->n */
    memcpy(b, m_p, m);
    m_n -= m;
    m_p += m;
    b = (char *)b + m;
    n -= m;
  }
  return 0;
}

/* ------------------------------------------------------------------------ */
char *luaZ_openspace (lua_State *L, Mbuffer *buff, size_t n) {
  if (n > buff->size()) {
    if (n < LUA_MINBUFFER) n = LUA_MINBUFFER;
    buff->resize(L, n);
  }
  return buff->buffer();
}


