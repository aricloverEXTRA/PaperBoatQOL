import re

with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()

# Remove static from pass ID definitions
pattern = r'static int (s\w+PassId)'
content = re.sub(pattern, r'int \1', content)

with open('src/port/enhancements/graphics/PostFx.cpp', 'w') as f:
    f.write(content)

print('Removed static from pass ID definitions')