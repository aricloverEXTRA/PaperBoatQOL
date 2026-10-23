with open('src/port/enhancements/graphics/PostFx.cpp', 'rb') as f:
    content = f.read()

old = b"Auto-detect area type - track internally, DON'T modify CVars (avoids pass re-reg storm)\r\n    UpdateAreaType();\r\n    sIsIndoor = (sCurrentAreaType == AREA_INDOOR || sCurrentAreaType == AREA_UNDERGROUND)"

new = b"Area type detection removed - use CVars directly for sun-dependent effects"

if old in content:
    content = content.replace(old, new)
    with open('src/port/enhancements/graphics/PostFx.cpp', 'wb') as f:
        f.write(content)
    print('Replaced UpdateAreaType call')
else:
    print('Could not find the code to replace')