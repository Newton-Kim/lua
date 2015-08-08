/*
** $Id: lzio.h,v 1.30 2014/12/19 17:26:14 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "lua.h"

#include "lmem.h"


#define EOZ	(-1)			/* end of stream */


class Mbuffer {
 private:
  char *m_buffer;
  size_t m_n;
  size_t m_buffsize;
 public:
  inline void init(void) {m_buffer = NULL; m_buffsize = 0;}
  inline char *buffer(void) {return m_buffer;}
  inline size_t size(void) {return m_buffsize;}
  inline size_t len(void) {return m_n;}
  inline void remove(size_t i) {m_n -= i;}
  inline void reset(void) {m_n = 0;}
  inline void resize(lua_State* L, size_t size) {
	m_buffer = luaM_reallocvchar(L, m_buffer, m_buffsize, size);
	m_buffsize = size;
  }
  inline void add (char c) {m_buffer[m_n++] = c;}
  inline void free(lua_State* L) {resize(L, 0);}
  inline char *openspace(lua_State *L, size_t n) {
    if (n > size()) {
      if (n < LUA_MINBUFFER) n = LUA_MINBUFFER;
      resize(L, n);
    }
    return m_buffer;
  }
};


/* --------- Private Part ------------------ */

class ZIO {
 private:
  size_t m_n;			/* bytes still unread */
  const char *m_p;		/* current position in buffer */
  lua_Reader m_reader;		/* reader function */
  void *m_data;			/* additional data */
  lua_State *m_L;		/* Lua state (for reader) */
 public:
  ZIO(lua_State *L, lua_Reader reader, void *data);
  size_t read(void *b, size_t n);
  int fill(void);
  inline int getc(void) {return (m_n--)>0 ?  cast_uchar(*m_p++) : fill();}
};

#endif
