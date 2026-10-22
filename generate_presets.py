#!/usr/bin/env python3
"""
Generate comprehensive area lighting presets for Paper Mario 64.
Run this script to generate src/area_lighting_presets.c
"""

import os
import re

# Area categories and their default lighting types
AREA_LIGHTING_TYPES = {
    # Outdoor sunny areas
    'kmr': 'SUNNY_OUTDOOR',      # Goomba Village/Road
    'pra': 'SUNNY_OUTDOOR',      # Toad Town
    'gv': 'SUNNY_OUTDOOR',       # Goomba Village
    'sbk': 'SUNNY_OUTDOOR',      # Shiver City (outdoor parts)
    
    # Forest areas
    'arn': 'FOREST',             # Flower Fields
    'flo': 'FOREST',             # Flower Fields
    'mac': 'FOREST',             # Lavalava Island (jungle parts)
    'mim': 'FOREST',             # Jade Jungle
    
    # Desert areas
    'dro': 'DESERT',             # Dry Dry Desert
    'nok': 'DESERT',             # Dry Dry Outpost
    
    # Snow/Ice areas
    'sbk': 'SNOW',               # Shiver City
    'kgr': 'SNOW',               # Crystal Palace
    'mac': 'SNOW',               # Starborn Valley (snow parts)
    
    # Underground/Cave areas
    'dgb': 'CAVE',               # Tubba Blubba's Manor
    'iwa': 'CAVE',               # Mt. Lavalava
    'isk': 'CAVE',               # Dry Dry Ruins
    'kpa': 'CAVE',               # Koopa Bros Fortress
    'kzn': 'CAVE',               # Mt. Lavalava
    'obk': 'CAVE',               # Crystal Palace
    'omo': 'CAVE',               # Toad Town Sewers
    'tst': 'CAVE',               # Test areas / Bowser's Castle
    
    # Volcano/Lava areas
    'iwa': 'VOLCANO',            # Mt. Lavalava (interior)
    'kzn': 'VOLCANO',            # Mt. Lavalava
    
    # Indoor areas
    'hos': 'INDOOR_WARM',        # Toad Town houses
    'jan': 'INDOOR_WARM',        # Toad Town houses
    'kkj': 'INDOOR_COOL',        # Koopa Bros Fortress interior
    'mgm': 'INDOOR_WARM',        # Mario's House
    'sam': 'INDOOR_COOL',        # Shiver City houses
    'tik': 'INDOOR_WARM',        # Toad Town houses
    
    # Haunted/Spoky areas
    'dgb': 'HAUNTED',            # Tubba Blubba's Manor
    'osr': 'HAUNTED',            # Boo's Mansion
    'trd': 'HAUNTED',            # Bowser's Castle
    
    # Underwater areas
    'mim': 'UNDERWATER',         # Jade Jungle (water parts)
    'osr': 'UNDERWATER',         # Boo's Mansion (flooded parts)
    
    # Sunset/Evening
    'end': 'SUNSET',             # Ending areas
    
    # Night areas
    'obk': 'NIGHT',              # Crystal Palace (night)
}

