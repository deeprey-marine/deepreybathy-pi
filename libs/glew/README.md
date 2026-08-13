# GLEW (vendored subset)

The OpenGL Extension Wrangler Library — https://github.com/nigels-com/glew

This is a trimmed copy containing only what the DeepView build consumes:

    include/GL/glew.h
    include/GL/eglew.h
    include/GL/glxew.h
    include/GL/wglew.h
    src/glew.c

`src/glew.c` is compiled directly into the plugin and `include/` is added to the
include path; see `Plugin.cmake`. The upstream code generator, standalone build
system, documentation and the `glewinfo`/`visualinfo` utilities are not included.

Licensed under the Modified BSD, Mesa 3-D and Khronos licences — see
[LICENSE.txt](LICENSE.txt).
