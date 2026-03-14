#!/usr/bin/env python3
"""
Agni Engine Build Script
Simple script to configure and build the Agni engine with CMake.

Usage:
    python build.py                 # Build in Debug mode (auto-detect generator)
    python build.py --release       # Build in Release mode
    python build.py --clean         # Clean build directory
    python build.py --tracy         # Also build Tracy profiler
    python build.py --no-shaders    # Skip shader compilation
    python build.py --no-tracy      # Disable Tracy profiling
    python build.py -G vs2022       # Use Visual Studio 2022
    python build.py -G vs2026       # Use Visual Studio 2026
    python build.py -G make         # Use Unix Makefiles (Linux/macOS)
"""

import argparse
import subprocess
import sys
import shutil
import time
from pathlib import Path
import platform


class Colors:
    """ANSI color codes for terminal output"""
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

    @staticmethod
    def disable():
        """Disable colors on Windows if not supported"""
        if platform.system() == 'Windows':
            Colors.HEADER = ''
            Colors.OKBLUE = ''
            Colors.OKCYAN = ''
            Colors.OKGREEN = ''
            Colors.WARNING = ''
            Colors.FAIL = ''
            Colors.ENDC = ''
            Colors.BOLD = ''


def print_header(message):
    """Print a header message"""
    print(f"\n{Colors.HEADER}{Colors.BOLD}{'=' * 60}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{message}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{'=' * 60}{Colors.ENDC}\n")


def print_success(message):
    """Print a success message"""
    print(f"{Colors.OKGREEN}[OK] {message}{Colors.ENDC}")


def print_error(message):
    """Print an error message"""
    print(f"{Colors.FAIL}[ERROR] {message}{Colors.ENDC}", file=sys.stderr)


def print_warning(message):
    """Print a warning message"""
    print(f"{Colors.WARNING}[WARNING] {message}{Colors.ENDC}")


def print_info(message):
    """Print an info message"""
    print(f"{Colors.OKCYAN}> {message}{Colors.ENDC}")


def run_command(cmd, cwd=None, description=None):
    """Run a shell command and handle errors"""
    if description:
        print_info(description)

    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            check=True,
            capture_output=False,
            text=True
        )
        return True
    except subprocess.CalledProcessError as e:
        print_error(f"Command failed with exit code {e.returncode}")
        return False
    except FileNotFoundError:
        print_error(f"Command not found: {cmd[0]}")
        print_error("Make sure CMake is installed and in your PATH")
        return False


def clean_build(build_dir):
    """Clean the build directory"""
    print_header("Cleaning Build Directory")

    if build_dir.exists():
        print_info(f"Removing {build_dir}")
        shutil.rmtree(build_dir)
        print_success("Build directory cleaned")
    else:
        print_info("Build directory doesn't exist, nothing to clean")


def configure_cmake(build_dir, source_dir, build_type, compile_shaders, enable_tracy, generator=None):
    """Configure CMake project"""
    print_header(f"Configuring CMake ({build_type} build)")

    # Map generator shortcuts to CMake generator names
    generator_map = {
        "vs2022": "Visual Studio 17 2022",
        "vs2026": "Visual Studio 18 2026",
        "make": "Unix Makefiles"
    }

    # Build CMake command
    cmake_args = [
        "cmake",
        "-S", str(source_dir),
        "-B", str(build_dir),
    ]

    # Add generator if specified
    if generator:
        cmake_generator = generator_map.get(generator, generator)
        cmake_args.extend(["-G", cmake_generator])
        print_info(f"Generator: {cmake_generator}")

    # Add build type for single-config generators (Unix Makefiles)
    # Multi-config generators (Visual Studio, Xcode) use --config at build time
    is_single_config = generator == "make" or (generator is None and platform.system() != 'Windows')
    if is_single_config:
        cmake_args.extend(["-DCMAKE_BUILD_TYPE=" + build_type])

    # Add build options
    if not compile_shaders:
        cmake_args.append("-DAGNI_COMPILE_SHADERS=OFF")
        print_info("Shader compilation: DISABLED (using pre-compiled shaders)")
    else:
        print_info("Shader compilation: ENABLED")

    if not enable_tracy:
        cmake_args.append("-DAGNI_ENABLE_TRACY=OFF")
        print_info("Tracy profiling: DISABLED")
    else:
        print_info("Tracy profiling: ENABLED")

    # Run CMake configuration
    if run_command(cmake_args, description=f"Running CMake configure..."):
        print_success("CMake configuration complete")
        return True
    else:
        print_error("CMake configuration failed")
        return False


def build_project(build_dir, build_type, jobs=None):
    """Build the project using CMake"""
    print_header(f"Building Agni Engine ({build_type})")

    # Build CMake command
    cmake_args = [
        "cmake",
        "--build", str(build_dir),
        "--config", build_type,
    ]

    # Add parallel jobs
    if jobs:
        cmake_args.extend(["--parallel", str(jobs)])
    else:
        cmake_args.append("--parallel")  # Auto-detect

    # Run build
    if run_command(cmake_args, description="Building project..."):
        print_success("Build complete!")
        return True
    else:
        print_error("Build failed")
        return False


