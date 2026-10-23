with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()

# Find the IsStableFrame function and add a helper after it
idx = content.find('extern "C" bool IsStableFrame() {')
if idx >= 0:
    # Find the end of the function
    brace_count = 0
    idx2 = idx
    while idx2 < len(content):
        if content[idx2] == '{':
            brace_count += 1
        elif content[idx2] == '}':
            brace_count -= 1
            if brace_count == 0:
                break
        idx2 += 1
    
    # Insert after the function
    insert_pos = idx2 + 1
    new_func = '''

// Check if we are running on DirectX 11 backend (Windows only)
extern "C" bool PostFx_IsDirectX11() {
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return false;
    }
    // Get the window backend from the interpreter's window
    // Fast3dWindow backend IDs: FAST3D_DXGI_DX11 = 1
    return interp->GetWindowBackend() == 1;  // FAST3D_DXGI_DX11
}

'''
    new_content = content[:insert_pos] + new_func + content[insert_pos:]
    with open('src/port/enhancements/graphics/PostFx.cpp', 'w') as f:
        f.write(new_content)
    print('Added PostFx_IsDirectX11 helper')
else:
    print('Could not find IsStableFrame')