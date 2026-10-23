with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()

# Find the start and end of the functions to remove
start = content.find('extern "C" AreaType DetectAreaType(const char* areaName) {')
end = content.find('// Stability helpers ------------------------------------------------------------')

if start >= 0 and end >= 0:
    # Remove everything from start to end
    new_content = content[:start] + content[end:]
    with open('src/port/enhancements/graphics/PostFx.cpp', 'w') as f:
        f.write(new_content)
    print("Removed DetectAreaType and UpdateAreaType functions")
else:
    print("Could not find the functions to remove")
    print("start:", content.find('extern "C" AreaType DetectAreaType'))
    print("end:", content.find('// Stability helpers ------------------------------------------------------------'))