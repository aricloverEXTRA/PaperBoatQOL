import re

with open('src/port/Engine.cpp', 'r') as f:
    content = f.read()

includes = re.findall(r'#include\s*[<"]([^">]+)[>"]', content)
for inc in includes:
    print(inc)