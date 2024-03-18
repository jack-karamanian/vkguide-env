# Setup

## macOS
1. Install the Vulkan SDK https://www.lunarg.com/vulkan-sdk/
2. Setup Vulkan SDK environment variables
```
source /path/to/vulkan-sdk/setup-env.sh
```

## All Systems
1. Setup vcpkg https://learn.microsoft.com/en-us/vcpkg/get_started/overview
2. Set the `VCPKG_ROOT` environment variable to the vcpkg directory
3. Configure `cmake -S . --preset default`
4. Build `cmake --build build`

