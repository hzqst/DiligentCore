We have deserialized shaders from `InlineConstants, RenderStateCache` -> `CreatePSOFromCache`, when non-seperate program enabled:

```glsl
#version 440 core
#define DESKTOP_GL 1
#define PLATFORM_WIN32 1
#define VERTEX_SHADER 1
layout(std140) uniform;


layout(binding = 0, std140) uniform cbInlinePositions
{
    vec4 g_Positions[6];
} _57;

layout(binding = 1, std140) uniform cbInlineColors
{
    vec4 g_Colors[3];
} _67;

layout(location = 0) out vec3 _PSIn_Color;

void main()
{
    uint _92 = uint(gl_VertexID);
    gl_Position = _57.g_Positions[_92];
    _PSIn_Color = _67.g_Colors[_92 % 3u].xyz;
}

/*$SHADER_SOURCE_LANGUAGE=1*/
```

```glsl
#version 440 core
#define DESKTOP_GL 1
#define PLATFORM_WIN32 1
#define FRAGMENT_SHADER 1
#define PIXEL_SHADER 1
layout(std140) uniform;


layout(binding = 0, std140) uniform cbInlineColors
{
    vec4 g_Colors[3];
} _53;

layout(location = 0) in vec3 _PSIn_Color;
layout(location = 0) out vec4 _psout_main;

void main()
{
    _psout_main = vec4(_PSIn_Color.x * _53.g_Colors[0].w, _PSIn_Color.y * _53.g_Colors[1].w, _PSIn_Color.z * _53.g_Colors[2].w, 1.0);
}

/*$SHADER_SOURCE_LANGUAGE=1*/
```

The VS -> `layout(binding = 1, std140) uniform cbInlineColors` and FS -> `layout(binding = 0, std140) uniform cbInlineColors` are using different binding qualifiers, and causing link error. 

We should stop emitting `binding = X` in `Archiver_GL -> TransformSource` and let OpenGL driver do the binding point allocation:

```cpp
#    if PLATFORM_APPLE
        // Apple does not support GL_ARB_shading_language_420pack extension
        Options.enable_420pack_extension = false;
#    endif

        if (!Options.separate_shader_objects)
        {
            // We do not emit binding qualifiers when separate_shader_objects disable.
            // Let OpenGL driver do the binding qualifiers allocation.
            Options.version                  = 410;
            Options.enable_420pack_extension = false;
        }

```
