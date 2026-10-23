import os

for root, dirs, files in os.walk('src/port'):
    for f in files:
        if f.endswith('.h') or f.endswith('.hpp'):
            p = os.path.join(root, f)
            try:
                with open(p, 'r') as f:
                    content = f.read()
                    if 'gfx_set_camera_view_projection' in content and '{' in content[content.find('gfx_set_camera_view_projection'):content.find('gfx_set_camera_view_projection')+100]:
                        print(p)
            except:
                pass