"""水印去除。

cgmodel 的图片水印特征 (实测):
  * 半透明浅色文字/logo, 在整幅图上重复平铺;
  * 灰度残差 (原图 - 高斯模糊) 中呈规律条纹, 边缘为细笔画;
  * 多张同一作品的图共享同一水印模板 (位置随图尺寸缩放)。

策略 (从强到弱, 自动选择):
  1. ``consensus``  多图共识: 对同作品多张图取残差中值, 提取共同出现的
     笔画 -> 掩膜, 再 inpaint。水印固定平铺时效果最好。
  2. ``auto``       单图检测: 形态学 top-hat 提取细而亮/暗的笔画,
     结合局部对比度阈值生成掩膜 -> inpaint。
  3. ``crop``       直接裁掉带水印的边缘条带 (最稳妥, 会损失画幅)。

去水印本质是像素重建, 无法 100% 还原; ``--wm consensus`` 通常最干净。
"""

from __future__ import annotations

import io
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

import cv2
import numpy as np
from PIL import Image


def load_bgr(path: Path) -> np.ndarray:
    """读图 (兼容 webp / 中文路径)。"""
    with Image.open(path) as im:
        rgb = im.convert("RGB")
        arr = np.asarray(rgb)
    return cv2.cvtColor(arr, cv2.COLOR_RGB2BGR)


def save_bgr(path: Path, img: np.ndarray, quality: int = 95) -> None:
    rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    im = Image.fromarray(rgb)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix.lower() in (".jpg", ".jpeg"):
        im.save(path, quality=quality, subsampling=1)
    elif path.suffix.lower() == ".webp":
        im.save(path, quality=quality, method=5)
    else:
        im.save(path)


def _residual(gray: np.ndarray, sigma: float = 3.0) -> np.ndarray:
    blur = cv2.GaussianBlur(gray, (0, 0), sigma)
    return gray.astype(np.float32) - blur.astype(np.float32)


def detect_mask_single(img: np.ndarray, sensitivity: float = 1.0) -> np.ndarray:
    """单图水印掩膜：top-hat 提取细笔画 + 残差能量筛选。"""
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

    k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
    white = cv2.morphologyEx(gray, cv2.MORPH_TOPHAT, k)   # 比周围亮的细结构
    black = cv2.morphologyEx(gray, cv2.MORPH_BLACKHAT, k)  # 比周围暗的细结构
    stroke = cv2.max(white, black)

    res = np.abs(_residual(gray, 3.0))
    energy = cv2.GaussianBlur(res, (0, 0), 1.2)

    s_thr = float(np.percentile(stroke, 99.0) / max(sensitivity, 0.2))
    e_thr = float(np.percentile(energy, 97.5) / max(sensitivity, 0.2))
    mask = ((stroke >= max(s_thr, 4)) & (energy >= max(e_thr, 2.5)))

    # 排除本身细节丰富的区域(模型表面纹理), 只保留孤立细笔画
    dense = cv2.GaussianBlur((mask.astype(np.float32)), (0, 0), 9)
    mask &= dense < 0.22

    m = (mask.astype(np.uint8)) * 255
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE,
                         cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3)))
    m = cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=1)

    # 丢弃过大的连通域(极可能是主体边缘)
    n, lab, stats, _ = cv2.connectedComponentsWithStats(m, 8)
    keep = np.zeros_like(m)
    limit = max(400, m.size // 2000)
    for i in range(1, n):
        area = stats[i, cv2.CC_STAT_AREA]
        w, h = stats[i, cv2.CC_STAT_WIDTH], stats[i, cv2.CC_STAT_HEIGHT]
        if area <= limit and max(w, h) <= max(m.shape) * 0.45:
            keep[lab == i] = 255
    return keep


def detect_mask_consensus(images: Sequence[np.ndarray],
                          sensitivity: float = 1.0,
                          size: Tuple[int, int] = (768, 768)) -> np.ndarray:
    """多图共识掩膜：归一化尺寸后取残差中值，提取共同水印笔画。"""
    if len(images) < 2:
        return np.zeros(images[0].shape[:2], np.uint8)

    stack = []
    for img in images:
        g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        g = cv2.resize(g, size, interpolation=cv2.INTER_AREA)
        stack.append(_residual(g, 3.0))
    med = np.median(np.stack(stack), axis=0)
    a = np.abs(med)
    thr = float(np.percentile(a, 99.2) / max(sensitivity, 0.2))
    m = (a >= max(thr, 2.0)).astype(np.uint8) * 255
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
    return m


def _mask_for(img: np.ndarray, consensus: Optional[np.ndarray],
              sensitivity: float) -> np.ndarray:
    h, w = img.shape[:2]
    m = detect_mask_single(img, sensitivity)
    if consensus is not None and consensus.any():
        c = cv2.resize(consensus, (w, h), interpolation=cv2.INTER_NEAREST)
        c = cv2.dilate(c, np.ones((3, 3), np.uint8), 1)
        m = cv2.max(m, c)
    return m


def inpaint(img: np.ndarray, mask: np.ndarray, radius: int = 3,
            method: str = "telea") -> np.ndarray:
    if not mask.any():
        return img.copy()
    flag = cv2.INPAINT_TELEA if method == "telea" else cv2.INPAINT_NS
    out = cv2.inpaint(img, mask, radius, flag)
    # 二次轻修：对掩膜区域做导向平滑，压掉残留鬼影
    soft = cv2.medianBlur(out, 3)
    m3 = cv2.GaussianBlur(mask, (0, 0), 1.0).astype(np.float32) / 255.0
    m3 = np.clip(m3, 0, 1)[..., None]
    return (out * (1 - m3 * 0.35) + soft * (m3 * 0.35)).astype(np.uint8)


def crop_border(img: np.ndarray, ratio: float = 0.06) -> np.ndarray:
    h, w = img.shape[:2]
    dy, dx = int(h * ratio), int(w * ratio)
    return img[dy:h - dy, dx:w - dx]


def remove_watermark_batch(paths: List[Path], outdir: Path, mode: str = "auto",
                           sensitivity: float = 1.0, radius: int = 3,
                           keep_ext: bool = True, log=None) -> List[Path]:
    """批量去水印。mode: auto | consensus | crop | none"""
    log = log or (lambda m: None)
    outdir.mkdir(parents=True, exist_ok=True)
    if mode == "none" or not paths:
        return []

    imgs = []
    for p in paths:
        try:
            imgs.append((p, load_bgr(p)))
        except Exception as e:  # noqa: BLE001
            log(f"  读取失败 {p.name}: {e}")

    consensus = None
    if mode == "consensus" and len(imgs) >= 2:
        consensus = detect_mask_consensus([i for _, i in imgs], sensitivity)
        cov = float((consensus > 0).mean())
        log(f"  共识掩膜覆盖率 {cov*100:.2f}%")
        if cov > 0.25:      # 覆盖太广说明检测发散，退回单图模式
            log("  共识掩膜过于发散，回退到 auto")
            consensus = None

    out: List[Path] = []
    for p, img in imgs:
        if mode == "crop":
            res = crop_border(img)
            m_cov = 0.0
        else:
            mask = _mask_for(img, consensus, sensitivity)
            m_cov = float((mask > 0).mean())
            res = inpaint(img, mask, radius)
        ext = p.suffix if keep_ext else ".png"
        dst = outdir / f"{p.stem}_clean{ext}"
        save_bgr(dst, res)
        log(f"  去水印 {p.name} -> {dst.name} (掩膜 {m_cov*100:.2f}%)")
        out.append(dst)
    return out