def build_tracy_profiler(tracy_dir, build_type, generator=None):
    """Build Tracy profiler viewer"""
    print_header("Building Tracy Profiler Viewer")

    tracy_build_dir = tracy_dir / "profiler" / "build"
    tracy_source_dir = tracy_dir / "profiler"

    # Map generator shortcuts to CMake generator names
    generator_map = {
        "vs2022": "Visual Studio 17 2022",
        "vs2026": "Visual Studio 18 2026",
        "make": "Unix Makefiles"
    }

    # Configure Tracy
    cmake_args = [
        "cmake",
        "-S", str(tracy_source_dir),
        "-B", str(tracy_build_dir),
    ]

    # Add generator if specified
    if generator:
        cmake_generator = generator_map.get(generator, generator)
        cmake_args.extend(["-G", cmake_generator])

    # Add build type for single-config generators
    is_single_config = generator == "make" or (generator is None and platform.system() != 'Windows')
    if is_single_config:
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={build_type}")

    if not run_command(cmake_args, description="Configuring Tracy profiler..."):
        print_error("Tracy profiler configuration failed")
        return False

    # Build Tracy
    build_args = [
        "cmake",
        "--build", str(tracy_build_dir),
        "--config", build_type,
        "--parallel"
    ]

    if run_command(build_args, description="Building Tracy profiler..."):
        print_success("Tracy profiler built successfully!")

        # Show location
        if platform.system() == 'Windows':
            exe_path = tracy_build_dir / "Release" / "tracy-profiler.exe"
        else:
            exe_path = tracy_build_dir / "tracy-profiler"

        print_info(f"Tracy profiler location: {exe_path}")
        return True
    else:
        print_error("Tracy profiler build failed")
        return False


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description="Build script for Agni Engine",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python build.py                    # Debug build + Tracy profiler (development)
  python build.py --release          # Release build (optimized, no Tracy viewer)
  python build.py --clean            # Clean and rebuild
  python build.py --release --tracy  # Release build + Tracy profiler
  python build.py --no-shaders       # Skip shader compilation (faster CI builds)
  python build.py --no-tracy         # Skip Tracy profiler build
  python build.py -G vs2022          # Use Visual Studio 2022
  python build.py -G vs2026          # Use Visual Studio 2026
  python build.py -G make            # Use Unix Makefiles (Linux/macOS)
        """
    )

    parser.add_argument(
        "--release",
        action="store_true",
        help="Build in Release mode (default: Debug)"
    )

    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean build directory before building"
    )

    parser.add_argument(
        "--tracy",
        action="store_true",
        help="Build Tracy profiler viewer (automatically enabled for Debug builds)"
    )

    parser.add_argument(
        "--no-shaders",
        action="store_true",
        help="Skip shader compilation (use pre-compiled shaders)"
    )

    parser.add_argument(
        "--no-tracy",
        action="store_true",
        help="Disable Tracy profiling integration"
    )

    parser.add_argument(
        "-j", "--jobs",
        type=int,
        help="Number of parallel build jobs (default: auto-detect)"
    )

    parser.add_argument(
        "-G", "--generator",
        type=str,
        choices=["vs2022", "vs2026", "make"],
        help="Build system generator: vs2022 (Visual Studio 17 2022), vs2026 (Visual Studio 18 2026), make (Unix Makefiles)"
    )

    args = parser.parse_args()

    # Disable colors on Windows unless running in a modern terminal
    if platform.system() == 'Windows':
        Colors.disable()

    # Determine paths
    script_dir = Path(__file__).parent.resolve()
    build_dir = script_dir / "build"
    tracy_dir = script_dir / "third_party" / "tracy"

    # Determine build type
    build_type = "Release" if args.release else "Debug"

    # Print welcome message
    print_header("Agni Engine Build Script")
    print_info(f"Source directory: {script_dir}")
    print_info(f"Build directory:  {build_dir}")
    print_info(f"Build type:       {build_type}")

    start_time = time.time()

    # Clean if requested
    if args.clean:
        clean_build(build_dir)

    # Configure CMake
    if not configure_cmake(
        build_dir,
        script_dir,
        build_type,
        compile_shaders=not args.no_shaders,
        enable_tracy=not args.no_tracy,
        generator=args.generator
    ):
        elapsed = time.time() - start_time
        print_header("Build FAILED")
        print_error(f"CMake configuration failed after {elapsed:.2f}s")
        input("\nPress Enter to exit...")
        return 1

    # Build project
    if not build_project(build_dir, build_type, args.jobs):
        elapsed = time.time() - start_time
        print_header("Build FAILED")
        print_error(f"Compilation failed after {elapsed:.2f}s")
        input("\nPress Enter to exit...")
        return 1

    # Build Tracy profiler if requested or if Debug build (for development)
    should_build_tracy = (args.tracy or build_type == "Debug") and not args.no_tracy
    if should_build_tracy:
        if build_type == "Debug" and not args.tracy:
            print_info("Debug build detected - automatically building Tracy profiler")
        if not build_tracy_profiler(tracy_dir, build_type, args.generator):
            print_warning("Tracy profiler build failed, but engine build succeeded")

    elapsed = time.time() - start_time
    minutes, seconds = divmod(int(elapsed), 60)

    # Print final success message
    print_header("Build Successful!")

    if platform.system() == 'Windows':
        exe_path = script_dir / "bin" / build_type / "engine.exe"
    else:
        exe_path = script_dir / "bin" / build_type / "engine"

    print_success(f"Status:     Build succeeded")
    print_success(f"Time:       {minutes}m {seconds}s ({elapsed:.2f}s)")
    print_success(f"Config:     {build_type}")
    print_success(f"Output:     {exe_path}")

    if should_build_tracy:
        if platform.system() == 'Windows':
            tracy_exe = tracy_dir / "profiler" / "build" / build_type / "tracy-profiler.exe"
        else:
            tracy_exe = tracy_dir / "profiler" / "build" / "tracy-profiler"
        print_success(f"Tracy profiler: {tracy_exe}")
        print_info("Run the Tracy profiler, then launch the engine to profile!")

    input("\nPress Enter to exit...")
    return 0


if __name__ == "__main__":
    sys.exit(main())