# Preset configurations by type
PRESET_CONFIGS = {
    'SUNNY_OUTDOOR': {
        'ambient': (80, 85, 90),
        'ambient_power': 12,
        'sky': (200, 220, 255),
        'ground': (100, 80, 60),
        'hemi_intensity': 0.6,
        'rim_intensity': 0.3,
        'rim_color': (255, 240, 200),
        'ao': 0.15,
        'dir_color': (255, 245, 220),
        'dir_intensity': 1.2,
        'dir_vec': (-0.3, -0.8, -0.5),
    },
    'CLOUDY_OUTDOOR': {
        'ambient': (100, 100, 110),
        'ambient_power': 10,
        'sky': (160, 170, 180),
        'ground': (80, 75, 70),
        'hemi_intensity': 0.4,
        'rim_intensity': 0.15,
        'rim_color': (200, 200, 210),
        'ao': 0.1,
        'dir_color': (200, 205, 215),
        'dir_intensity': 0.8,
        'dir_vec': (-0.2, -0.7, -0.6),
    },
    'FOREST': {
        'ambient': (50, 70, 50),
        'ambient_power': 8,
        'sky': (120, 180, 120),
        'ground': (60, 80, 50),
        'hemi_intensity': 0.5,
        'rim_intensity': 0.2,
        'rim_color': (180, 220, 180),
        'ao': 0.25,
        'dir_color': (200, 230, 200),
        'dir_intensity': 0.9,
        'dir_vec': (-0.2, -0.8, -0.4),
    },
    'DESERT': {
        'ambient': (100, 90, 70),
        'ambient_power': 15,
        'sky': (255, 240, 200),
        'ground': (180, 150, 100),
        'hemi_intensity': 0.7,
        'rim_intensity': 0.4,
        'rim_color': (255, 220, 160),
        'ao': 0.1,
        'dir_color': (255, 245, 210),
        'dir_intensity': 1.3,
        'dir_vec': (-0.4, -0.85, -0.3),
    },
    'SNOW': {
        'ambient': (120, 130, 150),
        'ambient_power': 10,
        'sky': (200, 220, 255),
        'ground': (180, 190, 210),
        'hemi_intensity': 0.65,
        'rim_intensity': 0.35,
        'rim_color': (220, 230, 255),
        'ao': 0.15,
        'dir_color': (230, 240, 255),
        'dir_intensity': 1.1,
        'dir_vec': (-0.3, -0.75, -0.5),
    },
    'CAVE': {
        'ambient': (30, 30, 40),
        'ambient_power': 5,
        'sky': (80, 80, 100),
        'ground': (40, 40, 50),
        'hemi_intensity': 0.2,
        'rim_intensity': 0.1,
        'rim_color': (150, 150, 180),
        'ao': 0.35,
        'dir_color': (180, 160, 140),
        'dir_intensity': 0.6,
        'dir_vec': (-0.1, -0.5, -0.3),
    },
    'VOLCANO': {
        'ambient': (60, 30, 20),
        'ambient_power': 8,
        'sky': (255, 100, 50),
        'ground': (150, 60, 30),
        'hemi_intensity': 0.4,
        'rim_intensity': 0.5,
        'rim_color': (255, 150, 80),
        'ao': 0.2,
        'dir_color': (255, 180, 100),
        'dir_intensity': 1.0,
        'dir_vec': (-0.2, -0.6, -0.4),
    },
    'INDOOR_WARM': {
        'ambient': (80, 70, 55),
        'ambient_power': 8,
        'sky': (200, 170, 130),
        'ground': (100, 80, 60),
        'hemi_intensity': 0.3,
        'rim_intensity': 0.25,
        'rim_color': (255, 200, 150),
        'ao': 0.2,
        'dir_color': (255, 210, 160),
        'dir_intensity': 0.8,
        'dir_vec': (-0.2, -0.6, -0.4),
    },
    'INDOOR_COOL': {
        'ambient': (60, 60, 80),
        'ambient_power': 6,
        'sky': (140, 140, 180),
        'ground': (70, 70, 100),
        'hemi_intensity': 0.25,
        'rim_intensity': 0.2,
        'rim_color': (180, 180, 220),
        'ao': 0.25,
        'dir_color': (180, 180, 210),
        'dir_intensity': 0.7,
        'dir_vec': (-0.1, -0.5, -0.3),
    },
    'HAUNTED': {
        'ambient': (30, 25, 35),
        'ambient_power': 4,
        'sky': (80, 60, 100),
        'ground': (40, 30, 50),
        'hemi_intensity': 0.15,
        'rim_intensity': 0.15,
        'rim_color': (150, 120, 180),
        'ao': 0.4,
        'dir_color': (120, 100, 160),
        'dir_intensity': 0.5,
        'dir_vec': (-0.1, -0.4, -0.3),
    },
    'UNDERWATER': {
        'ambient': (30, 50, 80),
        'ambient_power': 6,
        'sky': (60, 120, 180),
        'ground': (20, 60, 100),
        'hemi_intensity': 0.4,
        'rim_intensity': 0.3,
        'rim_color': (100, 180, 255),
        'ao': 0.3,
        'dir_color': (100, 160, 220),
        'dir_intensity': 0.7,
        'dir_vec': (0, -0.8, -0.2),
    },
    'SUNSET': {
        'ambient': (80, 60, 50),
        'ambient_power': 10,
        'sky': (255, 150, 100),
        'ground': (120, 80, 60),
        'hemi_intensity': 0.55,
        'rim_intensity': 0.45,
        'rim_color': (255, 180, 120),
        'ao': 0.15,
        'dir_color': (255, 160, 110),
        'dir_intensity': 1.0,
        'dir_vec': (-0.5, -0.6, -0.3),
    },
    'NIGHT': {
        'ambient': (20, 20, 40),
        'ambient_power': 3,
        'sky': (40, 40, 80),
        'ground': (15, 15, 30),
        'hemi_intensity': 0.2,
        'rim_intensity': 0.3,
        'rim_color': (100, 100, 180),
        'ao': 0.3,
        'dir_color': (120, 120, 180),
        'dir_intensity': 0.4,
        'dir_vec': (-0.1, -0.3, -0.2),
    },
}

def get_area_type(area_name):
    """Determine the lighting type for an area based on its prefix."""
    prefix = area_name.split('_')[0]
    return AREA_LIGHTING_TYPES.get(prefix, 'CAVE')

def get_map_id(area_name, map_name):
    """Generate a unique map ID from area and map names."""
    # Use a hash-like approach
    area_hash = hash(area_name) & 0xFFFF
    map_hash = hash(map_name) & 0xFFFF
    return (area_hash << 16) | map_hash

