with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()
idx = content.find('extern "C" AreaType DetectAreaType')
if idx >= 0:
    print(repr(content[idx:idx+500]))