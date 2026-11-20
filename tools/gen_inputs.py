"""生成仿真输入信号（float32 裸数据，单位伏特）

产物（写到 out/）：
  mic_96k.raw   麦克风前置放大后的模拟信号，96kHz，围绕 1.65V
  mic_5k.raw    5kHz 单音探针，用于验证 8kHz 采样的折返
  music_48k.raw 外部播放器送入的立体声音乐，48kHz 交织 L/R，围绕 0V

信号是自己合成的，因为拿不到真实录音的授权；但频谱结构
（基频 150Hz 附近起伏的谐波 + 齿音噪声 + 立体声相位差）
足以覆盖这条链路会出问题的地方。
"""
import os

import numpy as np

FS_MIC = 96000
FS_AUD = 48000
DUR = 3.0
OUT = "out"


def write_f32(name, arr):
    path = os.path.join(OUT, name)
    arr.astype(np.float32).tofile(path)
    print(f"  {path:24s} {arr.size:7d} samples  {arr.size * 4 / 1024:6.0f} KB")


def synth_voice(rng, n, fs):
    """合成"人声"：谐波叠加 + 音节包络调制的齿音噪声"""
    t = np.arange(n) / fs

    # 基频在 150Hz 附近缓慢起伏，模拟语调
    f0 = 148 + 18 * np.sin(2 * np.pi * 0.7 * t) + 6 * np.sin(2 * np.pi * 2.3 * t)
    ph = 2 * np.pi * np.cumsum(f0) / fs

    voice = np.zeros_like(t)
    for k, amp in enumerate([1.0, 0.62, 0.42, 0.26, 0.16, 0.10, 0.07, 0.05], start=1):
        voice += amp * np.sin(k * ph)

    # 齿音：一阶差分（高频提升）后的白噪声，按 1.6Hz 的音节包络调制
    hf = np.diff(rng.normal(0, 1, n), prepend=0.0)
    syll = 0.55 + 0.45 * np.abs(np.sin(2 * np.pi * 1.6 * t))
    voice += 0.10 * hf / (np.max(np.abs(hf)) + 1e-12) * syll

    return voice / np.max(np.abs(voice))


def synth_music(rng, n, fs):
    """合成立体声音乐：四个和弦，左右声道有微小失谐与不同的镲片位置"""
    t = np.arange(n) / fs
    chords = [[220.0, 277.2, 329.6], [246.9, 311.1, 370.0],
              [196.0, 246.9, 293.7], [220.0, 277.2, 329.6]]
    seg = n // len(chords)

    left = np.zeros_like(t)
    right = np.zeros_like(t)
    for idx, ch in enumerate(chords):
        s, e = idx * seg, min((idx + 1) * seg, n)
        env = np.linspace(1.0, 0.55, e - s)
        bump = 0.6 if idx % 2 == 0 else 1.0
        for j, f in enumerate(ch):
            left[s:e] += bump * env * np.sin(2 * np.pi * f * t[s:e]) / (j + 1.2)
            right[s:e] += bump * env * np.sin(2 * np.pi * f * 1.003 * t[s:e]) / (j + 1.2)

    # 镲片：左右错开 900 个样本，用来验证立体声没被破坏
    hat = np.diff(rng.normal(0, 1, n), prepend=0.0)
    gate = (np.sin(2 * np.pi * 4 * t) > 0.85).astype(float)
    left += 0.16 * hat * gate
    right += 0.16 * np.roll(hat, 900) * gate

    left /= np.max(np.abs(left))
    right /= np.max(np.abs(right))

    inter = np.empty(n * 2)
    inter[0::2] = 0.30 * left
    inter[1::2] = 0.30 * right
    return inter


def main():
    os.makedirs(OUT, exist_ok=True)
    rng = np.random.default_rng(7)

    print("生成输入信号：")
    n_mic = int(FS_MIC * DUR)
    t_mic = np.arange(n_mic) / FS_MIC

    voice = synth_voice(rng, n_mic, FS_MIC)
    write_f32("mic_96k.raw", 1.65 + 0.28 * voice)      # ±0.28V 围绕 1.65V 偏置

    write_f32("mic_5k.raw", 1.65 + 0.25 * np.sin(2 * np.pi * 5000 * t_mic))

    write_f32("music_48k.raw", synth_music(rng, int(FS_AUD * DUR), FS_AUD))

    # 全静音音乐，用于单独观察麦克风支路
    write_f32("music_silent.raw", np.zeros(int(FS_AUD * DUR) * 2))


if __name__ == "__main__":
    main()
