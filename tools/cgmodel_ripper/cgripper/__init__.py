"""cgripper — CG模型网 (cgmodel.com) 素材提取工具。

模块划分:
    client      HTTP 会话 / CSRF 管理
    parser      详情页解析 (标题/参数/说明/图片/视频)
    media       图片与 HLS 视频下载
    watermark   水印去除 (频域 + 修补)
    cli         命令行入口
"""

__version__ = "1.0.0"

from .parser import PageData, parse_page
from .client import CgClient

__all__ = ["PageData", "parse_page", "CgClient", "__version__"]
