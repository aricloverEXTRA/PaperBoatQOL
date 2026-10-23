with open('src/port/enhancements/graphics/PostFx.cpp', 'rb') as f:
    content = f.read()

old = b"""            # Modern shader suite params (even on unstable frames for continuity)\r\n            PostFx_PushPbr();\r\n            PostFx_PushLut();\r\n            PostFx_PushHdr();\r\n            PostFx_PushVolumetric();\r\n            PostFx_PushTaa();\r\n            PostFx_PushMotionBlur();\r\n            PostFx_PushParallax();\r\n            PostFx_PushVrs();"""

new = b"""            # Modern shader suite params (even on unstable frames for continuity)\r\n            if (PostFx_IsDirectX11()) {\r\n                PostFx_PushPbr();\r\n                PostFx_PushLut();\r\n                PostFx_PushHdr();\r\n                PostFx_PushVolumetric();\r\n                PostFx_PushTaa();\r\n                PostFx_PushMotionBlur();\r\n                PostFx_PushParallax();\r\n                PostFx_PushVrs();\r\n            }"""

if old in content:
    content = content.replace(old, new)
    with open('src/port/enhancements/graphics/PostFx.cpp', 'wb') as f:
        f.write(content)
    print('Updated unstable frame handling')
else:
    print('Could not find unstable frame handling')

# Also update the stable frame handling
old2 = b"""    PostFx_PushPbr();\r\n            PostFx_PushLut();\r\n            PostFx_PushHdr();\r\n            PostFx_PushVolumetric();\r\n            PostFx_PushTaa();\r\n            PostFx_PushMotionBlur();\r\n            PostFx_PushParallax();\r\n            PostFx_PushVrs();"""

new2 = b"""    if (PostFx_IsDirectX11()) {\r\n                PostFx_PushPbr();\r\n                PostFx_PushLut();\r\n                PostFx_PushHdr();\r\n                PostFx_PushVolumetric();\r\n                PostFx_PushTaa();\r\n                PostFx_PushMotionBlur();\r\n                PostFx_PushParallax();\r\n                PostFx_PushVrs();\r\n            }"""

if old2 in content:
    content = content.replace(old2, new2)
    with open('src/port/enhancements/graphics/PostFx.cpp', 'wb') as f:
        f.write(content)
    print('Updated stable frame handling')
else:
    print('Could not find stable frame handling')