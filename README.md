# lua-ffi-gen

A Clang plugin for generating declarations for the LuaJIT FFI library.

Basic usage information

Mark the declarations of functions, structures/unions or enums in C code that you want to use inside Lua code. Build the C code with Clang and run the implemented plugin. The plugin will generate bindings for marked declarations in an output Lua file. It will find all declarations of types that are needed for using marked declarations in Lua code.

Build instructions and usage details can be found in the wiki section.
