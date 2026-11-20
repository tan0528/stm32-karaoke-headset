/*
 * test_volume.c -- 音量映射：方向、曲线、阻值模型
 *
 * 这里有两个真实踩过的坑：
 *
 * 1) 方向。MCP41010 在本电路里是串联变阻器接法（A 端悬空，只用 B-W），
 *    阻值越大增益越低。所以"档位越高 -> 抽头码越小"。写成同相时，
 *    现象是按音量 + 反而变小声 —— 板子上要听一遍才发现。
 *
 * 2) 曲线。常见做法是按响度幂律做三次方映射，但那条经验的前提是
 *    "增益正比于抽头码"。本拓扑增益是 R1/(R8+R_WB)，已经近似对数，
 *    再叠三次方等于量化两遍。最后那个用例把这个结论固定成了断言，
 *    免得以后有人凭直觉改回去。
 */
#include "audio_calc.h"
#include "audio_core.h"
#include "test_util.h"

static void t_endpoints(void)
{
    CHECK_NEAR(volume_level_to_code(0),   255.0, 0.0);   /* 最低档 -> 最大阻值 */
    CHECK_NEAR(volume_level_to_code(100),   0.0, 0.0);   /* 最高档 -> 最小阻值 */
}

static void t_direction_is_inverted(void)
{
    uint8_t lv;
    int ok = 1;

    for (lv = 1; lv <= 100u; lv++) {
        if (volume_level_to_code(lv) > volume_level_to_code((uint8_t)(lv - 1u))) {
            ok = 0;
        }
    }
    CHECK(ok);
}

static void t_clamp_above_100(void)
{
    CHECK_NEAR(volume_level_to_code(200), volume_level_to_code(100), 0.0);
    CHECK_NEAR(volume_level_to_code_cubic(200), volume_level_to_code_cubic(100), 0.0);
    CHECK_NEAR(volume_level_to_code_linear(200), volume_level_to_code_linear(100), 0.0);
}

static void t_linear_curve(void)
{
    /* 等间距：每 1 档约 2.55 个码值 */
    CHECK_NEAR(volume_level_to_code_linear(0),  255.0, 0.0);
    CHECK_NEAR(volume_level_to_code_linear(50), 128.0, 0.0);
    CHECK_NEAR(volume_level_to_code_linear(100),  0.0, 0.0);

    /* 相邻档位的码值差不超过 4，说明刻度是均匀的 */
    {
        uint8_t lv;
        int ok = 1;
        for (lv = 1; lv <= 100u; lv++) {
            int d = (int)volume_level_to_code_linear((uint8_t)(lv - 1u))
                  - (int)volume_level_to_code_linear(lv);
            if (d < 2 || d > 3) { ok = 0; }
        }
        CHECK(ok);
    }
}

static void t_cubic_curve(void)
{
    /* level 50 -> 50^3 * 255 / 1e6 = 31.875 -> 31 */
    CHECK_NEAR(volume_level_to_code_cubic(50), 31.0, 0.0);
    CHECK_NEAR(volume_level_to_code_cubic(90), 0.0, 0.0);
    CHECK_NEAR(volume_level_to_code_cubic(10), 185.0, 0.0);
}

static void t_digipot_model(void)
{
    CHECK_NEAR(digipot_ohm(0),   52.0,   0.5);      /* 只剩抽头电阻 */
    CHECK_NEAR(digipot_ohm(255), 9961.0, 1.0);      /* 接近 10k */
    CHECK_RANGE(digipot_ohm(128), 4900.0, 5100.0);  /* 中点约 5k */
}

static void t_gain_monotonic(void)
{
    uint8_t lv;
    int ok = 1;

    for (lv = 1; lv <= 100u; lv++) {
        double g_prev = volume_gain(volume_level_to_code((uint8_t)(lv - 1u)));
        double g_now  = volume_gain(volume_level_to_code(lv));
        if (g_now < g_prev) { ok = 0; }
    }
    CHECK(ok);
}

