#ifndef LJKIWI_LUACOMPAT_H_
#define LJKIWI_LUACOMPAT_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 501 && defined(__GNUC__)
   #define LJKIWI_LJ_COMPAT_ATTR __attribute__((weak, visibility("default")))
#else
   #define LJKIWI_LJ_COMPAT_ATTR static
#endif

#if !defined(LUA_VERSION_NUM) || LUA_VERSION_NUM == 501

   #define LUA_OPADD 0
   #define LUA_OPSUB 1
   #define LUA_OPMUL 2
   #define LUA_OPDIV 3
   #define LUA_OPMOD 4
   #define LUA_OPPOW 5
   #define LUA_OPUNM 6

static int lua_absindex(lua_State* L, int i) {
   if (i < 0 && i > LUA_REGISTRYINDEX)
      i += lua_gettop(L) + 1;
   return i;
}

LJKIWI_LJ_COMPAT_ATTR lua_Number lua_tonumberx(lua_State* L, int i, int* isnum) {
   lua_Number n = lua_tonumber(L, i);
   if (isnum != NULL) {
      *isnum = (n != 0 || lua_isnumber(L, i));
   }
   return n;
}

LJKIWI_LJ_COMPAT_ATTR lua_Integer lua_tointegerx(lua_State* L, int i, int* isnum) {
   int ok = 0;
   lua_Number n = lua_tonumberx(L, i, &ok);
   if (ok) {
      if (n == (lua_Integer)n) {
         if (isnum)
            *isnum = 1;
         return (lua_Integer)n;
      }
   }
   if (isnum)
      *isnum = 0;
   return 0;
}

static const char* luaL_tolstring(lua_State* L, int idx, size_t* len) {
   if (!luaL_callmeta(L, idx, "__tostring")) {
      int t = lua_type(L, idx), tt = 0;
      char const* name = NULL;
      switch (t) {
         case LUA_TNIL:
            lua_pushliteral(L, "nil");
            break;
         case LUA_TSTRING:
         case LUA_TNUMBER:
            lua_pushvalue(L, idx);
            break;
         case LUA_TBOOLEAN:
            if (lua_toboolean(L, idx))
               lua_pushliteral(L, "true");
            else
               lua_pushliteral(L, "false");
            break;
         default:
            tt = luaL_getmetafield(L, idx, "__name");
            name = (tt == LUA_TSTRING) ? lua_tostring(L, -1) : lua_typename(L, t);
            lua_pushfstring(L, "%s: %p", name, lua_topointer(L, idx));
            if (tt != LUA_TNIL)
               lua_replace(L, -2);
            break;
      }
   } else {
      if (!lua_isstring(L, -1))
         luaL_error(L, "'__tostring' must return a string");
   }
   return lua_tolstring(L, -1, len);
}

#endif /* LUA_VERSION_NUM == 501 */

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM <= 502

static void compat_reverse(lua_State* L, int a, int b) {
   for (; a < b; ++a, --b) {
      lua_pushvalue(L, a);
      lua_pushvalue(L, b);
      lua_replace(L, a);
      lua_replace(L, b);
   }
}

static void lua_rotate(lua_State* L, int idx, int n) {
   int n_elems = 0;
   idx = lua_absindex(L, idx);
   n_elems = lua_gettop(L) - idx + 1;
   if (n < 0)
      n += n_elems;
   if (n > 0 && n < n_elems) {
      luaL_checkstack(L, 2, "not enough stack slots available");
      n = n_elems - n;
      compat_reverse(L, idx, idx + n - 1);
      compat_reverse(L, idx + n, idx + n_elems - 1);
      compat_reverse(L, idx, idx + n_elems - 1);
   }
}

static int lua_geti(lua_State* L, int index, lua_Integer i) {
   index = lua_absindex(L, index);
   lua_pushinteger(L, i);
   lua_gettable(L, index);
   return lua_type(L, -1);
}

#endif /* LUA_VERSION_NUM <= 502 */

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM <= 503
static int luaL_typeerror(lua_State* L, int arg, const char* tname) {
   const char* msg;
   const char* typearg; /* name for the type of the actual argument */
   if (luaL_getmetafield(L, arg, "__name") == LUA_TSTRING)
      typearg = lua_tostring(L, -1); /* use the given type name */
   else if (lua_type(L, arg) == LUA_TLIGHTUSERDATA)
      typearg = "light userdata"; /* special name for messages */
   else
      typearg = luaL_typename(L, arg); /* standard name */
   msg = lua_pushfstring(L, "%s expected, got %s", tname, typearg);
   return luaL_argerror(L, arg, msg);
}

#endif /* LUA_VERSION_NUM <= 503 */

#if !defined(luaL_newlibtable)
   #define luaL_newlibtable(L, l) lua_createtable(L, 0, sizeof(l) / sizeof((l)[0]) - 1)
#endif

#if !defined(luaL_checkversion)
   #define luaL_checkversion(L) ((void)0)
#endif

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // LJKIWI_LUACOMPAT_H_
