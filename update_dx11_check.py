with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()

# Find the PostFx_IsDirectX11 function and update it
old = '''// Check if we are running on DirectX 11 backend (Windows only)
extern "C" bool PostFx_IsDirectX11() {
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return false;
    }
    // Get the window backend from the interpreter's window
    // Fast3dWindow backend IDs: FAST3D_DXGI_DX11 = 1
    return interp->GetWindowBackend() == 1;  // FAST3D_DXGI_DX11
}'''

new = '''// Check if we are running on DirectX 11 backend (Windows only)
extern "C" bool PostFx_IsDirectX11() {
    // Get the window from the global context
    auto ctx = Ship::Context::GetRawInstance();
    if (ctx == nullptr) {
        return false;
    }
    auto window = ctx->GetWindow();
    if (!window) {
        return false;
    }
    // Fast3dWindow backend IDs: FAST3D_DXGI_DX11 = 1
    return window->GetWindowBackend() == 1;  // FAST3D_DXGI_DX11
}'''

if old in content:
    content = content.replace(old, new)
    with open('src/port/enhancements/graphics/PostFx.cpp', 'w') as f:
        f.write(content)
    print('Updated PostFx_IsDirectX11 helper')
else:
    print('Could not find the old function')