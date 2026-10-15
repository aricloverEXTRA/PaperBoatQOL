#!/usr/bin/env python3
"""
Performance benchmark script for Paper Mario 64 Enhanced Shading.
Tests performance on various hardware configurations including GT 1030.
"""

import os
import sys
import time
import subprocess
import json
import platform
from dataclasses import dataclass, asdict
from typing import List, Dict, Optional

@dataclass
class BenchmarkResult:
    test_name: str
    resolution: str
    msaa: int
    internal_res: float
    avg_fps: float
    min_fps: float
    max_fps: float
    frame_time_ms: float
    gpu_memory_mb: int
    cpu_percent: float
    gpu_percent: float
    success: bool
    error: Optional[str] = None

@dataclass
class HardwareInfo:
    cpu: str
    gpu: str
    ram_gb: int
    os: str
    driver_version: str

def get_hardware_info() -> HardwareInfo:
    """Detect hardware information."""
    cpu = platform.processor()
    if not cpu:
        try:
            with open('/proc/cpuinfo', 'r') as f:
                for line in f:
                    if 'model name' in line:
                        cpu = line.split(':')[1].strip()
                        break
        except:
            cpu = "Unknown"
    
    gpu = "Unknown"
    try:
        # Try nvidia-smi for NVIDIA GPUs
        result = subprocess.run(['nvidia-smi', '--query-gpu=name', '--format=csv,noheader'], 
                              capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            gpu = result.stdout.strip()
    except:
        pass
    
    if gpu == "Unknown":
        try:
            # Try lspci for other GPUs
            result = subprocess.run(['lspci', '-v'], capture_output=True, text=True, timeout=5)
            for line in result.stdout.split('\n'):
                if 'VGA' in line or '3D' in line:
                    gpu = line.split(':')[-1].strip()
                    break
        except:
            pass
    
    ram_gb = 0
    try:
        with open('/proc/meminfo', 'r') as f:
            for line in f:
                if 'MemTotal' in line:
                    ram_gb = int(line.split()[1]) // (1024 * 1024)
                    break
    except:
        try:
            import psutil
            ram_gb = psutil.virtual_memory().total // (1024 * 1024 * 1024)
        except:
            pass
    
    os_name = platform.system() + " " + platform.release()
    
    driver_version = "Unknown"
    try:
        result = subprocess.run(['nvidia-smi', '--query-gpu=driver_version', '--format=csv,noheader'],
                              capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            driver_version = result.stdout.strip()
    except:
        pass
    
    return HardwareInfo(
        cpu=cpu,
        gpu=gpu,
        ram_gb=ram_gb,
        os=os_name,
        driver_version=driver_version
    )

def run_benchmark_test(
    rom_path: str,
    test_name: str,
    resolution: str,
    msaa: int,
    internal_res: float,
    duration_seconds: int = 30,
    area: str = "kmr_00",
    enhanced_shading: bool = True,
    shading_mode: int = 1,
    dynamic_lights: bool = True,
    hemisphere: bool = True,
    rim_lighting: bool = False,
    subsurface: bool = False,
    ao: bool = False
) -> BenchmarkResult:
    """Run a single benchmark test."""
    
    # Build command line arguments for PaperBoat
    args = [
        '--rom', rom_path,
        '--area', area,
        '--benchmark',
        '--duration', str(duration_seconds),
        '--resolution', resolution,
        '--msaa', str(msaa),
        '--internal-res', str(internal_res),
    ]
    
    if not enhanced_shading:
        args.append('--no-enhanced-shading')
    else:
        args.append('--shading-mode')
        args.append(str(shading_mode))
        if not dynamic_lights:
            args.append('--no-dynamic-lights')
        if not hemisphere:
            args.append('--no-hemisphere')
        if rim_lighting:
            args.append('--rim-lighting')
        if subsurface:
            args.append('--subsurface')
        if ao:
            args.append('--ao')
    
    try:
        # Run the benchmark
        # Note: This assumes PaperBoat has benchmark command-line support
        # If not, this would need to be implemented in the engine
        result = subprocess.run(
            ['paperboat'] + args,
            capture_output=True,
            text=True,
            timeout=duration_seconds + 30
        )
        
        if result.returncode != 0:
            return BenchmarkResult(
                test_name=test_name,
                resolution=resolution,
                msaa=msaa,
                internal_res=internal_res,
                avg_fps=0, min_fps=0, max_fps=0,
                frame_time_ms=0, gpu_memory_mb=0,
                cpu_percent=0, gpu_percent=0,
                success=False,
                error=result.stderr[:500] if result.stderr else "Unknown error"
            )
        
        # Parse output for FPS metrics
        # Expected format: "AVG_FPS: 60.5 MIN_FPS: 55.2 MAX_FPS: 62.1 FRAME_TIME_MS: 16.5 GPU_MEM_MB: 512 CPU_PCT: 45 GPU_PCT: 78"
        output = result.stdout
        avg_fps = min_fps = max_fps = frame_time_ms = 0
        gpu_memory_mb = 0
        cpu_percent = gpu_percent = 0
        
        for line in output.split('\n'):
            if 'AVG_FPS:' in line:
                parts = line.split()
                for i, part in enumerate(parts):
                    if part == 'AVG_FPS:':
                        avg_fps = float(parts[i+1])
                    elif part == 'MIN_FPS:':
                        min_fps = float(parts[i+1])
                    elif part == 'MAX_FPS:':
                        max_fps = float(parts[i+1])
                    elif part == 'FRAME_TIME_MS:':
                        frame_time_ms = float(parts[i+1])
                    elif part == 'GPU_MEM_MB:':
                        gpu_memory_mb = int(parts[i+1])
                    elif part == 'CPU_PCT:':
                        cpu_percent = float(parts[i+1])
                    elif part == 'GPU_PCT:':
                        gpu_percent = float(parts[i+1])
        
        return BenchmarkResult(
            test_name=test_name,
            resolution=resolution,
            msaa=msaa,
            internal_res=internal_res,
            avg_fps=avg_fps,
            min_fps=min_fps,
            max_fps=max_fps,
            frame_time_ms=frame_time_ms,
            gpu_memory_mb=gpu_memory_mb,
            cpu_percent=cpu_percent,
            gpu_percent=gpu_percent,
            success=True
        )
        
    except subprocess.TimeoutExpired:
        return BenchmarkResult(
            test_name=test_name,
            resolution=resolution,
            msaa=msaa,
            internal_res=internal_res,
            avg_fps=0, min_fps=0, max_fps=0,
            frame_time_ms=0, gpu_memory_mb=0,
            cpu_percent=0, gpu_percent=0,
            success=False,
            error="Benchmark timed out"
        )
    except Exception as e:
        return BenchmarkResult(
            test_name=test_name,
            resolution=resolution,
            msaa=msaa,
            internal_res=internal_res,
            avg_fps=0, min_fps=0, max_fps=0,
            frame_time_ms=0, gpu_memory_mb=0,
            cpu_percent=0, gpu_percent=0,
            success=False,
            error=str(e)
        )

def run_full_benchmark(rom_path: str, output_file: str = "benchmark_results.json"):
    """Run a comprehensive benchmark suite."""
    
    hw = get_hardware_info()
    print(f"Hardware: {hw.cpu} | {hw.gpu} | {hw.ram_gb}GB RAM | {hw.os}")
    print(f"Driver: {hw.driver_version}")
    print()
    
    # Test configurations for GT 1030 and similar low-end hardware
    test_configs = [
        # Baseline vanilla
        ("Vanilla_720p", "1280x720", 1, 1.0, False, 0, False, False, False, False, False),
        ("Vanilla_1080p", "1920x1080", 1, 1.0, False, 0, False, False, False, False, False),
        
        # Enhanced shading - Area presets only (recommended for low-end)
        ("Enhanced_Presets_720p", "1280x720", 1, 1.0, True, 1, False, True, False, False, False),
        ("Enhanced_Presets_1080p", "1920x1080", 1, 1.0, True, 1, False, True, False, False, False),
        
        # Enhanced shading with dynamic lights (torch/lantern)
        ("Enhanced_Dynamic_720p", "1280x720", 1, 1.0, True, 1, True, True, False, False, False),
        ("Enhanced_Dynamic_1080p", "1920x1080", 1, 1.0, True, 1, True, True, False, False, False),
        
        # With MSAA 2x
        ("Enhanced_Dynamic_720p_2xMSAA", "1280x720", 2, 1.0, True, 1, True, True, False, False, False),
        ("Enhanced_Dynamic_1080p_2xMSAA", "1920x1080", 2, 1.0, True, 1, True, True, False, False, False),
        
        # With internal resolution scaling (super sampling)
        ("Enhanced_720p_1.5xIR", "1280x720", 1, 1.5, True, 1, True, True, False, False, False),
        ("Enhanced_1080p_1.5xIR", "1920x1080", 1, 1.5, True, 1, True, True, False, False, False),
        
        # Full features (high-end test)
        ("Enhanced_Full_720p", "1280x720", 1, 1.0, True, 1, True, True, True, True, True),
        ("Enhanced_Full_1080p", "1920x1080", 1, 1.0, True, 1, True, True, True, True, True),
    ]
    
    results = []
    
    for i, (name, res, msaa, ir, enhanced, mode, dyn, hemi, rim, sss, ao) in enumerate(test_configs):
        print(f"[{i+1}/{len(test_configs)}] Running {name}...")
        
        result = run_benchmark_test(
            rom_path=rom_path,
            test_name=name,
            resolution=res,
            msaa=msaa,
            internal_res=ir,
            duration_seconds=30,
            area="kmr_00",  # Goomba Village - good outdoor test
            enhanced_shading=enhanced,
            shading_mode=mode,
            dynamic_lights=dyn,
            hemisphere=hemi,
            rim_lighting=rim,
            subsurface=sss,
            ao=ao
        )
        
        results.append(result)
        
        if result.success:
            print(f"  ✓ {result.avg_fps:.1f} FPS avg (min: {result.min_fps:.1f}, max: {result.max_fps:.1f})")
            print(f"    Frame time: {result.frame_time_ms:.1f}ms | GPU: {result.gpu_memory_mb}MB | CPU: {result.cpu_percent:.0f}% | GPU: {result.gpu_percent:.0f}%")
        else:
            print(f"  ✗ FAILED: {result.error}")
        
        # Cool down between tests
        time.sleep(3)
    
    # Also test indoor area with dynamic lights
    print("\n--- Indoor Area Tests (with torches/lanterns) ---")
    indoor_configs = [
        ("Indoor_Dynamic_720p", "1280x720", 1, 1.0, True, 1, True, True, False, False, False, "hos_01"),
        ("Indoor_Dynamic_1080p", "1920x1080", 1, 1.0, True, 1, True, True, False, False, False, "hos_01"),
        ("Indoor_Full_720p", "1280x720", 1, 1.0, True, 1, True, True, True, True, True, "hos_01"),
    ]
    
    for i, (name, res, msaa, ir, enhanced, mode, dyn, hemi, rim, sss, ao, area) in enumerate(indoor_configs):
        print(f"[{len(test_configs)+i+1}/{len(test_configs)+len(indoor_configs)}] Running {name}...")
        
        result = run_benchmark_test(
            rom_path=rom_path,
            test_name=name,
            resolution=res,
            msaa=msaa,
            internal_res=ir,
            duration_seconds=30,
            area=area,
            enhanced_shading=enhanced,
            shading_mode=mode,
            dynamic_lights=dyn,
            hemisphere=hemi,
            rim_lighting=rim,
            subsurface=sss,
            ao=ao
        )
        
        results.append(result)
        
        if result.success:
            print(f"  ✓ {result.avg_fps:.1f} FPS avg (min: {result.min_fps:.1f}, max: {result.max_fps:.1f})")
        else:
            print(f"  ✗ FAILED: {result.error}")
        
        time.sleep(3)
    
    # Save results
    output = {
        "hardware": asdict(hw),
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "results": [asdict(r) for r in results]
    }
    
    with open(output_file, 'w') as f:
        json.dump(output, f, indent=2)
    
    print(f"\nResults saved to {output_file}")
    
    # Print summary
    print("\n=== BENCHMARK SUMMARY ===")
    print(f"{'Test':<30} {'Res':<12} {'MSAA':<5} {'IR':<5} {'Avg FPS':<8} {'Min FPS':<8} {'Frame ms':<9} {'GPU MB':<7} {'Status'}")
    print("-" * 100)
    
    for r in results:
        status = "PASS" if r.success else "FAIL"
        if r.success:
            print(f"{r.test_name:<30} {r.resolution:<12} {r.msaa:<5} {r.internal_res:<5.1f} {r.avg_fps:<8.1f} {r.min_fps:<8.1f} {r.frame_time_ms:<9.1f} {r.gpu_memory_mb:<7} {status}")
        else:
            print(f"{r.test_name:<30} {r.resolution:<12} {r.msaa:<5} {r.internal_res:<5.1f} {'N/A':<8} {'N/A':<8} {'N/A':<9} {'N/A':<7} {status} ({r.error})")
    
    # Recommendations
    print("\n=== RECOMMENDATIONS FOR GT 1030 ===")
    gt1030_results = [r for r in results if r.success and "1080p" in r.test_name and r.msaa == 1 and r.internal_res == 1.0]
    if gt1030_results:
        avg_1080p = sum(r.avg_fps for r in gt1030_results) / len(gt1030_results)
        print(f"Average 1080p FPS (no MSAA, 1x IR): {avg_1080p:.1f}")
        
        if avg_1080p >= 55:
            print("✓ GT 1030 handles 1080p well with enhanced shading!")
            print("  Recommended settings: 1080p, 1x MSAA, 1x IR, Area Presets + Dynamic Lights")
        elif avg_1080p >= 30:
            print("~ GT 1030 handles 1080p but may dip below 60 FPS")
            print("  Recommended settings: 720p or 900p, 1x MSAA, 1x IR, Area Presets only")
        else:
            print("✗ GT 1030 struggles at 1080p with enhanced shading")
            print("  Recommended settings: 720p, 1x MSAA, 1x IR, Vanilla or Area Presets only")
    
    return results

def main():
    if len(sys.argv) < 2:
        print("Usage: python benchmark.py <rom_path> [output_file]")
        print("Example: python benchmark.py 'Paper Mario (USA).z64' results.json")
        sys.exit(1)
    
    rom_path = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else "benchmark_results.json"
    
    if not os.path.exists(rom_path):
        print(f"ROM not found: {rom_path}")
        sys.exit(1)
    
    run_full_benchmark(rom_path, output_file)

if __name__ == '__main__':
    main()