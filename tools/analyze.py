"""链路测量与出图

跑一遍就能得到 README 里那张结果表和三张图：
  docs/figures/fig1_waveform.png
  docs/figures/fig2_spectrum.png
  docs/figures/fig3_volume.png
以及可以直接播放的 out/*.wav

用法：make demo（或先 python tools/gen_inputs.py）
"""
import os
import subprocess
import sys
import wave

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams["font.sans-serif"] = [
    "Microsoft YaHei", "SimHei", "Noto Sans CJK SC", "WenQuanYi Zen Hei", "DejaVu Sans"
]
plt.rcParams["axes.unicode_minus"] = False

FS_AUD = 48000
FS_MIC = 96000
FS_ADC = 8000
FS_HI = 2400000

OUT = "out"
FIG = os.path.join("docs", "figures")


# --------------------------------------------------------------------------
# 基础设施
# --------------------------------------------------------------------------
def find_sim():
    for p in (os.path.join("build", "sim"), os.path.join("build", "sim.exe")):
        if os.path.exists(p):
            return p
    sys.exit("找不到 build/sim，请先 make")


SIM = find_sim()


def run(mic, music, raw_out, level=100, gain=256, proc=2.0, probe_ms=None, probe_at=1.0):
    """跑一次仿真，返回 stdout 里的 key=value 字典"""
    csv_out = raw_out.replace(".raw", ".csv")
    cmd = [SIM, mic, music, raw_out, csv_out, str(level), str(gain), str(proc)]
    if probe_ms is not None:
        cmd += [str(probe_at), str(probe_ms)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"sim 失败: {mic} -> {raw_out}\n{r.stderr[:400]}")
    kv = {}
    for line in r.stdout.strip().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            kv[k] = v
    return kv


def rd(path, ch=1):
    a = np.fromfile(path, dtype=np.float32)
    return a.reshape(-1, 2) if ch == 2 else a


