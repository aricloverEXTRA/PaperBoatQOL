with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()

old = '''            # Modern shader suite params (even on unstable frames for continuity)
            PostFx_PushPbr();
            PostFx_PushLut();
            PostFx_PushHdr();
            PostFx_PushVolumetric();
            PostFx_PushTaa();
            PostFx_PushMotionBlur();
            PostFx_PushParallax();'''

new = '''            # Modern shader suite params (even on unstable frames for continuity)
            if (PostFx_IsDirectX11()) {
                PostFx_PushPbr();
                PostFx_PushLut();
                PostFx_PushHdr();
                PostFx_PushVolumetric();
                PostFx_PushTaa();
                PostFx_PushMotionBlur();
                PostFx_PushParallax();
            }'''

if old in content:
    content = content.replace(old, new)
    with open('src/port/enhancements/graphics/PostFx.cpp', 'w') as f:
        f.write(content)
    print('Updated unstable frame handling')
else:
    print('Could not find unstable frame handling')

# Also update the stable frame handling
old2 = '''    PostFx_PushPbr();
    PostFx_PushLut();
    PostFx_PushHdr();
    PostFx_PushVolumetric();
    PostFx_PushTaa();
    PostFx_PushMotionBlur();
    PostFx_PushParallax();'''

new2 = '''    if (PostFx_IsDirectX11()) {
        PostFx_PushPbr();
        PostFx_PushLut();
        PostFx_PushHdr();
        PostFx_PushVolumetric();
        PostFx_PushTaa();
        PostFx_PushMotionBlur();
        PostFx_PushParallax();
    }'''

if old2 in content:
    content = content.replace(old2, new2)
    with open('src/port/enhancements/graphics/PostFx.cpp', 'w') as f:
        f.write(content)
    print('Updated stable frame handling')
else:
    print('Could not find stable frame handling')