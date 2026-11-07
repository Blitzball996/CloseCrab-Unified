"""媒体下载：图片并发下载 + HLS(.m3u8) 分片下载合并。

无需外部 ffmpeg 也能工作：TS 分片按序拼接为 .ts 文件即可正常播放。
若检测到 ffmpeg，则额外 remux 成 .mp4 (拷贝流，无损无重编码)。
"""

from __future__ import annotations

import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import urllib.parse
from pathlib import Path
from typing import Callable, List, Optional, Tuple

from .client import CgClient

SAFE_RE = re.compile(r'[\\/:*?"<>|\r\n\t]+')


def safe_name(name: str, maxlen: int = 80) -> str:
    n = SAFE_RE.sub("_", name).strip(" .")
    n = re.sub(r"\s+", " ", n)
    return (n[:maxlen] or "untitled")


def has_ffmpeg() -> bool:
    return shutil.which("ffmpeg") is not None


def download_images(client: CgClient, urls: List[str], outdir: Path,
                    referer: str, workers: int = 6,
                    log: Optional[Callable[[str], None]] = None) -> List[Path]:
    outdir.mkdir(parents=True, exist_ok=True)
    log = log or (lambda m: None)
    results: List[Tuple[int, Optional[Path]]] = []

    def one(idx_url):
        idx, url = idx_url
        ext = os.path.splitext(urllib.parse.urlparse(url).path)[1] or ".webp"
        dst = outdir / f"{idx:02d}{ext}"
        try:
            data = client.get_bytes(url, referer=referer)
            if len(data) < 512:
                raise ValueError("响应过小，可能被拦截")
            dst.write_bytes(data)
            log(f"  图片 {idx:02d} -> {dst.name} ({len(data)//1024} KB)")
            return idx, dst
        except Exception as e:  # noqa: BLE001
            log(f"  图片 {idx:02d} 失败: {e}")
            return idx, None

    with cf.ThreadPoolExecutor(max_workers=workers) as ex:
        results = list(ex.map(one, enumerate(urls, 1)))

    return [p for _, p in sorted(results) if p]


def _abs_seg(base_m3u8: str, seg: str) -> str:
    if seg.startswith("http"):
        return seg
    if seg.startswith("//"):
        return "https:" + seg
    return urllib.parse.urljoin(base_m3u8, seg)


def download_hls(client: CgClient, m3u8_url: str, out_base: Path,
                 referer: str, workers: int = 8,
                 to_mp4: bool = True,
                 log: Optional[Callable[[str], None]] = None) -> Optional[Path]:
    """下载 m3u8 全部分片并合并。返回最终文件路径。"""
    log = log or (lambda m: None)
    if m3u8_url.startswith("//"):
        m3u8_url = "https:" + m3u8_url

    playlist = client.get_bytes(m3u8_url, referer=referer).decode("utf-8", "ignore")

    # 主播放列表 -> 选第一个子流
    if "#EXT-X-STREAM-INF" in playlist:
        subs = [l.strip() for l in playlist.splitlines()
                if l.strip() and not l.startswith("#")]
        if subs:
            return download_hls(client, _abs_seg(m3u8_url, subs[0]), out_base,
                                referer, workers, to_mp4, log)

    if "#EXT-X-KEY" in playlist and "METHOD=NONE" not in playlist:
        log("  警告: 播放列表声明了加密 (EXT-X-KEY)，合并结果可能无法播放")

    segs = [_abs_seg(m3u8_url, l.strip()) for l in playlist.splitlines()
            if l.strip() and not l.startswith("#")]
    if not segs:
        log("  未在播放列表中找到分片")
        return None
    log(f"  分片数: {len(segs)}")

    out_base.parent.mkdir(parents=True, exist_ok=True)
    tmpdir = out_base.parent / (out_base.stem + "_ts")
    tmpdir.mkdir(exist_ok=True)

    def one(i_url):
        i, u = i_url
        p = tmpdir / f"{i:05d}.ts"
        if p.exists() and p.stat().st_size > 0:
            return i, p
        try:
            p.write_bytes(client.get_bytes(u, referer=referer))
            return i, p
        except Exception:  # noqa: BLE001
            return i, None

    with cf.ThreadPoolExecutor(max_workers=workers) as ex:
        parts = sorted(ex.map(one, enumerate(segs)))

    missing = [i for i, p in parts if p is None]
    if missing:
        log(f"  {len(missing)} 个分片下载失败，将跳过")

    ts_out = out_base.with_suffix(".ts")
    with open(ts_out, "wb") as f:
        for _, p in parts:
            if p:
                f.write(p.read_bytes())
    shutil.rmtree(tmpdir, ignore_errors=True)
    log(f"  合并 -> {ts_out.name} ({ts_out.stat().st_size // 1048576} MB)")

    if to_mp4 and has_ffmpeg():
        mp4 = out_base.with_suffix(".mp4")
        cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", str(ts_out),
               "-c", "copy", "-bsf:a", "aac_adtstoasc", str(mp4)]
        try:
            subprocess.run(cmd, check=True, timeout=1800)
            ts_out.unlink(missing_ok=True)
            log(f"  转封装 -> {mp4.name}")
            return mp4
        except Exception as e:  # noqa: BLE001
            log(f"  ffmpeg 转封装失败({e})，保留 .ts")
    elif to_mp4:
        log("  未检测到 ffmpeg，保留 .ts (可直接播放)")
    return ts_out