def get_preset_name(area_name, map_name):
    """Generate preset name matching mapConfig->id format (e.g., 'kmr_00')."""
    # Extract area prefix (kmr, arn, etc.) and map number
    area_parts = area_name.split('_')
    area_prefix = area_parts[1] if len(area_parts) > 1 else area_parts[0]
    if area_prefix.startswith('area_'):
        area_prefix = area_prefix[5:]
    
    # map_name format: "kmr_00", "kmr_02", etc. or just "00", "02"
    map_parts = map_name.split('_')
    if len(map_parts) > 1:
        map_num = map_parts[1]
    else:
        map_num = map_name
    
    return f"{area_prefix}_{map_num}"

def generate_preset_c(area_name, map_name, preset_type):
    """Generate a C struct for a single preset."""
    config = PRESET_CONFIGS[preset_type]
    map_id = get_map_id(area_name, map_name)
    preset_name = get_preset_name(area_name, map_name)
    
    return f'''    {{
        .id = 0x{map_id:08X},
        .name = "{preset_name}",
        .type = ENHANCED_SHADING_PRESET_{preset_type},
        .profile = {{
            .flags = ENHANCED_SHADING_FLAG_ENABLED | ENHANCED_SHADING_FLAG_USE_PRESET | 
                     ENHANCED_SHADING_FLAG_HEMISPHERE | ENHANCED_SHADING_FLAG_RIM_LIGHTING | ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY,
            .presetType = ENHANCED_SHADING_PRESET_{preset_type},
            .lightCount = 2,
            .ambientColor = {{ {config['ambient'][0]}, {config['ambient'][1]}, {config['ambient'][2]} }},
            .ambientPower = {config['ambient_power']},
            .hemisphereSkyColor = {{ {config['sky'][0]}, {config['sky'][1]}, {config['sky'][2]} }},
            .hemisphereGroundColor = {{ {config['ground'][0]}, {config['ground'][1]}, {config['ground'][2]} }},
            .hemisphereIntensity = {config['hemi_intensity']}f,
            .rimLightIntensity = {config['rim_intensity']}f,
            .rimLightColor = {{ {config['rim_color'][0]}, {config['rim_color'][1]}, {config['rim_color'][2]} }},
            .subsurfaceStrength = 0.0f,
            .subsurfaceColor = {{ 255, 200, 150 }},
            .aoIntensity = {config['ao']}f,
            .aoRadius = 1.0f,
            .temporalBlendFactor = 0.85f,
            .lights = {{
                [0] = {{
                    .type = ENHANCED_LIGHT_DIRECTIONAL,
                    .flags = 1,
                    .pos = {{ 0, 0, 0 }},
                    .dir = {{ {config['dir_vec'][0]}f, {config['dir_vec'][1]}f, {config['dir_vec'][2]}f }},
                    .color = {{ {config['dir_color'][0]}, {config['dir_color'][1]}, {config['dir_color'][2]} }},
                    .intensity = {config['dir_intensity']}f,
                    .falloff = 0.0f,
                    .innerAngle = 0.0f,
                    .outerAngle = 0.0f,
                    .radius = 0.0f,
                }},
                [1] = {{
                    .type = ENHANCED_LIGHT_POINT,
                    .flags = 1,
                    .pos = {{ 0, 500, 0 }},
                    .dir = {{ 0, 0, 0 }},
                    .color = {{ {config['rim_color'][0]}, {config['rim_color'][1]}, {config['rim_color'][2]} }},
                    .intensity = 0.25f,
                    .falloff = 0.001f,
                    .innerAngle = 0.0f,
                    .outerAngle = 0.0f,
                    .radius = 800.0f,
                }},
            }},
        }},
    }},'''

def main():
    # Scan all areas
    areas = {}
    for d in os.listdir('src/world'):
        if d.startswith('area_'):
            area_path = f'src/world/{d}'
            if os.path.isdir(area_path):
                maps = []
                for m in os.listdir(area_path):
                    map_path = f'{area_path}/{m}'
                    if os.path.isdir(map_path) and not m.endswith('.h') and m != 'common':
                        maps.append(m)
                if maps:
                    areas[d] = maps
    
    # Generate presets
    presets = []
    for area_name, maps in sorted(areas.items()):
        area_type = get_area_type(area_name)
        for map_name in sorted(maps):
            presets.append(generate_preset_c(area_name, map_name, area_type))
    
    # Write the C file
    header = '''#include "sprite/sprite_enhanced_shading.h"

static EnhancedShadingPreset gAreaPresets[] = {
'''
    footer = '''
};

static const s32 gAreaPresetCount = sizeof(gAreaPresets) / sizeof(gAreaPresets[0]);

void register_all_area_presets(void) {
    for (s32 i = 0; i < gAreaPresetCount; i++) {
        register_enhanced_shading_preset(&gAreaPresets[i]);
    }
}
'''
    
    with open('src/area_lighting_presets.c', 'w') as f:
        f.write(header)
        f.write('\n'.join(presets))
        f.write(footer)
    
    print(f"Generated {len(presets)} presets for {len(areas)} areas")

if __name__ == '__main__':
    main()