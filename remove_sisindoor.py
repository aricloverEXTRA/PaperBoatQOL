with open('src/port/enhancements/graphics/PostFx.cpp', 'rb') as f:
    content = f.read()

# Remove sIsIndoor checks - they're no longer needed
old1 = b"const bool on = CVarGetInteger(POSTFX_SUNSHADE_CVAR, 0) != 0 && !sIsIndoor;"
new1 = b"const bool on = CVarGetInteger(POSTFX_SUNSHADE_CVAR, 0) != 0;"

old2 = b"const bool on = CVarGetInteger(POSTFX_GODRAYS_CVAR, 0) != 0 && !sIsIndoor;"
new2 = b"const bool on = CVarGetInteger(POSTFX_GODRAYS_CVAR, 0) != 0;"

content = content.replace(old1, b"const bool on = CVarGetInteger(POSTFX_SUNSHADE_CVAR, 0) != 0;")
content = content.replace(old2, b"const bool on = CVarGetInteger(POSTFX_GODRAYS_CVAR, 0) != 0;")

with open('src/port/enhancements/graphics/PostFx.cpp', 'wb') as f:
    f.write(content)
print('Removed sIsIndoor references')