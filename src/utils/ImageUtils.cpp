// ImageUtils.cpp - stb_image implementation
// 在这个文件中定义 STB 的实现

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO  // We handle file I/O ourselves
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

// 这个文件需要单独编译，只包含 stb 的实现
// 所有其他文件通过 ImageUtils_Enhanced.h 使用接口
