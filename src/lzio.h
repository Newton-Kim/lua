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

typedef struct Zio ZIO;

#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : luaZ_fill(z))


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
};

LUAI_FUNC char *luaZ_openspace (lua_State *L, Mbuffer *buff, size_t n);
LUAI_FUNC void luaZ_init (lua_State *L, ZIO *z, lua_Reader reader,
                                        void *data);
LUAI_FUNC size_t luaZ_read (ZIO* z, void *b, size_t n);	/* read next n bytes */



/* --------- Private Part ------------------ */

struct Zio {
  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  lua_Reader reader;		/* reader function */
  void *data;			/* additional data */
  lua_State *L;			/* Lua state (for reader) */
};


LUAI_FUNC int luaZ_fill (ZIO *z);

#endif
