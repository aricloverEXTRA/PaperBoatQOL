with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()

idx = content.find('extern "C" bool IsStableFrame()')
if idx >= 0:
    print(content[idx:idx+300])