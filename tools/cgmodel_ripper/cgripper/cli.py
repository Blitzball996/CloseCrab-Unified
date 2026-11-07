"""命令行入口：cgripper <url> [...]"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import List

from .client import CgClient
from .media import download_hls, download_images, has_ffmpeg, safe_name
from .parser import PageData, VideoItem, parse_page
from .watermark import remove_watermark_batch


def _log(msg: str) -> None:
    print(msg, flush=True)


def write_report(d: PageData, outdir: Path) -> None:
    (outdir / "metadata.json").write_text(
        json.dumps(d.as_dict(), ensure_ascii=False, indent=2), encoding="utf-8")

    lines = [f"# {d.title}", "", f"- 来源: {d.url}", f"- 编号: {d.page_id}"]
    if d.author:
        lines.append(f"- 作者: {d.author}")
    if d.keywords:
        lines.append(f"- 关键词: {', '.join(d.keywords)}")
    if d.params:
        lines += ["", "## 参数"]
        lines += [f"| {k} | {v} |" for k, v in d.params.items()]
    if d.description:
        lines += ["", "## 说明", "", d.description]
    if d.images:
        lines += ["", f"## 图片 ({len(d.images)})"]
        lines += [f"{i}. {u}" for i, u in enumerate(d.images, 1)]
    if d.videos:
        lines += ["", f"## 视频 ({len(d.videos)})"]
        for i, v in enumerate(d.videos, 1):
            lines.append(f"{i}. {v.name or v.md5} | {v.size_mb} | {v.m3u8}")
    (outdir / "info.md").write_text("\n".join(lines), encoding="utf-8")


def process(url: str, args: argparse.Namespace, client: CgClient) -> int:
    _log(f"\n=== 抓取 {url}")
    try:
        html = client.get_text(url)
    except Exception as e:  # noqa: BLE001
        _log(f"页面请求失败: {e}")
        return 1

    d = parse_page(html, url)
    if not d.images and not d.video_md5:
        _log("未解析到任何图片/视频，站点结构可能已变更")

    d.videos = [
        VideoItem(md5=v.get("md5", ""),
                  m3u8=v.get("videoHls", "") or v.get("videoUrl", ""),
                  poster=v.get("videoImg", ""),
                  name=v.get("videoName", "") or v.get("fileName", ""),
                  size_mb=str(v.get("videoSize", "") or ""))
        for v in client.fetch_videos(d.video_md5, url)
    ]

    root = Path(args.out) / f"{d.page_id or int(time.time())}_{safe_name(d.title)}"
    root.mkdir(parents=True, exist_ok=True)
    _log(f"标题: {d.title}")
    _log(f"图片: {len(d.images)}  视频: {len(d.videos)}")
    write_report(d, root)

    imgs: List[Path] = []
    if d.images and not args.no_images:
        _log("下载图片…")
        imgs = download_images(client, d.images, root / "images", url,
                               workers=args.workers, log=_log)

    if imgs and args.wm != "none":
        _log(f"去水印 (模式 {args.wm})…")
        remove_watermark_batch(imgs, root / "images_clean", mode=args.wm,
                               sensitivity=args.wm_strength, log=_log)

    if d.videos and not args.no_video:
        _log("下载视频…")
        vdir = root / "videos"
        for i, v in enumerate(d.videos, 1):
            if not v.m3u8:
                _log(f"  视频 {i} 无播放地址，跳过")
                continue
            base = vdir / safe_name(v.name or f"video_{i}").rsplit(".", 1)[0]
            try:
                download_hls(client, v.m3u8, base, url, workers=args.workers,
                             to_mp4=not args.keep_ts, log=_log)
            except Exception as e:  # noqa: BLE001
                _log(f"  视频 {i} 下载失败: {e}")
            if v.poster:
                try:
                    p = vdir / (base.stem + "_poster.jpg")
                    p.write_bytes(client.get_bytes(
                        v.poster if v.poster.startswith("http") else "https:" + v.poster,
                        referer=url))
                except Exception:  # noqa: BLE001
                    pass

    _log(f"完成 -> {root}")
    return 0


def read_url_list(path: str) -> List[str]:
    return [l.strip() for l in Path(path).read_text(encoding="utf-8").splitlines()
            if l.strip() and not l.startswith("#")]


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="cgripper",
        description="提取 cgmodel.com 作品/模型页的图片、视频与文字介绍，并可去水印。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""示例:
  cgripper https://www.cgmodel.com/model/988253.html
  cgripper https://www.cgmodel.com/art/50616.html --wm consensus
  cgripper -f urls.txt -o D:/cg_out --workers 12
  cgripper <url> --no-video --wm crop
""")
    p.add_argument("urls", nargs="*", help="作品/模型页链接")
    p.add_argument("-f", "--file", help="从文本文件按行读取链接")
    p.add_argument("-o", "--out", default="cg_output", help="输出目录 (默认 cg_output)")
    p.add_argument("--wm", choices=("auto", "consensus", "crop", "none"),
                   default="auto", help="去水印模式 (默认 auto)")
    p.add_argument("--wm-strength", type=float, default=1.0,
                   help="去水印灵敏度, >1 更激进 (默认 1.0)")
    p.add_argument("--workers", type=int, default=8, help="并发数 (默认 8)")
    p.add_argument("--no-images", action="store_true", help="不下载图片")
    p.add_argument("--no-video", action="store_true", help="不下载视频")
    p.add_argument("--keep-ts", action="store_true", help="视频保留 .ts 不转 mp4")
    p.add_argument("--proxy", help="代理, 如 http://127.0.0.1:7890")
    p.add_argument("--cookie", help="登录后的 Cookie (访问受限内容时使用)")
    p.add_argument("--delay", type=float, default=1.0, help="多链接间隔秒数")
    return p


def main(argv: List[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    urls = list(args.urls)
    if args.file:
        urls += read_url_list(args.file)
    if not urls:
        build_parser().print_help()
        return 2

    if not has_ffmpeg() and not args.no_video:
        _log("提示: 未检测到 ffmpeg，视频将保存为 .ts (可正常播放)")

    rc = 0
    with CgClient(proxy=args.proxy, cookie=args.cookie) as client:
        for i, u in enumerate(urls):
            if not u.startswith("http"):
                u = "https://www.cgmodel.com" + (u if u.startswith("/") else "/" + u)
            rc |= process(u, args, client)
            if i + 1 < len(urls):
                time.sleep(args.delay)
    return rc


if __name__ == "__main__":
    sys.exit(main())
