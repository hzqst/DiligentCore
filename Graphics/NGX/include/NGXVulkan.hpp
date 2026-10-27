/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once
#include "NGXRuntime.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace Diligent
{
// Null native handles query instance requirements only, before instance creation.
DILIGENT_NGX_API bool GetNGXVulkanExtensions(VkInstance Instance, VkPhysicalDevice PhysicalDevice, std::vector<std::string>& InstanceExtensions, std::vector<std::string>& DeviceExtensions);
} // namespace Diligent