def write_wav(path, x, fs=FS_AUD):
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    peak = np.max(np.abs(x)) + 1e-12
    pcm = (np.clip(x / peak * 0.92, -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes(pcm.tobytes())
    print(f"  {path:32s} 峰值 {peak:.3f} V（已按峰值归一化）")


def spec(x, fs, n=65536):
    x = np.asarray(x, dtype=np.float64)
    x = x[:n] - np.mean(x[:n])
    w = np.hanning(x.size)
    amp = np.abs(np.fft.rfft(x * w)) / (x.size / 4)
    f = np.fft.rfftfreq(x.size, 1 / fs)
    return f, 20 * np.log10(amp + 1e-12)


def band_rms(path, lo=2500, hi=3500):
    a = rd(path, 2)[12000:, 0].astype(np.float64)
    a = a - a.mean()
    X = np.fft.rfft(a * np.hanning(a.size))
    fr = np.fft.rfftfreq(a.size, 1 / FS_AUD)
    X[(fr < lo) | (fr > hi)] = 0
    return float(np.std(np.fft.irfft(X, a.size)))


def cascade_lp(x, fc, order, fs):
    """order 级一阶低通串联。

    单极点 IIR 的冲激响应是 (1-a)*a^n，用截断到 -100dB 的核做卷积，
    与逐点递归的结果等价，但省掉百万次 Python 循环。
    """
    a = np.exp(-2 * np.pi * fc / fs)
    length = int(np.ceil(np.log(1e-5) / np.log(a)))
    kernel = (1 - a) * a ** np.arange(length)

    y = np.asarray(x, dtype=np.float64).copy()
    for _ in range(order):
        y = np.convolve(y, kernel, mode="full")[: y.size]
    return y


# --------------------------------------------------------------------------
# 1. 主场景：K 歌，音量最高档
# --------------------------------------------------------------------------
def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(FIG, exist_ok=True)

    print("① 主场景（音量 100，软件增益 1.0x）")
    st100 = run(f"{OUT}/mic_96k.raw", f"{OUT}/music_48k.raw", f"{OUT}/mix_100.raw",
                level=100, probe_ms=20.0)
    print(f"   ADC 触发 {st100['adc_ticks']} 次 / 削顶 {float(st100['clip_ratio'])*100:.2f}% "
          f"/ 摆幅 {st100['peak_swing']} V")

    print("② 音量最低档")
    st0 = run(f"{OUT}/mic_96k.raw", f"{OUT}/music_48k.raw", f"{OUT}/mix_0.raw", level=0)
    print(f"   抽头码 {st0['digipot_code']} / 阻值 {st0['digipot_ohm']} Ω "
          f"/ 增益 {st0['music_gain']}")

    # ---- 单麦克风支路（音乐静音），用于频谱 ----
    print("③ 麦克风单支路")
    run(f"{OUT}/mic_96k.raw", f"{OUT}/music_silent.raw", f"{OUT}/mic_only.raw")

    # ---- 5kHz 折返 ----
    print("④ 5kHz 探针：折返与抗混叠")
    run(f"{OUT}/mic_5k.raw", f"{OUT}/music_silent.raw", f"{OUT}/aa_off.raw")

    x5 = rd(f"{OUT}/mic_5k.raw")
    for order, fc, tag in ((6, 3200.0, "aa06"), (12, 2800.0, "aa12")):
        cascade_lp(x5, fc, order, FS_MIC).astype(np.float32).tofile(f"{OUT}/mic_5k_{tag}.raw")
        run(f"{OUT}/mic_5k_{tag}.raw", f"{OUT}/music_silent.raw", f"{OUT}/{tag}.raw")

    res_off, res_06, res_12 = (band_rms(f"{OUT}/aa_off.raw"),
                               band_rms(f"{OUT}/aa06.raw"),
                               band_rms(f"{OUT}/aa12.raw"))
    print(f"   3kHz 残留：无滤波 {res_off*1000:.0f} mV -> 6 阶 {res_06*1000:.0f} mV "
          f"-> 12 阶 {res_12*1000:.0f} mV")

    # ---- 音量扫描 ----
    print("⑤ 音量扫描")
    short = rd(f"{OUT}/mic_96k.raw")[: int(0.5 * FS_MIC)]
    short.astype(np.float32).tofile(f"{OUT}/mic_short.raw")
    levels = list(range(0, 101, 10))
    codes, gains = [], []
    for lv in levels:
        kv = run(f"{OUT}/mic_short.raw", f"{OUT}/music_48k.raw",
                 f"{OUT}/sweep_{lv}.raw", level=lv)
        codes.append(int(kv["digipot_code"]))
        gains.append(float(kv["music_gain"]))

    plot_waveform(st100)
    plot_spectrum(res_off, res_06, res_12)
    plot_volume(levels, codes, gains)

    # ---- 可听文件 ----
    print("⑥ 输出音频")
    write_wav(f"{OUT}/1_原始音乐_48k.wav", rd(f"{OUT}/music_48k.raw", 2))
    mic8k = rd(f"{OUT}/mic_96k.raw")[:: FS_MIC // FS_ADC] - 1.65
    write_wav(f"{OUT}/2_麦克风干声_8k.wav", mic8k, fs=FS_ADC)
    write_wav(f"{OUT}/3_合成输出_音量100.wav", rd(f"{OUT}/mix_100.raw", 2))
    write_wav(f"{OUT}/4_合成输出_音量0.wav", rd(f"{OUT}/mix_0.raw", 2))

    summarize(st100, st0, levels, gains, res_off, res_06, res_12)


# --------------------------------------------------------------------------
# 图 1：链路波形
# --------------------------------------------------------------------------
def plot_waveform(st):
    probe = np.loadtxt(f"{OUT}/mix_100.csv", delimiter=",", skiprows=1)
    # 探针从 1.0s 开始截，这里把时间轴拉回 0 起，读图更直观
    t_us = probe[:, 0] - probe[0, 0]
    v_pwm, v_rc, v_pre_l = probe[:, 1], probe[:, 2], probe[:, 4]
    mic = rd(f"{OUT}/mic_96k.raw")
    mic5 = rd(f"{OUT}/mic_5k.raw")

    fig, ax = plt.subplots(2, 2, figsize=(13.5, 8.0))
    fig.suptitle("① 一条麦克风样本走过的四段路：采样 → 映射 → 重构 → 合成",
                 fontsize=15, fontweight="bold")

    # (a) 原始 vs 8kHz 采样点
    n = int(0.005 * FS_MIC)
    t = np.arange(n) / FS_MIC * 1e3
    ax[0, 0].plot(t, mic[:n], lw=1.2, color="#868e96", label="96kHz 原始")
    idx = np.arange(0, n, FS_MIC // FS_ADC)
    ax[0, 0].plot(t[idx], mic[idx], "o", ms=5, color="#d6336c", label="8kHz 采样点")
    ax[0, 0].set_title("(a) 每 125µs 抓一个点，4kHz 以上直接丢")
    ax[0, 0].set_xlabel("ms")
    ax[0, 0].set_ylabel("V")
    ax[0, 0].legend(fontsize=9, loc="upper right")

    # (b) 占空比台阶：每个 125µs 窗口取平均，还原 DAC 真正"看到"的阶梯
    win = t_us <= 20000
    v_win = v_pre_l[win]
    step = FS_HI // FS_ADC                       # 300 个 2.4MHz 点 = 一个 8kHz 周期
    n_step = v_win.size // step
    v_step = v_win[: n_step * step].reshape(n_step, step).mean(axis=1) - 2.5
    t_step = (np.arange(n_step) + 0.5) * (1e6 / FS_ADC)
    ax[0, 1].step(t_step / 1e3, v_step, where="mid", lw=1.1, color="#1f6feb")
    ax[0, 1].plot(t_step / 1e3, v_step, "o", ms=2.5, color="#1f6feb")
    ax[0, 1].set_title("(b) 占空比每 125µs 换一次，输出是 8kHz 阶梯")
    ax[0, 1].set_xlabel("ms")
    ax[0, 1].set_ylabel("加法器输出（已去 2.5V 虚地）")

    # (c) 100kHz 载波与 RC 重构
    w45 = t_us <= 45
    seg_rc = v_rc[(t_us > 10) & w45]
    ripple_mv = (seg_rc.max() - seg_rc.min()) * 1000
    ax[1, 0].plot(t_us[w45], v_pwm[w45], lw=0.9,
                  color="#adb5bd", label="PWM 载波 100kHz")
    ax[1, 0].plot(t_us[w45], v_rc[w45], lw=1.8,
                  color="#e8590c", label="RC 重构后")
    ax[1, 0].set_title(f"(c) 一阶 RC（2.2k + 10nF）残留纹波 ≈ {ripple_mv:.0f} mVpp")
    ax[1, 0].set_xlabel("µs")
    ax[1, 0].set_ylabel("V")
    ax[1, 0].legend(fontsize=9, loc="upper right")

    # (d) 5kHz 折返
    n5 = 384                                    # 4ms @ 96kHz
    seg = mic5[:n5] - 1.65
    t5 = np.arange(n5) / FS_MIC * 1e3
    si = np.arange(0, n5, FS_MIC // FS_ADC)
    tt = np.arange(n5) / FS_MIC
    ax[1, 1].plot(t5, seg, lw=1.0, color="#adb5bd", label="5kHz 输入")
    ax[1, 1].plot(t5[si], seg[si], "o", ms=6, color="#d6336c", label="8kHz 采样点")
    ax[1, 1].plot(t5, -0.25 * np.sin(2 * np.pi * 3000 * tt), lw=1.4,
                  color="#2f9e44", label="连起来 = 3kHz")
    ax[1, 1].set_title("(d) 5kHz 在 8kHz 采样下折返成 3kHz，无法分辨")
    ax[1, 1].set_xlabel("ms")
    ax[1, 1].set_ylabel("V")
    ax[1, 1].legend(fontsize=9, loc="upper right")

    for a in ax.flat:
        a.grid(alpha=0.25)
    plt.tight_layout()
    plt.savefig(os.path.join(FIG, "fig1_waveform.png"), dpi=150)
    plt.close()


# --------------------------------------------------------------------------
# 图 2：频谱与抗混叠
# --------------------------------------------------------------------------
def plot_spectrum(res_off, res_06, res_12):
    mic = rd(f"{OUT}/mic_96k.raw")
    mo = rd(f"{OUT}/mic_only.raw", 2)[:, 0]

    f_in, s_in = spec(mic - 1.65, FS_MIC, 131072)
    f_out, s_out = spec(mo[12000:], FS_AUD, 131072)
    s_in -= s_in.max()
    s_out -= s_out.max()

    def smooth(x, k=17):
        return np.convolve(x, np.ones(k) / k, mode="same")

    fig, ax = plt.subplots(1, 2, figsize=(13, 4.6),
                           gridspec_kw={"width_ratios": [1.3, 1]})
    fig.suptitle("② 8kHz 采样买了什么、又丢了什么", fontsize=14, fontweight="bold")

    ax[0].plot(f_in, smooth(s_in), lw=1.0, color="#868e96", label="麦克风原始 96kHz")
    ax[0].plot(f_out, smooth(s_out), lw=1.3, color="#d6336c", label="过完链路 48kHz")
    ax[0].axvline(4000, color="#e8590c", ls="--", lw=1.2)
    ax[0].text(4130, 2, "4kHz Nyquist", color="#e8590c", fontsize=8.5,
               rotation=90, va="top")
    ax[0].annotate("这一段只剩采样残留，不是声音", xy=(5750, -50), xytext=(4680, -28),
                   color="#d6336c", fontsize=8.5,
                   arrowprops=dict(arrowstyle="->", color="#d6336c", lw=0.9))
    ax[0].annotate("原信号的齿音、气息全在这段", xy=(6350, -67), xytext=(4700, -71),
                   color="#868e96", fontsize=8.5,
                   arrowprops=dict(arrowstyle="->", color="#868e96", lw=0.9))
    ax[0].set_xlim(0, 7000)
    ax[0].set_ylim(-75, 5)
    ax[0].set_xlabel("Hz")
    ax[0].set_ylabel("dB（各自归一化）")
    ax[0].set_title("语音频谱：4kHz 以上不再有内容")
    ax[0].legend(fontsize=8.5, loc="upper right")
    ax[0].grid(alpha=0.25)

    names = ["无抗混叠", "6 阶 3.2kHz", "12 阶 2.8kHz"]
    vals = [res_off, res_06, res_12]
    bars = ax[1].bar(names, [v * 1000 for v in vals],
                     color=["#d6336c", "#f59f00", "#2f9e44"], width=0.55)
    for b, v in zip(bars, vals):
        label = "≈ 0" if v < 2e-4 else f"{v * 1000:.0f} mV"
        ax[1].text(b.get_x() + b.get_width() / 2, v * 1000 + 8, label,
                   ha="center", fontsize=11, fontweight="bold")
    ax[1].set_ylim(0, max(vals) * 1000 * 1.25 + 10)
    ax[1].set_ylabel("3kHz 处残留（mV rms）")
    ax[1].set_title("5kHz 折返到 3kHz 后的残留：滤波器能压多少")
    ax[1].grid(axis="y", alpha=0.25)

    plt.tight_layout()
    plt.savefig(os.path.join(FIG, "fig2_spectrum.png"), dpi=150)
    plt.close()


# --------------------------------------------------------------------------
# 图 3：音量曲线
# --------------------------------------------------------------------------
def plot_volume(levels, codes, gains):
    """左图用仿真实测值（默认映射），三次方曲线按固件同式计算"""
    g = np.asarray(gains, dtype=float)
    db = 20 * np.log10(g / g[-1])
    lv = np.asarray(levels, dtype=float)

    # 与 volume_level_to_code_cubic 同式，已被 t_cubic_curve 钉住
    lv_full = np.arange(0, 101, dtype=float)
    code_cub = np.floor((100 - lv_full) ** 3 * 255 / 1e6)
    g_cub = 20000.0 / (5000.0 + 52.0 + code_cub * (10000.0 - 52.0) / 256.0)
    db_cub = 20 * np.log10(g_cub / g_cub[-1])

    span = -db[0]
    ideal = -span * (1 - lv_full / 100.0)
    rms_lin = np.sqrt(((np.interp(lv_full, lv, db) - ideal) ** 2).mean())
    rms_cub = np.sqrt(((db_cub - ideal) ** 2).mean())

    fig, ax = plt.subplots(1, 2, figsize=(13, 4.6))
    fig.suptitle("③ 音量映射：为什么这个电路该用线性，而不是教科书里的三次方",
                 fontsize=14, fontweight="bold")

    ax[0].plot(lv_full, ideal, ":", lw=1.6, color="#495057", label="理想：每档等 dB")
    ax[0].plot(lv, db, "o-", lw=1.8, ms=5, color="#1f6feb", label="线性映射（实测）")
    ax[0].plot(lv_full, db_cub, "--", lw=1.4, color="#d6336c", label="三次方映射（计算）")
    ax[0].set_xlabel("音量档位")
    ax[0].set_ylabel("相对最高档的增益 (dB)")
    ax[0].set_title(f"总行程只有 {span:.1f} dB —— 这是电路的硬伤，不是映射的")
    ax[0].legend(fontsize=9, loc="lower left")
    ax[0].grid(alpha=0.25)
    ax[0].text(0.03, 0.96,
               f"与理想刻度的 RMS 偏差\n线性 {rms_lin:.2f} dB\n三次方 {rms_cub:.2f} dB",
               transform=ax[0].transAxes, va="top", fontsize=9,
               bbox=dict(boxstyle="round,pad=0.4", fc="#f8f9fa", ec="#dee2e6"))

    ax[1].plot(lv, codes, "o-", lw=1.8, ms=5, color="#2f9e44", label="线性映射（实测）")
    ax[1].plot(lv_full, code_cub, "--", lw=1.4, color="#d6336c", label="三次方映射（计算）")
    ax[1].axhspan(0, 20, color="#ffe3e3", alpha=0.7)
    ax[1].text(3, 7, "三次方：最后 25% 行程只有 0.2 dB", fontsize=8.5, color="#c92a2a")
    ax[1].set_xlabel("音量档位")
    ax[1].set_ylabel("MCP41010 抽头码")
    ax[1].set_title("串联变阻器：码值 -> 阻值 -> 增益本来就不是线性的")
    ax[1].legend(fontsize=9, loc="center right")
    ax[1].grid(alpha=0.25)

    plt.tight_layout()
    plt.savefig(os.path.join(FIG, "fig3_volume.png"), dpi=150)
    plt.close()


# --------------------------------------------------------------------------
# 结果汇总
# --------------------------------------------------------------------------
def summarize(st100, st0, levels, gains, res_off, res_06, res_12):
    print("\n" + "=" * 62)
    print("测量结果")
    print("=" * 62)
    rows = [
        ("ADC 触发次数（3 秒）", f"{st100['adc_ticks']} 次 = 8kHz"),
        ("输出帧数", f"{st100['out_frames']} 帧 @48kHz"),
        ("启动延迟", f"{st100['fifo_underrun']} 次 underrun ≈ 15.9ms"),
        ("丢块", st100["blocks_dropped"]),
        ("削顶比例", f"{float(st100['clip_ratio']) * 100:.2f}%"),
        ("最大摆幅", f"{st100['peak_swing']} V"),
        ("音量档 100 增益", f"{st100['music_gain']}x（{20 * np.log10(float(st100['music_gain'])):.1f} dB）"),
        ("音量档 0 增益", f"{st0['music_gain']}x（{20 * np.log10(float(st0['music_gain'])):.1f} dB）"),
        ("音量总行程", f"{20 * np.log10(float(st100['music_gain']) / float(st0['music_gain'])):.1f} dB"),
        ("5kHz 折返残留", f"{res_off * 1000:.0f} / {res_06 * 1000:.0f} / {res_12 * 1000:.0f} mV"),
    ]
    for k, v in rows:
        print(f"  {k:18s} {v}")
    print("=" * 62)


if __name__ == "__main__":
    main()
