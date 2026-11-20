/*
 * run_tests.c -- 测试入口
 *
 * 不引 gtest / Unity / CMocka：这个项目的测试量级用不上，
 * 而且交叉编译环境下少一个依赖就少一个坑。
 */
#include <stdio.h>

#include "test_util.h"

int g_checks = 0;
int g_fails  = 0;

void test_run_case(const char *name, void (*fn)(void))
{
    int before = g_fails;

    fn();
    printf("  %s  %s\n", (g_fails == before) ? "PASS" : "FAIL", name);
}

void test_audio_core_all(void);
void test_volume_all(void);
void test_key_all(void);
void test_engine_all(void);

int main(void)
{
    printf("\n=== audio_core: 块处理 ===\n");
    test_audio_core_all();

    printf("\n=== volume: 音量映射 ===\n");
    test_volume_all();

    printf("\n=== key: 线控按键 ===\n");
    test_key_all();

    printf("\n=== engine: 端到端链路 ===\n");
    test_engine_all();

    printf("\n----------------------------------------\n");
    if (g_fails == 0) {
        printf("全部通过：%d 项断言\n\n", g_checks);
        return 0;
    }
    printf("失败 %d / %d 项断言\n\n", g_fails, g_checks);
    return 1;
}