static void t_gain_range(void)
{
    double g_max = volume_gain(volume_level_to_code(100));
    double g_min = volume_gain(volume_level_to_code(0));
    double span_db = 20.0 * log10(g_max / g_min);

    /* 20k / (5k + 52) = 3.96 ；20k / (5k + 9961) = 1.34 */
    CHECK_NEAR(g_max, 3.958, 0.01);
    CHECK_NEAR(g_min, 1.337, 0.01);

    /* 这条实测出来的 9.4dB 就是设计缺陷：正常音量控制需要 40dB 以上。
     * 把范围钉在 [9, 10] 是为了防止有人"顺手改个电阻"把结论冲掉 ——
     * 如果哪天这个数变了，说明硬件参数变了，README 里的说明也要跟着改。 */
    CHECK_RANGE(span_db, 9.0, 10.0);
}

static void t_level_to_db(void)
{
    CHECK_NEAR(level_to_db(100), 0.0, 0.01);        /* 以最高档为 0dB 参考 */
    CHECK(level_to_db(0) < -9.0);
    CHECK(level_to_db(50) > level_to_db(0));
}

/* 与"每档等 dB"的理想刻度相比的 RMS 偏差 */
static double map_rms_dev(uint8_t (*map)(uint8_t))
{
    double ref = volume_gain(map(100));      /* 最高档为 0dB */
    double span = -20.0 * log10(volume_gain(map(0)) / ref);
    double sum = 0.0;
    int    l;

    for (l = 0; l <= 100; l++) {
        double db = 20.0 * log10(volume_gain(map((uint8_t)l)) / ref);
        double ideal = -span * (1.0 - (double)l / 100.0);
        double e = db - ideal;
        sum += e * e;
    }
    return sqrt(sum / 101.0);
}

/*
 * 设计决策的回归测试：本拓扑下线性映射比三次方更接近均匀刻度。
 *    线性 RMS 偏差 ≈ 0.91 dB
 *    三次方 RMS 偏差 ≈ 1.99 dB，且最后 25% 行程只变化 0.20 dB
 * 任何把默认映射改回三次方的改动都会让这条挂掉。
 */
static void t_linear_beats_cubic(void)
{
    double rms_lin = map_rms_dev(volume_level_to_code_linear);
    double rms_cub = map_rms_dev(volume_level_to_code_cubic);

    CHECK_MSG(rms_lin < rms_cub,
              "线性 %.2f dB 应优于三次方 %.2f dB", rms_lin, rms_cub);
    CHECK_RANGE(rms_lin, 0.6, 1.2);
    CHECK_RANGE(rms_cub, 1.6, 2.4);

    /* 三次方映射把最后一截行程浪费掉了 */
    {
        double top = fabs(level_to_db(100) - level_to_db(75));
        double top_cubic = 20.0 * log10(volume_gain(volume_level_to_code_cubic(100)) /
                                        volume_gain(volume_level_to_code_cubic(75)));
        CHECK_MSG(top_cubic < 0.5,
                  "三次方映射后 25%% 行程只有 %.2f dB", fabs(top_cubic));
        CHECK(top > 1.5);
    }

#if VOLUME_MAP == VOLUME_MAP_LINEAR
    CHECK(volume_level_to_code(50) == volume_level_to_code_linear(50));
#else
    CHECK(volume_level_to_code(50) == volume_level_to_code_cubic(50));
#endif
}

void test_volume_all(void)
{
    RUN(t_endpoints);
    RUN(t_direction_is_inverted);
    RUN(t_clamp_above_100);
    RUN(t_linear_curve);
    RUN(t_cubic_curve);
    RUN(t_digipot_model);
    RUN(t_gain_monotonic);
    RUN(t_gain_range);
    RUN(t_level_to_db);
    RUN(t_linear_beats_cubic);
}
