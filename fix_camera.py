with open('src/port/Engine.cpp', 'r') as f:
    content = f.read()

# Find the start of the camera functions to remove
start = content.find('static float sCameraViewProj[4][4] = {{0}};')
if start >= 0:
    # Find the end of the last camera function
    end_idx = content.find('extern "C" void gfx_set_camera_world_position')
    if end_idx >= 0:
        brace_count = 0
        for i in range(end_idx, len(content)):
            if content[i] == '{':
                brace_count += 1
            elif content[i] == '}':
                brace_count -= 1
                if brace_count == 0:
                    new_content = content[:content.find('static float sCameraViewProj[4][4] = {{0}};')] + content[i+1:]
                    with open('src/port/Engine.cpp', 'w') as f:
                        f.write(new_content)
                    print('Removed camera functions')
                    break