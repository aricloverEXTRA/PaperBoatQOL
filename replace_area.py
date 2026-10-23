with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()

old = """    # Auto-detect area type - track internally, DON'T modify CVars (avoids pass re-reg storm)
    UpdateAreaType()
    sIsIndoor = (sCurrentAreaType == AREA_INDOOR || sCurrentAreaType == AREA_UNDERGROUND)"""

new = """    # Area type detection removed - use CVars directly for sun-dependent effects"""

if old in content:
    content = content.replace(old, new)
    with open('src/port/enhancements/graphics/PostFx.cpp', 'w') as f:
        f.write(content)
    print('Replaced UpdateAreaType call')
else:
    print('Could not find the code to replace')